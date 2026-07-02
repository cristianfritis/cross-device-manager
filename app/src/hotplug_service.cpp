#include "devmgr/app/hotplug_service.hpp"

#include <optional>
#include <utility>

namespace devmgr::app {

HotplugService::HotplugService(pal::IHotplugMonitor& monitor, DeviceService& service,
                               runtime::DelayedScheduler& timer, std::chrono::milliseconds window)
    : monitor_(monitor), service_(service), timer_(timer), window_(window) {}

HotplugService::~HotplugService() {
    stop();
}

core::Result<void> HotplugService::start() {
    return monitor_.start([this](const pal::HotplugEvent& event) { onEvent(event); });
}

void HotplugService::stop() {
    monitor_.stop();  // joins the reader thread — onEvent will no longer be called
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto& [id, pending] : pending_) {
        // cancel() returning true guarantees this flush will never run, so we
        // can resolve (decrement) its outstanding claim right here; false
        // means the timer thread had already dequeued it — flush() itself
        // will decrement once it actually runs (it will find pending_
        // already cleared below and simply skip applying, per its own
        // comment). See the class doc comment for why the wait below, not
        // this cancel() call, is what actually resolves that case.
        if (timer_.cancel(pending.handle)) --outstandingFlushes_;
    }
    pending_.clear();
    // Wait for any flush() the scheduler had already dequeued (and which the
    // cancel() calls above therefore could not reach) to finish running
    // before returning — see the class doc comment for why this barrier is
    // required, and why claiming outstandingFlushes_ at schedule() time (in
    // onEvent()), not from inside flush(), is what makes this wait correct:
    // it cannot observe zero while a dequeued-but-not-yet-run flush is still
    // about to touch `this`.
    flushDone_.wait(lock, [this] { return outstandingFlushes_ == 0; });
}

void HotplugService::onEvent(const pal::HotplugEvent& event) {
    std::scoped_lock lock(mutex_);
    const std::string id = event.device.id.value;
    auto it = pending_.find(id);
    if (it != pending_.end()) {
        // Best-effort: if flush() has already been dequeued by the scheduler,
        // this cancel() is a no-op and that flush fires immediately (slightly
        // ahead of the nominal window) carrying whatever event it already
        // captured. The final state is still correct — this newer event lands
        // in `pending_` and gets its own fresh window — just the debounce
        // timing is approximate under adversarial scheduler racing. cancel()
        // returning true guarantees the old flush will never run, so we
        // resolve its outstanding claim immediately; false means it's
        // already dequeued and stays counted until flush() itself resolves
        // it (see flush()).
        if (timer_.cancel(it->second.handle)) --outstandingFlushes_;
        it->second.event = event;
    } else {
        it = pending_.emplace(id, Pending{.event = event, .handle = 0}).first;
    }
    // Claim this schedule's outstanding status right here, in the same
    // critical section as the schedule() call below — not from inside
    // flush() — so stop()'s barrier can never observe zero outstanding work
    // while this flush is dequeued-but-not-yet-run. See the class doc
    // comment.
    ++outstandingFlushes_;
    it->second.handle = timer_.schedule(window_, [this, id] { flush(id); });
}

void HotplugService::flush(const std::string& id) {
    std::optional<pal::HotplugEvent> event;
    {
        std::scoped_lock lock(mutex_);
        auto it = pending_.find(id);
        if (it != pending_.end()) {
            event = std::move(it->second.event);
            pending_.erase(it);
        }
        // Else: cancelled/superseded — a faster flush() for this id (or
        // stop()) already erased the pending_ entry (see onEvent()'s comment
        // above). Either way this schedule must still resolve exactly once,
        // below.
    }
    if (event) service_.applyDelta(*event);  // cheap, non-blocking; publishes on this timer thread
    {
        std::scoped_lock lock(mutex_);
        // This schedule is now resolved regardless of which branch above ran
        // — it has definitely finished running, including any applyDelta()
        // call above (which touches this->service_). This is the only place
        // a false-cancel()'d schedule (dequeued by the worker thread but not
        // yet run when onEvent()'s reschedule or stop() raced it) ever gets
        // resolved, which is exactly what makes stop()'s flushDone_.wait()
        // safe to block on: it cannot return while any dequeued-but-not-yet-
        // run flush is still touching `this`.
        --outstandingFlushes_;
    }
    flushDone_.notify_all();
}

}  // namespace devmgr::app
