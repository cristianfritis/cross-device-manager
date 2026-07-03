#pragma once
#include <string>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

inline constexpr const char* kActionSetDeviceEnabled = "org.devmgr.set-device-enabled";

// The daemon's verb pipeline: validate → guard → authorize → act (spec
// 2026-07-03). Pure logic, no D-Bus types — ManagerAdaptor translates.
// Trusts nothing from the client: the path is canonicalized and containment-
// checked here, criticality facts are probed fresh per request, and the guard
// runs BEFORE authorization so a refused request never triggers a password
// prompt. The guard applies to disable only; re-enabling skips it.
class RequestProcessor {
   public:
    RequestProcessor(pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                     IAuthority& authority, std::string sysfsRoot = "/sys");

    core::Result<void> setDeviceEnabled(const CallerId& caller, const std::string& sysfsPath,
                                        bool enabled);

   private:
    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    IAuthority& authority_;
    std::string sysfsRoot_;
};

}  // namespace devmgr::daemon
