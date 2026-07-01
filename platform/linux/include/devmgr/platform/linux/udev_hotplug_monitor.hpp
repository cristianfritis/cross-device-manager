#pragma once
#include <atomic>
#include <mutex>
#include <thread>

#include "devmgr/pal/interfaces.hpp"

// Opaque libudev types — forward-declared so this public header stays free of
// <libudev.h> (same rule the enumerator header follows). Defined in the .cpp.
struct udev;
struct udev_monitor;

namespace devmgr::platform_linux {

// Real libudev netlink monitor. start() spawns a reader thread that translates
// uevents to pal::HotplugEvent and invokes the callback ON THAT THREAD.
//
// stop() (also run by the destructor) is safe to call REENTRANTLY from inside
// the callback itself — e.g. a HotplugService reacting to a fatal event by
// tearing down monitoring. A call made from the reader thread only signals
// "please stop" (so readLoop()/drainEvents() unwind without making any further
// libudev calls); it never joins itself and never releases monitor_/udev_ while
// drainEvents()'s loop (the very thing invoking the callback) might still touch
// them afterwards. The actual thread join + libudev cleanup is performed by the
// first stop() call made from any OTHER thread (a later external stop(), or the
// destructor). Concretely: if stop() is only ever called reentrantly from the
// callback and never again from any other thread, the reader thread exits but
// its std::thread stays joinable and its udev/udev_monitor handles stay
// allocated until an external stop()/destructor call completes the hand-off —
// the same requirement DelayedScheduler places on its own reentrant shutdown().
//
// As with DelayedScheduler, this object must never be *destroyed* (as opposed
// to stopped) synchronously from within its own callback: destroying a
// joinable std::thread member calls std::terminate(). Hand off to another
// thread first (e.g. via IUiDispatcher::post) if the callback needs to tear
// down the owner itself.
//
// start()/stop() are NOT safe to call concurrently from multiple external
// threads — this class assumes a single owner drives its lifecycle
// sequentially, matching the composition root's ownership model.
class UdevHotplugMonitor final : public pal::IHotplugMonitor {
   public:
    UdevHotplugMonitor() = default;
    ~UdevHotplugMonitor() override;
    UdevHotplugMonitor(const UdevHotplugMonitor&) = delete;
    UdevHotplugMonitor& operator=(const UdevHotplugMonitor&) = delete;

    core::Result<void> start(Callback callback) override;
    void stop() override;

   private:
    void readLoop();
    void drainEvents();    // receive+dispatch queued uevents until empty or stop requested
    void freeResources();  // unref monitor/udev, close eventfd (null/-1 guarded)

    Callback callback_;
    udev* udev_ = nullptr;
    udev_monitor* monitor_ = nullptr;
    int wakeFd_ = -1;
    std::thread reader_;

    // Set by the reader thread itself as its very first action — never derived
    // from reader_.get_id() directly, which could still be mid-move-assignment
    // on the starting thread at that instant (reader_ is reassigned on every
    // restart, unlike a thread only ever initialized once). Lets stop() reliably
    // tell a reentrant (reader-thread) call apart from an external one.
    std::atomic<std::thread::id> readerThreadId_{};
    std::atomic<bool> stopRequested_{false};  // polled by the reader loop to unwind promptly

    std::mutex lifecycleMutex_;    // guards started_/cleanupClaimed_ (rare transitions only)
    bool started_ = false;         // true from a successful start() until cleanup fully completes
    bool cleanupClaimed_ = false;  // true once some external thread has claimed join+freeResources
};

}  // namespace devmgr::platform_linux
