#include <atomic>
#include <condition_variable>
#include <future>
#include <functional>
#include <mutex>
#include <queue>
#include <ranges>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>


struct Request{
    int8_t id;
    int8_t thread;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::steady_clock::time_point start_time;
    unsigned int size;
    unsigned int address;
    std::string type;
    std::chrono::steady_clock::time_point over_time;
};


class ThreadPool {

    std::vector<std::jthread> threads;
    std::vector<std::queue<Request>> queues_by_threads;
    std::mutex m_mtx;
    std::condition_variable_any m_cv;
    std::vector<Request> requests_in_queues;

    ThreadPool(int8_t N){
        for (size_t i = 0; i < N; ++i) {
            threads.emplace_back([this, i](std::stop_token stok){
                while (true) {
                    std::unique_lock<std::mutex> lk(m_mtx);

                    /* Wait for either a task to be added, or a stop to be requested */
                    m_cv.wait(lk, stok, [this,i]() { return !queues_by_threads[i].empty(); });
                    if (stok.stop_requested() && queues_by_threads[i].empty()) {
                        return;
                    }

                    Request request = queues_by_threads[i].front();
                    queues_by_threads[i].pop();
                    lk.unlock();
                    std::this_thread::sleep_for(request.over_time-request.start_time);
                }
            });

            }
        }

    std::vector<Request> requests_in_queues_that_interferes_with_req (Request req){
        std::vector<Request> intersects;
        for(const auto written : requests_in_queues){
            bool start_in_written = (req.address >= written.address && req.address <= written.size);
            bool end_in_written = ((req.address + req.size) >= written.address && (req.address + req.size) <= written.size);
            if(start_in_written || end_in_written){
                /* todo tommorrow(today): check if it's really bad idea to return list of whole requests rather than just array of [thread_id, start_time, over_time]*/
                intersects.push_back(req);
            }
        }
        return intersects;
    }

    void add_request(int8_t thread, Request request,std::chrono::steady_clock::time_point timestamp){
        //std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
        std::queue<Request> queue = queues_by_threads.at(thread);
        int8_t base_latency = 1 + static_cast<int8_t>(request.type == "READ");
        std::chrono::microseconds latency = std::chrono::microseconds(request.size*base_latency);
        if (queue.empty() || queue.front().over_time < timestamp){
            request.start_time = request.timestamp;
            request.over_time = request.timestamp + latency;
            requests_in_queues.push_back(request);
            queue.push(request);
        }
        else{
            request.start_time = queue.front().over_time;
            request.over_time = request.start_time + latency;
            requests_in_queues.push_back(request);
            queue.push(request);
        }
    }

    int8_t optimal_thread_to_insert(std::vector<int8_t> ids, std::chrono::microseconds latency){
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::vector<std::chrono::steady_clock::time_point> best_times(queues_by_threads.size());
        std::queue<Request> queue;
        Request req;


        for (int8_t queue_id ; queue_id < queues_by_threads.size(); ++queue_id){
            queue = queues_by_threads.at(queue_id);
            if (queue.empty()){
                best_times.at(queue_id) = std::chrono::steady_clock::now();
            }
            /* todo tommorrow(today): make it work as I drew on whiteboard */
            if (queue.)
        }

    }

    bool timestamp_intersects_interfering_requests(std::chrono::steady_clock::time_point timestamp, Request req){
        std::vector<Request> interfering_reqs = requests_in_queues_that_interferes_with_req(req);
        for (const auto interfering_req : interfering_reqs){
            if (timestamp>=req.start_time && timestamp<=req.over_time){
                return true;
            }
        }
        return false;
    }

};

int main(){



}