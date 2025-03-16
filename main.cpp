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


struct TimeSlot {
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point over;
    std::chrono::milliseconds latency;

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


class ThreadPool {

    std::vector<std::jthread> threads;
    std::vector<std::queue<std::function<void()>>> queues_by_threads;
    std::mutex m_mtx;
    std::condition_variable_any m_cv;

    std::vector<std::chrono::steady_clock::time_point> queues_last_reqs_timeovers;
    // has to contain whole request so we can find write reqs that intersect given one by bytes
    std::vector<Request> all_write_requests_in_every_queue;

    explicit ThreadPool(std::uint8_t N) {
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


    // Iterate by write requests in all queues and return timeslots of those that intersects given req by bytes
    std::vector<TimeSlot> timeslots_of_write_requests_in_queues_that_interferes_with_req_by_bytes(Request req) {
        std::vector<TimeSlot> intersects;
        for (const auto &written_write_req: all_write_requests_in_every_queue) {
            if (req.intersects_by_bytes(written_write_req)) {
                intersects.push_back(written_write_req.timeSlot);
            }
        }
        return intersects;
    }

    void add_request(Request request) {



        // calculating info out of request's incomes:
        std::uint8_t base_latency = (request.type == "READ") ? 2 : 1;
        request.timeSlot.latency = std::chrono::milliseconds(base_latency * request.size);
        auto latency = request.timeSlot.latency;
        //calculating best place to put request
        std::pair<uint8_t, std::chrono::microseconds> optimal_thread_and_time_to_wait = optimal_thread_to_insert(
                request);

        // parsing output
        auto thread_id = optimal_thread_and_time_to_wait.first;
        auto time_to_wait = optimal_thread_and_time_to_wait.second;
        auto queue = queues_by_threads.at(thread_id);

        //if thread need to wait
        if (std::chrono::microseconds(0) != time_to_wait) {
            //put delay
            queue.emplace([time_to_wait]() { std::this_thread::sleep_for(time_to_wait); });
            queue.emplace([latency]() { std::this_thread::sleep_for(latency); });
        } else {
            queue.emplace([latency]() { std::this_thread::sleep_for(latency); });
        }
        if (request.type == "WRITE") {
            const auto place = std::lower_bound(all_write_requests_in_every_queue.begin(),
                                                all_write_requests_in_every_queue.end(), request);
            all_write_requests_in_every_queue.insert(place, request);
        }
        queues_last_reqs_timeovers.at(thread_id) = request.timeSlot.over;
    }

    std::pair<uint8_t, std::chrono::microseconds> optimal_thread_to_insert(Request &req) {

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> best_times(queues_by_threads.size());

        std::vector<TimeSlot> interfering_reqs_timeslots = timeslots_of_write_requests_in_queues_that_interferes_with_req_by_bytes(
                req);

        for (size_t queue_id = 0; queue_id != queues_by_threads.size() - 1; ++queue_id) {
            best_times.push_back(get_best_time_for_thread(queue_id, interfering_reqs_timeslots, req.timeSlot.latency));
        }

        auto best_element_iterator = std::min_element(best_times.begin(), best_times.end());
        uint8_t thread_id = std::distance(best_times.begin(), best_element_iterator);
        std::chrono::microseconds time_difference = std::chrono::duration_cast<std::chrono::microseconds>(
                *best_element_iterator - now
        );
        return std::pair<uint8_t, std::chrono::microseconds>{thread_id, time_difference};


    }

    std::chrono::steady_clock::time_point
    get_best_time_for_thread(size_t queue_id, std::vector<TimeSlot> interfering_reqs_timeslots,
                             std::chrono::microseconds latency) {
        auto now = std::chrono::steady_clock::now();
        auto max_server_inactivity_time = std::chrono::microseconds(7);
        auto queues_last_req_timeover = queues_last_reqs_timeovers.at(queue_id);
        TimeSlot hypothetical_place_to_put_request{std::chrono::steady_clock::time_point::max()};

        if (queues_last_req_timeover < now) {
            hypothetical_place_to_put_request = TimeSlot{now, now + latency};
        } else {
            hypothetical_place_to_put_request = TimeSlot{queues_last_req_timeover, queues_last_req_timeover + latency};

        }


        for (auto &interfering_reqs_timeslot: interfering_reqs_timeslots) {
            // if this process out of our interests range
            if (interfering_reqs_timeslot.start > queues_last_req_timeover + latency + max_server_inactivity_time) {
                //if it is - we don't need to even look at next one
                return hypothetical_place_to_put_request.start;
            }
            //if process intersects hypothetical place to put request
            if (interfering_reqs_timeslot.intersects(hypothetical_place_to_put_request)) {
                //if when process is over we can insert request, and it won't be inefficient
                if (interfering_reqs_timeslot.over - queues_last_req_timeover < max_server_inactivity_time) {
                    hypothetical_place_to_put_request = TimeSlot{interfering_reqs_timeslot.over,
                                                                 interfering_reqs_timeslot.over + latency};
                }
                    //if it will be we don't need to even look at next one
                else {
                    return std::chrono::steady_clock::time_point::max();
                }
            }
            return hypothetical_place_to_put_request.start;
        }
    }
};


int main() {


}