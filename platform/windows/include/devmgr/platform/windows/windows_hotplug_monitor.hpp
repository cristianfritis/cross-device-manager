#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "devmgr/core/models.hpp"
#include "devmgr/pal/interfaces.hpp"

// PLATFORM-NEUTRAL. The lifecycle rules below — when a callback may fire, what
// stopping guarantees, what a callback may not do — are the part of hotplug
// that shipped a use-after-free on Linux in Phase 2, so they live here, in code
// that compiles and is unit-tested against a fake source on any host. The Win32
// registration is behind INotificationSource, in cfgmgr_notification_source.cpp.
namespace devmgr::platform_windows {

// One device-arrival or device-removal notification, as the operating system
// reports it: an action and the affected device's instance identifier. The
// identifier is the same string the enumerator writes into Device::nativeId, so
// the two halves of the application agree on what a device is.
struct NativeNotification {
    enum class Action { Arrived, Removed };
    Action action = Action::Arrived;
    std::string instanceId;
};

// The operating-system half of hotplug.
class INotificationSource {
   public:
    using Handler = std::function<void(const NativeNotification&)>;
    virtual ~INotificationSource() = default;
    INotificationSource() = default;
    INotificationSource(const INotificationSource&) = delete;
    INotificationSource& operator=(const INotificationSource&) = delete;
    INotificationSource(INotificationSource&&) = delete;
    INotificationSource& operator=(INotificationSource&&) = delete;

    // Begins delivery. The handler runs on arbitrary threads the source owns,
    // possibly several at once.
    virtual core::Result<void> beginDelivery(Handler handler) = 0;
    // MUST block until no handler invocation is in flight, and MUST guarantee
    // none fires afterwards. MUST NOT be called from inside a handler: the
    // native unregistration waits for that very handler to return, so calling
    // it from there deadlocks. WindowsHotplugMonitor::stop() enforces that
    // rather than trusting it.
    virtual void endDelivery() = 0;
};

// Resolves a device instance identifier to the full model, so an arrival
// carries exactly what an enumeration would have produced. Returns nullopt when
// the device node is no longer present — a device that arrived and left again
// before it could be read is not something to report half of.
using DeviceResolver = std::function<std::optional<core::Device>(const std::string&)>;

// Satisfies pal::IHotplugMonitor's shutdown contract (interfaces.hpp:29-31):
// stop() blocks until no callback is executing and no callback fires after it
// returns.
//
// stop() MUST NOT be called from inside the callback. Doing so would deadlock
// in the native unregistration, so this class detects a reentrant call and
// refuses it: the monitor stays started and the next external stop() — the
// destructor, at the latest — performs the real shutdown. That makes the
// invariant enforced rather than remembered. The paths reachable from the
// callback today (HotplugService::onEvent → DelayedScheduler → applyDelta →
// EventBus) initiate no shutdown; the guard is what keeps that true.
//
// start()/stop() assume a single owner driving the lifecycle sequentially, the
// same assumption the Linux monitor makes and the composition root satisfies.
class WindowsHotplugMonitor final : public pal::IHotplugMonitor {
   public:
    WindowsHotplugMonitor(INotificationSource& source, DeviceResolver resolver);
    ~WindowsHotplugMonitor() override;
    WindowsHotplugMonitor(const WindowsHotplugMonitor&) = delete;
    WindowsHotplugMonitor& operator=(const WindowsHotplugMonitor&) = delete;
    WindowsHotplugMonitor(WindowsHotplugMonitor&&) = delete;
    WindowsHotplugMonitor& operator=(WindowsHotplugMonitor&&) = delete;

    core::Result<void> start(Callback callback) override;
    void stop() override;

   private:
    void onNative(const NativeNotification& notification);

    INotificationSource& source_;
    DeviceResolver resolver_;

    mutable std::mutex mutex_;
    Callback callback_;
    bool started_ = false;
    // Bumped on every start. A callback that entered before shutdown and
    // reaches the delivery point after a RESTART would otherwise be delivered
    // to the new generation's callback, which never asked for it.
    std::uint64_t generation_ = 0;
    // Threads currently inside a callback, so a reentrant stop() is recognised
    // as such instead of deadlocking the process.
    std::set<std::thread::id> callbackThreads_;
};

}  // namespace devmgr::platform_windows
