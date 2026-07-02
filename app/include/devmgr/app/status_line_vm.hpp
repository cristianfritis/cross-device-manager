#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Toolkit-agnostic transient status line. Subscribes to device deltas and shows
// the latest ("usb device added: <name>"), auto-clearing after `ttl` via a
// DelayedScheduler. Starts DISARMED and ignores events until arm() so the
// initial bulk enumeration produces no message. Handlers may run on the timer
// thread (applyDelta publishes there), so text_ is mutex-guarded; every update
// wakes the UI via IUiDispatcher::post.
//
// DelayedScheduler::cancel() is best-effort (see delayed_scheduler.hpp): it
// returns true only if it actually found and removed the still-queued clear
// (guaranteeing the callback will never run); it returns false if the timer
// thread had already dequeued it — in which case the callback either already
// finished or is about to run, and cancel() cannot tell which nor stop it.
// Two hazards follow from that false case, both handled here:
//   1. (destructor UAF) The callback could still be mid-flight, about to lock
//      mutex_, after ~StatusLineVM() returns and text_/mutex_/armed_ start
//      being destroyed. Fixed with a completion barrier (outstandingClears_ +
//      clearDone_): outstandingClears_ is incremented at schedule() time (in
//      setMessage()) and decremented either when cancel() returns true
//      (definitely will not run — resolved immediately) or, if cancel()
//      returns false, only once the callback itself actually finishes
//      running (resolved late). ~StatusLineVM() blocks until this reaches
//      zero, so it can never return while a dequeued-but-not-yet-run callback
//      is still outstanding. This is the same pattern HotplugService's
//      outstandingFlushes_/flushDone_ barrier (hotplug_service.hpp/.cpp)
//      follows: claiming in-flight status from inside the callback itself
//      (as an earlier version of both classes did) leaves a real gap between
//      the worker thread dequeuing an entry (so cancel() already reports
//      false) and that callback actually acquiring the lock to claim itself,
//      during which a waiter can observe zero in-flight work and return
//      early. Claiming at schedule() time instead, gated on cancel()'s bool
//      return, closes that gap.
//   2. (stale clobber) During ordinary operation (no destructor involved), a
//      superseded clear callback that slipped past a false cancel() could
//      otherwise wipe out a newer message and its own still-pending clear
//      timer. Fixed by tagging every scheduled clear with the generation_ it
//      was scheduled under; a callback whose generation no longer matches
//      generation_ knows it has been superseded and no-ops instead of
//      mutating text_/clearHandle_ (it still participates in the
//      outstandingClears_ barrier above, just without mutating state).
//
// Residual (not eliminated by this class): ~StatusLineVM() unsubscribes
// (subAdded_.reset()/subRemoved_.reset()/subChanged_.reset()) before waiting
// on the barrier above, which helps but does not fully close a separate,
// narrower gap in EventBus itself (event_bus.hpp). EventBus::publish()
// snapshots its handlers' shared_ptrs under its own lock, releases that
// lock, then invokes them outside it; Subscription::reset() (which drives
// EventBus::unsubscribe()) only erases the entry from the handler map under
// that same lock — it does not wait for an invocation that had already been
// snapshotted before reset() ran. So a bus.publish() call already past that
// snapshot point when the destructor runs can still invoke setMessage() on a
// `this` that is mid-destruction or already destroyed, later; unsubscribing
// first merely blocks any *new* snapshot from being taken after that point.
// Fully closing this would require a completion barrier inside
// EventBus::unsubscribe() itself — affecting every subscriber in the
// codebase (DeviceListVM, DeviceDetailVM, HotplugService, etc.), not just
// this class — and is deliberately out of scope here.
//
// Precondition: the referenced runtime::DelayedScheduler must outlive this
// object — its shutdown()/destructor must not run before this object's
// destructor completes — because the destructor blocks until every dequeued-
// but-not-yet-run clear resolves outstandingClears_, which requires the
// scheduler's timer thread to still be alive to run those callbacks. The
// composition root (tui/src/tui_app.cpp) honors this via declaration order.
class StatusLineVM {
   public:
    StatusLineVM(runtime::EventBus& bus, runtime::DelayedScheduler& timer,
                 IUiDispatcher& dispatcher,
                 std::chrono::milliseconds ttl = std::chrono::seconds(4));
    ~StatusLineVM();  // cancels any pending clear and blocks until it can no
                      // longer be outstanding, so no callback fires into a dead VM
    StatusLineVM(const StatusLineVM&) = delete;
    StatusLineVM& operator=(const StatusLineVM&) = delete;

    void arm() { armed_.store(true); }
    std::string text() const;

   private:
    void setMessage(std::string message);
    void onClearFired(std::uint64_t generation);  // may run on the timer thread

    runtime::DelayedScheduler& timer_;
    IUiDispatcher& dispatcher_;
    std::chrono::milliseconds ttl_;
    std::atomic<bool> armed_{false};
    mutable std::mutex mutex_;
    std::condition_variable clearDone_;  // signaled when outstandingClears_ reaches 0
    std::string text_;
    runtime::DelayedScheduler::Handle clearHandle_ = 0;
    std::uint64_t generation_ = 0;  // bumped each time a new clear is scheduled; guarded by mutex_
    int outstandingClears_ =
        0;  // guarded by mutex_; see ~StatusLineVM()/setMessage()/onClearFired()
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
