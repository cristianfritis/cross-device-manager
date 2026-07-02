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
// a Handle you may cancel() before it fires (best-effort: cancel() returns true
// only if it actually found and removed the still-queued entry, which then
// guarantees the callback will never run; it returns false if the entry was
// already dequeued by the worker thread — in that case the callback either has
// already run to completion or is about to run, and cancel() cannot tell which
// nor stop it. Callers that need to know the callback has *finished* — not
// merely that cancel() couldn't reach it — must build their own completion
// barrier around the false case: increment an outstanding-work counter at
// schedule() time, decrement it when cancel() returns true (definitely will
// not run) OR from inside the callback once it actually finishes (definitely
// did run); a false return from cancel() alone is not sufficient evidence of
// either. StatusLineVM (status_line_vm.hpp/.cpp) and HotplugService
// (hotplug_service.hpp/.cpp) both follow this pattern correctly, claiming
// outstanding status at schedule() time rather than from inside the
// callback — the latter would leave a real gap between a worker thread
// dequeuing an entry and that callback acquiring the lock to claim itself.
//
// Callbacks run on the timer thread, so keep them cheap; a callback may
// itself schedule()/cancel(), and a callback may also safely call shutdown()
// on its own scheduler (it just signals the worker to stop after the current
// callback batch; it will not try to join itself). shutdown() (also run by
// the destructor) stops and joins the thread from any OTHER (non-worker)
// thread. However, the DelayedScheduler object itself must
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
    // Returns true iff `handle` was still queued and was removed (the callback
    // is now guaranteed never to run). Returns false if `handle` was unknown —
    // already fired, already cancelled, or already dequeued by the worker
    // thread and about to run/running (see the class doc comment above).
    bool cancel(Handle handle);
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
