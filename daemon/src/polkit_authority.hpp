#pragma once
#include <string>

#include "devmgr/daemon/authority.hpp"

namespace devmgr::daemon {

// IAuthority over polkit's own D-Bus API (CheckAuthorization with
// AllowUserInteraction) — no libpolkit link. Opens a fresh short-lived
// system-bus connection per check: the daemon's main connection is busy
// dispatching the method call that triggered us, and sdbus-c++ v2 sync calls
// block their connection.
class PolkitAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId& caller,
                                       const std::string& actionId) override;
};

}  // namespace devmgr::daemon
