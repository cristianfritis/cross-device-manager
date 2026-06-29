#include "devmgr/runtime/task_scheduler.hpp"

#include <algorithm>

namespace devmgr::runtime {

std::size_t TaskScheduler::defaultThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) return 2;
    return std::max(2u, hardware);
}

TaskScheduler::TaskScheduler(std::size_t threadCount) {
    if (threadCount == 0) threadCount = 1;
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) workers_.emplace_back([this] { workerLoop(); });
}

TaskScheduler::~TaskScheduler() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

void TaskScheduler::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop();
        }
        job();
    }
}

}  // namespace devmgr::runtime
