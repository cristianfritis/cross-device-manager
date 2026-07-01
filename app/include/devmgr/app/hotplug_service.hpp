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
// stop() (also run by the destructor) blocks until every flush() that the
// DelayedScheduler has already dequeued — and that DelayedScheduler::cancel()
// can therefore no longer reach, since cancel() is best-effort and cannot
// interrupt an in-flight callback — has finished running. This completion
// barrier is implemented entirely inside HotplugService (inFlightFlushes_ +
// flushDone_), not inside DelayedScheduler: without it, stop() could return
// (letting a caller destroy this HotplugService) while flush() was still
// mid-execution on the timer thread, about to read `this->service_` — a
// use-after-free of HotplugService's OWN storage, not merely a lifetime issue
// with the referenced DeviceService/DelayedScheduler. flush() never calls
// stop(), so stop()'s wait for in-flight flushes to drain is an ordinary
// cross-thread wait (timer thread -> whatever thread owns this HotplugService),
// never a self-wait/self-join.
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
    // cancels every pending debounce timer, and blocks until any flush()
    // invocation already in flight on the timer thread has finished — see the
    // class doc comment above for why that barrier is required. Idempotent:
    // safe to call more than once, and safe even if start() was never called.
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
    std::condition_variable flushDone_;  // signaled when inFlightFlushes_ reaches 0
    std::unordered_map<std::string, Pending> pending_;
    int inFlightFlushes_ = 0;  // guarded by mutex_; see stop()/flush()
};

}  // namespace devmgr::app
