#include "devmgr/runtime/delayed_scheduler.hpp"

#include <utility>
#include <vector>

namespace devmgr::runtime {

DelayedScheduler::DelayedScheduler() : worker_([this] { run(); }) {}

DelayedScheduler::~DelayedScheduler() {
    shutdown();
}

DelayedScheduler::Handle DelayedScheduler::schedule(std::chrono::milliseconds delay,
                                                    std::function<void()> fn) {
    const auto due = Clock::now() + delay;
    std::scoped_lock lock(mutex_);
    const Handle id = nextId_++;
    auto it = queue_.emplace(due, Entry{.id = id, .fn = std::move(fn)});
    index_.emplace(id, it);
    cv_.notify_all();  // a possibly-earlier deadline arrived
    return id;
}

void DelayedScheduler::cancel(Handle handle) {
    std::scoped_lock lock(mutex_);
    auto found = index_.find(handle);
    if (found == index_.end()) return;
    queue_.erase(found->second);
    index_.erase(found);
    cv_.notify_all();
}

void DelayedScheduler::shutdown() {
    const bool selfCall = (std::this_thread::get_id() == worker_.get_id());
    bool shouldJoin = false;
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        if (!selfCall && !joinStarted_) {
            joinStarted_ = true;
            shouldJoin = true;
        }
    }
    cv_.notify_all();
    if (shouldJoin && worker_.joinable()) worker_.join();
}

void DelayedScheduler::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (stopping_) return;
        if (queue_.empty()) {
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            continue;
        }
        const auto due = queue_.begin()->first;
        if (Clock::now() < due) {
            cv_.wait_until(lock, due);
            continue;  // re-evaluate: earliest deadline or stop may have changed
        }
        // Collect everything due now, then run outside the lock.
        std::vector<std::function<void()>> ready;
        const auto now = Clock::now();
        while (!queue_.empty() && queue_.begin()->first <= now) {
            auto it = queue_.begin();
            index_.erase(it->second.id);
            ready.push_back(std::move(it->second.fn));
            queue_.erase(it);
        }
        lock.unlock();
        for (auto& fn : ready) fn();  // callbacks may schedule()/cancel() re-entrantly
        lock.lock();
    }
}

}  // namespace devmgr::runtime
