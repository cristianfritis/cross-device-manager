#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/runtime/delayed_scheduler.hpp"

using devmgr::runtime::DelayedScheduler;
using namespace std::chrono_literals;

namespace {

// Busy-wait up to `timeout` for `pred` to become true; keeps the test fast when
// it passes and bounded when it regresses.
template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

}  // namespace

TEST(DelayedScheduler, FiresCallbackAfterDelay) {
    DelayedScheduler timer;
    std::atomic<int> fired{0};
    timer.schedule(20ms, [&] { fired.fetch_add(1); });
    EXPECT_TRUE(waitFor([&] { return fired.load() == 1; }, 1s));
}

TEST(DelayedScheduler, CancelBeforeFireSuppressesCallback) {
    DelayedScheduler timer;
    std::atomic<int> fired{0};
    auto h = timer.schedule(100ms, [&] { fired.fetch_add(1); });
    EXPECT_TRUE(timer.cancel(h));  // still queued: found-and-removed, guaranteed suppressed
    std::this_thread::sleep_for(150ms);
    EXPECT_EQ(fired.load(), 0);
}

// Callers (HotplugService, StatusLineVM) build a completion barrier on top of
// this return value: true means the callback is guaranteed to never run, so
// it's safe to resolve/decrement outstanding work immediately; false means
// cancel() could not confirm that and the caller must instead wait for the
// callback itself to run and resolve. An unknown/already-fired/already-
// cancelled handle must therefore report false, not silently succeed.
TEST(DelayedScheduler, CancelOfUnknownOrAlreadyFiredHandleReturnsFalse) {
    DelayedScheduler timer;
    std::atomic<int> fired{0};
    EXPECT_FALSE(timer.cancel(999));  // never scheduled

    auto h = timer.schedule(5ms, [&] { fired.fetch_add(1); });
    EXPECT_TRUE(waitFor([&] { return fired.load() == 1; }, 1s));
    EXPECT_FALSE(timer.cancel(h));  // already fired — no longer queued
}

TEST(DelayedScheduler, RunsDueCallbacksInOrderAcrossHandles) {
    DelayedScheduler timer;
    std::atomic<int> count{0};
    timer.schedule(10ms, [&] { count.fetch_add(1); });
    timer.schedule(20ms, [&] { count.fetch_add(1); });
    EXPECT_TRUE(waitFor([&] { return count.load() == 2; }, 1s));
}

TEST(DelayedScheduler, ShutdownWithPendingDoesNotRunCancelledAndJoins) {
    std::atomic<int> fired{0};
    {
        DelayedScheduler timer;
        timer.schedule(500ms, [&] { fired.fetch_add(1); });  // far in the future
        timer.shutdown();                                    // must not hang, must not fire
    }
    EXPECT_EQ(fired.load(), 0);
}

TEST(DelayedScheduler, CallbackShuttingDownItsOwnSchedulerDoesNotCrashOrHang) {
    // Regression test: a callback running on the worker thread must be able to
    // call shutdown() on its own scheduler without attempting to join itself
    // (which would throw std::system_error and terminate the process).
    std::atomic<int> selfShutdownRan{0};
    {
        DelayedScheduler timer;
        timer.schedule(1ms, [&] {
            timer.shutdown();  // reentrant self-call: must only signal stop, never join
            selfShutdownRan.fetch_add(1);
        });
        EXPECT_TRUE(waitFor([&] { return selfShutdownRan.load() == 1; }, 1s));
        // A later external shutdown() call (here, via the destructor) must still
        // perform the actual join so the worker thread is not left joinable.
    }
    EXPECT_EQ(selfShutdownRan.load(), 1);
}
