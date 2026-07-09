#include "devmgr/app/disabled_overlay.hpp"

#include "devmgr/services/device_key.hpp"

namespace devmgr::app {

void applyDisabledOverlay(std::vector<core::Device>& devices,
                          const std::vector<core::DisabledDeviceEntry>& entries) {
    for (auto& device : devices) {
        for (const auto& entry : entries) {
            if (services::matchesDevice(entry.key, device) ||
                entry.lastSysfsPath == device.sysfsPath) {
                device.status = core::DeviceStatus::Disabled;
                if (entry.guardSuspended)
                    device.errorNote = "disabled — enforcement suspended (guard refused re-apply)";
                break;
            }
        }
    }
}

}  // namespace devmgr::app
