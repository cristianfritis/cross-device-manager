#pragma once
#include <string>

#include "devmgr/core/result.hpp"

namespace devmgr::daemon {

// A caller's unique D-Bus bus name (e.g. ":1.42") — the subject polkit
// authorizes. ManagerAdaptor extracts it from the method-call message.
using CallerId = std::string;

// Authorization seam: PolkitAuthority in production, Allow/DenyAll under
// --authority test flags (spec: full pipeline testable without a polkit agent).
class IAuthority {
   public:
    virtual ~IAuthority() = default;
    // True = authorized. Interactive authentication may block for ~minutes.
    virtual core::Result<bool> checkAuthorized(const CallerId& caller,
                                               const std::string& actionId) = 0;
};

class AllowAllAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId&, const std::string&) override {
        return true;
    }
};

class DenyAllAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId&, const std::string&) override {
        return false;
    }
};

}  // namespace devmgr::daemon
