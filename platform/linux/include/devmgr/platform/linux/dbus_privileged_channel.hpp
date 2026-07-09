#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// System-bus client of devmgrd (spec §6.1). Every call opens a fresh
// connection (Phase 4 pattern). v2 verbs check ApiVersion >= 2 once and cache
// it; Phase 4 SetDeviceEnabled keeps working against an old daemon.
class DbusPrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    enum class Bus { System, Session };
    explicit DbusPrivilegedChannel(Bus bus = Bus::System);

    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override;
    core::Result<void> loadModule(const std::string& name) override;
    core::Result<void> unloadModule(const std::string& name) override;
    core::Result<void> bindDriver(const core::Device& device,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const core::Device& device) override;
    core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() override;

   private:
    core::Result<void> ensureApi2();
    Bus bus_;
    std::mutex cacheMutex_;
    std::optional<std::uint32_t> cachedApi_;
};

}  // namespace devmgr::platform_linux
