#pragma once
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// IDeviceController over sysfs. Phase 4 mechanism: the USB `authorized`
// attribute only — the one path whose disabled-state round-trips purely
// through sysfs (spec 2026-07-03). Runs in-process in devmgrd (as root);
// the sysfs root is injectable so tests drive a fake tree in a temp dir.
class SysfsDeviceController final : public pal::IDeviceController {
   public:
    explicit SysfsDeviceController(std::string sysfsRoot = "/sys");
    core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) override;

   private:
    std::string sysfsRoot_;
};

}  // namespace devmgr::platform_linux
