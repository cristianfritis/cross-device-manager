#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace devmgr::runtime {

// Fixed-size thread pool. Destructor drains queued work then joins workers.
class TaskScheduler {
   public:
    explicit TaskScheduler(std::size_t threadCount = defaultThreadCount());
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using Return = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<Return()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<Return> future = task->get_future();
        {
            std::scoped_lock lock(mutex_);
            if (stopping_) throw std::runtime_error("TaskScheduler is stopping");
            queue_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    std::size_t threadCount() const { return workers_.size(); }
    static std::size_t defaultThreadCount();

   private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

}  // namespace devmgr::runtime
