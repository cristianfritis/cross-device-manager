#pragma once
#include <filesystem>
#include <optional>
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Acts on canonical sysfs paths under an injectable root (tests use tmp trees).
// Mechanism selection is attribute-driven: `authorized` present => USB
// authorized mechanism; otherwise driver unbind + driver_override/drivers_probe
// rebind (spec §5.4).
class SysfsDeviceController final : public pal::IDeviceController {
   public:
    explicit SysfsDeviceController(std::string sysfsRoot = "/sys");
    core::Result<std::optional<std::string>> setEnabled(
        const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) override;
    core::Result<void> bindDriver(const std::string& sysfsPath,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const std::string& sysfsPath) override;

   private:
    core::Result<std::filesystem::path> canonicalDevice(const std::string& sysfsPath) const;
    std::string sysfsRoot_;
};

}  // namespace devmgr::platform_linux
