#include <atomic>
#include <condition_variable>
#include <future>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <iostream>
#include <numeric>


struct TimeSlot {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point over;
    std::chrono::microseconds latency;

    bool operator<(const TimeSlot &other) const {
        return start < other.start;
    }

    [[nodiscard]] bool intersects(const TimeSlot &other) const {
        return (start < other.over) && (over > other.start);
    }
};

struct Request {
    std::chrono::steady_clock::time_point timestamp;
    unsigned int size;
    unsigned int address;
    std::string type;

    TimeSlot timeSlot;

    //standard comparator for sorted vector
    bool operator<(const Request &other) const {
        return timeSlot < other.timeSlot;
    }

    [[nodiscard]] bool intersects_by_bytes(const Request &other) const;

};

bool Request::intersects_by_bytes(const Request &other) const {
    return (address < (other.address + other.size) && (address + size) > other.address);
}


class Server {

private:
    std::vector<Request> AllWriteRequests;
    std::vector<std::chrono::steady_clock::duration> WriteReqsTotalLatencies;
    std::vector<std::chrono::steady_clock::duration> ReadReqsTotalLatencies;
    std::vector<std::chrono::steady_clock::duration> add_req_durations;

    std::vector<std::jthread> threads;
    std::vector<std::queue<std::function<void()>>> queues_by_threads;
    std::mutex m_mtx;
    std::condition_variable_any m_cv;

    std::vector<std::chrono::steady_clock::time_point> queues_last_reqs_timeovers;
    // has to contain whole request so we can find write reqs that intersect given one by bytes


// Iterate by write requests in all queues and return timeslots of those that intersects given req by bytes
    std::vector<TimeSlot> timeslots_of_write_requests_in_queues_that_interferes_with_req_by_bytes(Request req) {
        std::vector<TimeSlot> intersects;
        for (const auto &written_write_req: AllWriteRequests) {
            if (req.intersects_by_bytes(written_write_req)) {
                intersects.push_back(written_write_req.timeSlot);
            }
        }
        return intersects;
    }

    std::pair<uint8_t, std::chrono::microseconds> optimal_thread_to_insert_and_delay_if_indeed(Request &req) {

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> best_times;

        std::vector<TimeSlot> interfering_reqs_timeslots = timeslots_of_write_requests_in_queues_that_interferes_with_req_by_bytes(
                req);

        for (size_t queue_id = 0; queue_id != queues_by_threads.size(); ++queue_id) {
            best_times.push_back(
                    get_best_time_for_thread(queue_id, interfering_reqs_timeslots, req.timeSlot.latency, now));
        }

        auto best_time_iterator = std::min_element(best_times.begin(), best_times.end());
        uint8_t thread_id = std::distance(best_times.begin(), best_time_iterator);

        std::chrono::microseconds time_difference;


        time_difference = std::chrono::duration_cast<std::chrono::microseconds>(
                *best_time_iterator - now
        );

        //filling req.timeslot
        req.timeSlot.start = *best_time_iterator;
        req.timeSlot.over = *best_time_iterator + req.timeSlot.latency;
        return std::pair<uint8_t, std::chrono::microseconds>{thread_id, time_difference};


    }


    std::chrono::steady_clock::time_point
    get_best_time_for_thread(size_t queue_id, std::vector<TimeSlot> interfering_reqs_timeslots,
                             std::chrono::microseconds latency,
                             std::chrono::steady_clock::time_point now) {
        auto queue_last_req_timeover = queues_last_reqs_timeovers.at(queue_id);
        if (queue_last_req_timeover == std::chrono::steady_clock::time_point::min()) { return now; }
        auto max_server_inactivity_time = std::chrono::microseconds(20);

        TimeSlot hypothetical_place_to_put_request{std::chrono::steady_clock::time_point::max()};
        if (queue_last_req_timeover < now) {// if best time is now
            hypothetical_place_to_put_request = TimeSlot{now, now + latency};
        } else {
            hypothetical_place_to_put_request = TimeSlot{queue_last_req_timeover, queue_last_req_timeover + latency};
        }

        // check if hypothetical place intersects with interfering requests
        // if it does - try to emplace it after a little server planned delay

        for (auto &interfering_reqs_timeslot: interfering_reqs_timeslots) {
            // if this process out of our interests range
            if (interfering_reqs_timeslot.start > queue_last_req_timeover + latency + max_server_inactivity_time) {
                //if it is - we don't need to even look at next one
                return hypothetical_place_to_put_request.start;
            }
            //if process intersects hypothetical place to put request
            if (interfering_reqs_timeslot.intersects(hypothetical_place_to_put_request)) {
                //if when process is over we can insert request, and it won't be inefficient
                if (interfering_reqs_timeslot.over - queue_last_req_timeover < max_server_inactivity_time) {
                    hypothetical_place_to_put_request = TimeSlot{interfering_reqs_timeslot.over,
                                                                 interfering_reqs_timeslot.over + latency};
                }
                    //if we can't - don't need to even look at next one
                else {
                    return std::chrono::steady_clock::time_point::max();
                }
            }
        }
        return hypothetical_place_to_put_request.start;
    }

public:
    Server(std::uint8_t N) {
        queues_last_reqs_timeovers.resize(N, std::chrono::steady_clock::time_point::min());
        queues_by_threads.resize(N);
        for (size_t i = 0; i < N; ++i) {
            threads.emplace_back([this, i](std::stop_token stok) {
                while (true) {
                    std::unique_lock<std::mutex> lk(m_mtx);

                    /* Wait for either a task to be added, or a stop to be requested */
                    m_cv.wait(lk, stok, [this, i]() { return !queues_by_threads[i].empty(); });

                    if (stok.stop_requested() && queues_by_threads[i].empty()) {
                        return;
                    }

                    std::function<void()> fn = queues_by_threads[i].front();
                    queues_by_threads[i].pop();
                    lk.unlock();
                    //
                    fn();

                }
            });

        }
    }


    void add_request(Request request) {
        //std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        // calculating info out of request's incomes:
        std::uint8_t base_latency = (request.type == "READ") ? 2 : 1;
        request.timeSlot.latency = std::chrono::microseconds(base_latency * request.size);
        auto latency = request.timeSlot.latency;
        //calculating best place to put request
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::pair<uint8_t, std::chrono::microseconds> optimal_thread_and_time_to_wait = optimal_thread_to_insert_and_delay_if_indeed(
                request);


        // parsing output
        auto thread_id = optimal_thread_and_time_to_wait.first;
        auto time_to_wait = optimal_thread_and_time_to_wait.second;
        auto &queue = queues_by_threads.at(thread_id);

        //if thread need to wait
        if (std::chrono::microseconds(0) != time_to_wait) {
            //put delay
            queue.emplace([time_to_wait]() { std::this_thread::sleep_for(time_to_wait); });
            //put request processing simulation
            queue.emplace([latency]() { std::this_thread::sleep_for(latency); });

        } else {
            queue.emplace([latency]() { std::this_thread::sleep_for(latency); });
        }
        if (request.type == "WRITE") {
            const auto place = std::lower_bound(AllWriteRequests.begin(), AllWriteRequests.end(), request);
            AllWriteRequests.insert(place, request);
            add_req_durations.push_back(std::chrono::steady_clock::now() - start);
            WriteReqsTotalLatencies.push_back(request.timeSlot.over - request.timestamp);
        } else {
            add_req_durations.push_back(std::chrono::steady_clock::now() - start);
            ReadReqsTotalLatencies.push_back(request.timeSlot.over - request.timestamp);
        }
        queues_last_reqs_timeovers.at(thread_id) = request.timeSlot.over;

        add_req_durations.push_back(std::chrono::steady_clock::now() - start);
    }

    ~Server() {
        threads.clear();

        std::sort(WriteReqsTotalLatencies.begin(), WriteReqsTotalLatencies.end());
        auto minWrite = WriteReqsTotalLatencies.front();
        auto maxWrite = WriteReqsTotalLatencies.back();
        auto sumWrite = std::accumulate(WriteReqsTotalLatencies.begin(), WriteReqsTotalLatencies.end(),
                                        std::chrono::high_resolution_clock::duration::zero());
        auto avgWrite = sumWrite / WriteReqsTotalLatencies.size();
        auto medianWrite = WriteReqsTotalLatencies.size() % 2 == 0
                           ? (WriteReqsTotalLatencies[WriteReqsTotalLatencies.size() / 2 - 1] +
                              WriteReqsTotalLatencies[WriteReqsTotalLatencies.size() / 2]) / 2
                           : WriteReqsTotalLatencies[WriteReqsTotalLatencies.size() / 2];

        std::cout << "WRITE Requests:\n";
        std::cout << "  Min Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(minWrite).count()
                  << " us\n";
        std::cout << "  Max Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(maxWrite).count()
                  << " us\n";
        std::cout << "  Median Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(medianWrite).count()
                  << " us\n";
        std::cout << "  Average Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(avgWrite).count()
                  << " us\n";

        std::sort(ReadReqsTotalLatencies.begin(), ReadReqsTotalLatencies.end());
        auto minRead = ReadReqsTotalLatencies.front();
        auto maxRead = ReadReqsTotalLatencies.back();
        auto sumRead = std::accumulate(ReadReqsTotalLatencies.begin(), ReadReqsTotalLatencies.end(),
                                       std::chrono::high_resolution_clock::duration::zero());
        auto avgRead = sumRead / ReadReqsTotalLatencies.size();
        auto medianRead = ReadReqsTotalLatencies.size() % 2 == 0
                          ? (ReadReqsTotalLatencies[ReadReqsTotalLatencies.size() / 2 - 1] +
                             ReadReqsTotalLatencies[ReadReqsTotalLatencies.size() / 2]) / 2
                          : ReadReqsTotalLatencies[ReadReqsTotalLatencies.size() / 2];

        std::cout << "READ Requests:\n";
        std::cout << "  Min Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(minRead).count()
                  << " us\n";
        std::cout << "  Max Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(maxRead).count()
                  << " us\n";
        std::cout << "  Median Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(medianRead).count()
                  << " us\n";
        std::cout << "  Average Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(avgRead).count()
                  << " us\n";


        auto sumAddReq = std::accumulate(add_req_durations.begin(), add_req_durations.end(),
                                         std::chrono::nanoseconds::zero());
        auto avgAddReq = sumAddReq / add_req_durations.size();
        std::cout << "add_request() processing time:\n";
        std::cout << "  Average Latency: " << std::chrono::duration_cast<std::chrono::microseconds>(avgAddReq).count()
                  << " us\n";


    }
};


int main() {
    Server server(10);
    std::chrono::time_point start = std::chrono::steady_clock::now() + std::chrono::microseconds(40);
    std::vector<Request> requests = {
            {start + std::chrono::microseconds(3),  5,  1024, "READ"},
            {start + std::chrono::microseconds(5),  5,  2048, "READ"},
            {start + std::chrono::microseconds(7),  10, 2048, "WRITE"},
            {start + std::chrono::microseconds(9),  10, 2052, "WRITE"},
            {start + std::chrono::microseconds(12), 4,  2048, "READ"},
            {start + std::chrono::microseconds(13), 1,  1024, "WRITE"},
            {start + std::chrono::microseconds(15), 10, 512,  "READ"},
            {start + std::chrono::microseconds(16), 20, 256,  "WRITE"},
            {start + std::chrono::microseconds(18), 5,  260,  "WRITE"},
            {start + std::chrono::microseconds(20), 7,  512,  "WRITE"},
            {start + std::chrono::microseconds(24), 10, 1024, "WRITE"},
            {start + std::chrono::microseconds(25), 10, 1024, "WRITE"},
            {start + std::chrono::microseconds(26), 10, 1024, "WRITE"},
            {start + std::chrono::microseconds(29), 2,  512,  "READ"},
            {start + std::chrono::microseconds(31), 15, 2048, "READ"},
            {start + std::chrono::microseconds(32), 6,  784,  "WRITE"},
            {start + std::chrono::microseconds(35), 3,  512,  "WRITE"},
            {start + std::chrono::microseconds(38), 4,  256,  "READ"},
            {start + std::chrono::microseconds(39), 6,  256,  "WRITE"},
            {start + std::chrono::microseconds(40), 10, 256,  "READ"},
            {start + std::chrono::microseconds(41), 5,  260,  "READ"},
            {start + std::chrono::microseconds(45), 5,  270,  "READ"},
            {start + std::chrono::microseconds(46), 5,  280,  "READ"},
            {start + std::chrono::microseconds(47), 20, 1000, "WRITE"},
            {start + std::chrono::microseconds(48), 20, 1010, "WRITE"},
            {start + std::chrono::microseconds(50), 20, 1020, "WRITE"},
            {start + std::chrono::microseconds(55), 30, 1000, "READ"},
            {start + std::chrono::microseconds(57), 30, 1000, "READ"},
            {start + std::chrono::microseconds(58), 10, 2052, "WRITE"},
            {start + std::chrono::microseconds(59), 4,  2048, "WRITE"},
            {start + std::chrono::microseconds(60), 1,  1024, "READ"},
    };

    for (auto &req: requests) {
        std::async(std::launch::async, [&server, req]() {

            std::this_thread::sleep_until(req.timestamp);

            server.add_request(req);
        });
    }
    return 0;

}
