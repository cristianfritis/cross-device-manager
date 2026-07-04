#include "daemon/src/polkit_authority.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <tuple>

#include <sdbus-c++/sdbus-c++.h>

namespace devmgr::daemon {

namespace {
// Under the client's 120 s call budget so the daemon answers before the
// caller's own timeout fires.
constexpr std::chrono::seconds kPolkitCallTimeout{110};
constexpr std::uint32_t kAllowUserInteraction = 1;  // may block for ~minutes
}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): CallerId aliases std::string
core::Result<bool> PolkitAuthority::checkAuthorized(const CallerId& caller,
                                                    const std::string& actionId) {
    using Subject = sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>;
    using CheckResult = sdbus::Struct<bool, bool, std::map<std::string, std::string>>;
    try {
        auto connection = sdbus::createSystemBusConnection();
        auto proxy = sdbus::createProxy(std::move(connection),
                                        sdbus::ServiceName{"org.freedesktop.PolicyKit1"},
                                        sdbus::ObjectPath{"/org/freedesktop/PolicyKit1/Authority"});
        const Subject subject{"system-bus-name", {{"name", sdbus::Variant{caller}}}};
        const std::map<std::string, std::string> details;
        CheckResult result;
        proxy->callMethod("CheckAuthorization")
            .onInterface(sdbus::InterfaceName{"org.freedesktop.PolicyKit1.Authority"})
            .withArguments(subject, actionId, details, kAllowUserInteraction, std::string{})
            .withTimeout(kPolkitCallTimeout)
            .storeResultsTo(result);
        return std::get<0>(result);
    } catch (const sdbus::Error& e) {
        return core::makeError(core::Error::Code::Io,
                               "polkit unavailable: " + std::string{e.getMessage()});
    }
}

}  // namespace devmgr::daemon
