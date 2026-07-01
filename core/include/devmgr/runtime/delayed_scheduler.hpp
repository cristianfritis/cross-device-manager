#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace devmgr::runtime {

// Runs callbacks after a delay on a single background thread. schedule() returns
// a Handle you may cancel() before it fires (best-effort). Callbacks run on the
// timer thread, so keep them cheap; a callback may itself schedule()/cancel(), and
// a callback may also safely call shutdown() on its own scheduler (it just signals
// the worker to stop after the current callback batch; it will not try to join
// itself). shutdown() (also run by the destructor) stops and joins the thread from
// any OTHER (non-worker) thread. However, the DelayedScheduler object itself must
// never be *destroyed* synchronously from within its own callback — nothing can
// safely join a thread from inside itself, and destroying the std::thread member
// while still joinable calls std::terminate(). If a callback needs to tear down
// the owning object, it must hand off to another thread first (e.g. via
// IUiDispatcher::post).
class DelayedScheduler {
   public:
    using Handle = std::uint64_t;

    DelayedScheduler();
    ~DelayedScheduler();
    DelayedScheduler(const DelayedScheduler&) = delete;
    DelayedScheduler& operator=(const DelayedScheduler&) = delete;

    Handle schedule(std::chrono::milliseconds delay, std::function<void()> fn);
    void cancel(Handle handle);
    void shutdown();

   private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        Handle id;
        std::function<void()> fn;
    };
    using Queue = std::multimap<Clock::time_point, Entry>;

    void run();

    std::mutex mutex_;
    std::condition_variable cv_;
    Queue queue_;
    std::unordered_map<Handle, Queue::iterator> index_;
    Handle nextId_ = 1;
    bool stopping_ = false;
    bool joinStarted_ =
        false;  // guarded by mutex_; true once some external thread has claimed the join
    std::thread worker_;
};

}  // namespace devmgr::runtime
