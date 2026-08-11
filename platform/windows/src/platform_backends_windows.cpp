#include <memory>
#include <optional>
#include <string>

#include "cfgmgr_notification_source.hpp"
#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/pal/refusing_backends.hpp"
#include "devmgr/platform/windows/cfgmgr_device_enumerator.hpp"
#include "devmgr/platform/windows/windows_device_mapper.hpp"
#include "devmgr/platform/windows/windows_hotplug_monitor.hpp"
#include "devmgr/platform/windows/windows_system_info.hpp"

namespace devmgr::pal {
namespace {

// A hotplug arrival carries the same model an enumeration would produce, read
// through the same path — so a device that appeared and one that was scanned
// are indistinguishable downstream.
std::optional<core::Device> resolveDevice(const std::string& instanceId) {
    const auto facts = platform_windows::readDevnodeFacts(instanceId);
    if (!facts) return std::nullopt;
    return platform_windows::mapDevice(*facts);
}

// The Windows definition of the one factory declared in core. Linking this file
// into a binary IS the platform selection.
//
// READ-ONLY BY CONSTRUCTION. Three interfaces are implemented; the other four
// are the shared refusing implementations, so there is no Windows code path
// that can enable, disable, bind, unbind, load, unload, install or snapshot —
// such a call returns Unsupported without contacting the operating system. That
// is not a policy this class checks at runtime, it is which objects exist.
//
// Criticality probing is deliberately absent, which means every Windows device
// classifies at the lowest tier and the shared safety guard can refuse nothing.
// No device-mutating capability may be enabled on Windows until a real
// criticality prober exists here (windows-device-inventory spec) — otherwise a
// user could disable the only keyboard attached to the machine.
class WindowsPlatformBackends final : public PlatformBackends {
   public:
    WindowsPlatformBackends()
        : monitor_(source_, &resolveDevice),
          set_{.enumerator = enumerator_,
               .hotplug = monitor_,
               .controller = refusingDeviceController(),
               .drivers = refusingDriverManager(),
               .privileged = refusingPrivilegedChannel(),
               .systemInfo = sysinfo_,
               .criticality = refusingCriticalityProber(),
               .updateProviders = {}} {}

    [[nodiscard]] const BackendSet& backends() const override { return set_; }

    [[nodiscard]] PlatformCapabilities capabilities() const override {
        return PlatformCapabilities{.deviceEnumeration = true,
                                    .hotplug = true,
                                    // No load/unload counterpart to modprobe,
                                    // so driver facts arrive as device
                                    // properties instead (design D7).
                                    .deviceControl = false,
                                    .driverManagement = false,
                                    // No helper to talk to and nothing to ask
                                    // it for: every verb behind it is a
                                    // mutation this platform does not have.
                                    .privilegedChannel = false,
                                    .updateProviders = false,
                                    .criticalityProbing = false,
                                    .systemInfo = true};
    }

   private:
    // DECLARATION ORDER IS THE TEARDOWN CONTRACT — members are destroyed in
    // reverse. The notification source is declared BEFORE the monitor that
    // references it, so the monitor's destructor (which stops delivery through
    // that reference) runs while the source is still alive. This is the same
    // hazard the Linux monitor's Phase 2 use-after-free came from.
    platform_windows::CfgMgrDeviceEnumerator enumerator_;
    platform_windows::CfgMgrNotificationSource source_;
    platform_windows::WindowsHotplugMonitor monitor_;
    platform_windows::WindowsSystemInfo sysinfo_;
    BackendSet set_;
};

}  // namespace

core::Result<std::unique_ptr<PlatformBackends>> PlatformBackends::create(
    const BackendOptions& options) {
    // Nothing here needs an event bus or an endpoint choice: the backends that
    // publish diagnostics or connect to a helper are exactly the ones this
    // platform does not implement. Absence is reported through capabilities(),
    // never as a construction failure.
    (void)options;
    return std::unique_ptr<PlatformBackends>(new WindowsPlatformBackends());
}

}  // namespace devmgr::pal
