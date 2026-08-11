#include "devmgr/platform/windows/windows_hotplug_monitor.hpp"

#include <utility>

#include "devmgr/platform/windows/windows_device_mapper.hpp"

namespace devmgr::platform_windows {
namespace {

// Marks the calling thread as being inside a callback for as long as it lives,
// so a stop() made from that thread is recognised as reentrant. Exception-safe
// by construction: the mark is removed even if the callback throws.
class CallbackThreadMark {
   public:
    CallbackThreadMark(std::mutex& mutex, std::set<std::thread::id>& threads)
        : mutex_(mutex), threads_(threads) {
        const std::scoped_lock lock(mutex_);
        threads_.insert(std::this_thread::get_id());
    }
    ~CallbackThreadMark() {
        const std::scoped_lock lock(mutex_);
        threads_.erase(std::this_thread::get_id());
    }
    CallbackThreadMark(const CallbackThreadMark&) = delete;
    CallbackThreadMark& operator=(const CallbackThreadMark&) = delete;
    CallbackThreadMark(CallbackThreadMark&&) = delete;
    CallbackThreadMark& operator=(CallbackThreadMark&&) = delete;

   private:
    std::mutex& mutex_;
    std::set<std::thread::id>& threads_;
};

}  // namespace

WindowsHotplugMonitor::WindowsHotplugMonitor(INotificationSource& source, DeviceResolver resolver)
    : source_(source), resolver_(std::move(resolver)) {}

WindowsHotplugMonitor::~WindowsHotplugMonitor() {
    stop();
}

core::Result<void> WindowsHotplugMonitor::start(Callback callback) {
    {
        const std::scoped_lock lock(mutex_);
        if (started_)
            return core::makeError(core::Error::Code::Io, "hotplug monitor already started");
        callback_ = std::move(callback);
        ++generation_;
        started_ = true;
    }
    if (auto begun = source_.beginDelivery([this](const NativeNotification& n) { onNative(n); });
        !begun) {
        const std::scoped_lock lock(mutex_);
        started_ = false;  // nothing was registered, so nothing can arrive
        callback_ = {};
        return begun;
    }
    return {};
}

void WindowsHotplugMonitor::stop() {
    {
        const std::scoped_lock lock(mutex_);
        if (!started_) return;  // never started, or a prior stop() already finished
        if (callbackThreads_.contains(std::this_thread::get_id())) {
            // Reentrant. The native unregistration blocks until THIS callback
            // returns, so performing it here would deadlock the process. Leave
            // the monitor started and let the next external stop() — the
            // destructor at the latest — do the work. Silence is deliberate:
            // there is no caller here to report to, and the shutdown still
            // happens, just not on this thread.
            return;
        }
        // From here no further callback is dispatched, even one already in
        // flight: onNative() re-reads these under the lock before delivering.
        started_ = false;
        ++generation_;
        callback_ = {};
    }
    source_.endDelivery();  // blocks until no delivery is in flight
}

void WindowsHotplugMonitor::onNative(const NativeNotification& notification) {
    if (notification.instanceId.empty()) return;  // nothing identifiable to report

    const CallbackThreadMark mark(mutex_, callbackThreads_);

    std::uint64_t generation = 0;
    {
        const std::scoped_lock lock(mutex_);
        if (!started_) return;  // outside a started period — drop it
        generation = generation_;
    }

    const bool removed = notification.action == NativeNotification::Action::Removed;
    core::Device device;
    if (removed) {
        // The device node is gone; the identifier is all there is, and it is
        // all DeviceService needs to drop the row.
        device = mapKnownOnlyByInstanceId(notification.instanceId);
    } else {
        auto resolved = resolver_ ? resolver_(notification.instanceId) : std::nullopt;
        // Arrived and left again before it could be read. Reporting a device
        // with no facts would put a nameless row on screen that no refresh
        // removes; the next enumeration is the honest answer.
        if (!resolved) return;
        device = std::move(*resolved);
    }

    Callback callback;
    {
        const std::scoped_lock lock(mutex_);
        // Re-checked after the (potentially slow) resolution above: a stop or a
        // stop-then-start may have happened while it ran, and this delivery
        // belongs to neither.
        if (!started_ || generation != generation_) return;
        callback = callback_;
    }
    if (!callback) return;
    // Invoked OUTSIDE the lock: the callback runs application code, which must
    // never execute under this object's mutex.
    callback(pal::HotplugEvent{
        .action = removed ? pal::HotplugEvent::Action::Removed : pal::HotplugEvent::Action::Added,
        .device = std::move(device)});
}

}  // namespace devmgr::platform_windows
