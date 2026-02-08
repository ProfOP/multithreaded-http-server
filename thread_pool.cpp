#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this]() {
                worker_loop();
            });
        }
    }

    ~ThreadPool() {
        stop_pool();
    }

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(task);
        }
        condition.notify_one();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;

    void worker_loop() {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this]() {
                    return stop || !tasks.empty();
                });

                if (stop && tasks.empty()) {
                    return;
                }

                task = tasks.front();
                tasks.pop();
            }

            task(); // 🔥 execute task
        }
    }

    void stop_pool() {
        stop = true;
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

int main() {
    ThreadPool pool(4);

    for (int i = 0; i < 10; i++) {
        pool.enqueue([i]() {
            std::cout << "Task " << i
                      << " executed by thread "
                      << std::this_thread::get_id()
                      << std::endl;
        });
    }

    // Give threads time to finish
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
