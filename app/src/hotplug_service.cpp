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
    for (auto& [id, pending] : pending_) timer_.cancel(pending.handle);
    pending_.clear();
    // Wait for any flush() the scheduler had already dequeued (and which the
    // cancel() calls above therefore could not reach) to finish running before
    // returning — see the class doc comment for why this barrier is required.
    flushDone_.wait(lock, [this] { return inFlightFlushes_ == 0; });
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
        // timing is approximate under adversarial scheduler racing.
        timer_.cancel(it->second.handle);  // reset this device's trailing window
        it->second.event = event;
    } else {
        it = pending_.emplace(id, Pending{.event = event, .handle = 0}).first;
    }
    it->second.handle = timer_.schedule(window_, [this, id] { flush(id); });
}

void HotplugService::flush(const std::string& id) {
    std::optional<pal::HotplugEvent> event;
    {
        std::scoped_lock lock(mutex_);
        auto it = pending_.find(id);
        if (it == pending_.end()) return;  // cancelled/superseded
        event = it->second.event;
        pending_.erase(it);
        // Marked in-flight in the same critical section as the pending_ erase
        // above so stop()'s locked cancel-and-clear pass and this increment are
        // strictly ordered by mutex_: whichever runs first, stop() is
        // guaranteed to observe this flush as either absent from pending_ (not
        // yet started) or counted in inFlightFlushes_ (already started) —
        // never neither, which is what would let stop() return early.
        ++inFlightFlushes_;
    }
    service_.applyDelta(*event);  // cheap, non-blocking; publishes on this timer thread
    {
        std::scoped_lock lock(mutex_);
        --inFlightFlushes_;
    }
    flushDone_.notify_all();
}

}  // namespace devmgr::app
