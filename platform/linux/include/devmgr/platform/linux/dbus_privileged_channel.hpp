#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// System-bus client of devmgrd (spec §6.1). Every call opens a fresh
// connection (Phase 4 pattern). Versioned verbs check ApiVersion >= their
// minimum once and cache it (v2: modules/drivers/list, v3: snapshots);
// Phase 4 SetDeviceEnabled keeps working against an old daemon.
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
    core::Result<std::vector<core::SnapshotMeta>> snapshotList() override;
    core::Result<std::string> snapshotCreate(const std::string& label) override;
    core::Result<core::RestoreOutcome> snapshotRestore(const std::string& id) override;
    core::Result<void> snapshotDelete(const std::string& id) override;

   private:
    core::Result<void> ensureApi(std::uint32_t minVersion);
    Bus bus_;
    std::mutex cacheMutex_;
    std::optional<std::uint32_t> cachedApi_;
};

}  // namespace devmgr::platform_linux
