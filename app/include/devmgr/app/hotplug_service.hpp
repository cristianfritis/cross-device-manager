#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"

namespace devmgr::app {

// Owns the IHotplugMonitor lifecycle and per-device trailing debounce. Monitor
// events (on the reader thread) coalesce into a pending map; a DelayedScheduler
// flush (on the timer thread) applies the latest event per device to the
// DeviceService. applyDelta runs on the timer thread — no TaskScheduler hop —
// so stopping the timer halts all hotplug-driven model mutation (deterministic
// shutdown). Depends on DelayedScheduler + DeviceService only.
//
// DelayedScheduler::cancel() is best-effort (see delayed_scheduler.hpp): it
// returns true only if it actually found and removed the still-queued flush
// (guaranteeing the callback will never run); it returns false if the timer
// thread had already dequeued it — in which case the callback either already
// finished or is about to run, and cancel() cannot tell which nor stop it.
// stop() (also run by the destructor) must not return while a dequeued-but-
// not-yet-run flush() is still outstanding: flush() reads/writes `this`'s own
// storage (pending_, service_, ...), so returning early would let a caller
// destroy this HotplugService while flush() is still mid-execution on the
// timer thread — a use-after-free of HotplugService's OWN storage, not merely
// a lifetime issue with the referenced DeviceService/DelayedScheduler.
//
// This is fixed with a completion barrier (outstandingFlushes_ + flushDone_)
// that claims outstanding status at schedule() time rather than from inside
// flush() itself: onEvent() increments outstandingFlushes_ in the very same
// critical section where it calls timer_.schedule() for a device's flush, and
// resolves (decrements) it either immediately, when a later cancel() for that
// same handle returns true (definitely will not run), or — if cancel()
// returns false — only once flush() itself actually finishes running,
// including its call into service_.applyDelta(). flush() decrements exactly
// once on every path (whether or not it still finds a pending_ entry for its
// id — see flush() for why), which is the only place a false-cancel()'d
// schedule ever gets resolved. stop() blocks on flushDone_.wait() until
// outstandingFlushes_ reaches 0, so it can never return while any dequeued-
// but-not-yet-run flush is still touching `this`. (Claiming from *inside* the
// callback instead, as an earlier version of this class did, leaves a real
// gap between the worker thread dequeuing an entry — so cancel() already
// reports false for it — and that callback actually acquiring the lock to
// claim itself, during which stop() could observe zero outstanding work and
// return early; see StatusLineVM (status_line_vm.hpp/.cpp), which follows
// this same corrected pattern.)
//
// flush() never calls stop(), so stop()'s wait for outstanding flushes to
// drain is an ordinary cross-thread wait (timer thread -> whatever thread
// owns this HotplugService), never a self-wait/self-join.
//
// Precondition: the referenced runtime::DelayedScheduler must outlive this
// object — its shutdown()/destructor must not run before this object's
// stop()/destructor completes — because stop() blocks until every dequeued-
// but-not-yet-run flush resolves outstandingFlushes_, which requires the
// scheduler's timer thread to still be alive to run those callbacks. The
// composition root (tui/src/tui_app.cpp) honors this via declaration order.
class HotplugService {
   public:
    HotplugService(pal::IHotplugMonitor& monitor, DeviceService& service,
                   runtime::DelayedScheduler& timer,
                   std::chrono::milliseconds window = std::chrono::milliseconds(250));
    ~HotplugService();
    HotplugService(const HotplugService&) = delete;
    HotplugService& operator=(const HotplugService&) = delete;

    core::Result<void> start();

    // Stops the monitor (joining its reader thread — see IHotplugMonitor::stop()),
    // cancels every pending debounce timer — resolving each immediately when
    // cancel() returns true, else leaving it counted for flush() to resolve
    // once it actually runs — then blocks until every outstanding flush has
    // finished. See the class doc comment above for why that barrier is
    // required. Idempotent: safe to call more than once, and safe even if
    // start() was never called.
    void stop();

   private:
    void onEvent(const pal::HotplugEvent& event);  // monitor reader thread
    void flush(const std::string& id);             // timer thread

    struct Pending {
        pal::HotplugEvent event;
        runtime::DelayedScheduler::Handle handle;
    };

    pal::IHotplugMonitor& monitor_;
    DeviceService& service_;
    runtime::DelayedScheduler& timer_;
    std::chrono::milliseconds window_;
    std::mutex mutex_;
    std::condition_variable flushDone_;  // signaled when outstandingFlushes_ reaches 0
    std::unordered_map<std::string, Pending> pending_;
    int outstandingFlushes_ =
        0;  // guarded by mutex_; claimed at schedule() time — see onEvent()/flush()/stop()
};

}  // namespace devmgr::app
