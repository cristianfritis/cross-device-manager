#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

inline constexpr const char* kActionSetDeviceEnabled = "org.devmgr.set-device-enabled";
inline constexpr const char* kActionManageModules = "org.devmgr.manage-modules";
inline constexpr const char* kActionManageDrivers = "org.devmgr.manage-drivers";

// validate → guard → authorize → act (unchanged, spec §6.2). New in Phase 5:
// SetDeviceEnabled persists desired state (StateStore); Bind/UnbindDriver are
// surgical and NEVER touch the store; module names are charset-validated; the
// apply mutex serializes every controller/store ACTION with EnforcementService.
class RequestProcessor {
   public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — adjacent string params
    RequestProcessor(pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                     IAuthority& authority, pal::IDriverManager& drivers,
                     pal::IDeviceEnumerator& enumerator, StateStore& store, std::mutex& applyMutex,
                     std::string sysfsRoot = "/sys");

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> setDeviceEnabled(const CallerId& caller, const std::string& sysfsPath,
                                        bool enabled);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> loadModule(const CallerId& caller, const std::string& name);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> unloadModule(const CallerId& caller, const std::string& name);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId + three string params
    core::Result<void> bindDriver(const CallerId& caller, const std::string& sysfsPath,
                                  const std::string& driverName);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> unbindDriver(const CallerId& caller, const std::string& sysfsPath);
    std::vector<core::DisabledDeviceEntry> listDisabledDevices() const;  // read-only, no auth

   private:
    core::Result<std::string> canonicalContained(const std::string& sysfsPath) const;
    core::Result<void> authorize(const CallerId& caller, const char* action);
    core::Result<void> applyDisable(const std::string& canonical);
    core::Result<void> applyEnable(const std::string& canonical);

    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    IAuthority& authority_;
    pal::IDriverManager& drivers_;
    pal::IDeviceEnumerator& enumerator_;
    StateStore& store_;
    std::mutex& applyMutex_;
    std::string sysfsRoot_;
};

}  // namespace devmgr::daemon
