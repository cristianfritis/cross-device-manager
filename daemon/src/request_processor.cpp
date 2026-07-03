#include "devmgr/daemon/request_processor.hpp"

#include <filesystem>
#include <utility>

#include "devmgr/services/critical_device_guard.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

RequestProcessor::RequestProcessor(pal::IDeviceController& controller,
                                   pal::ICriticalityProber& prober, IAuthority& authority,
                                   std::string sysfsRoot)
    : controller_(controller),
      prober_(prober),
      authority_(authority),
      sysfsRoot_(std::move(sysfsRoot)) {}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::setDeviceEnabled(const CallerId& caller,
                                                      const std::string& sysfsPath, bool enabled) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    const auto rel = canonical.lexically_relative(root);
    if (ec || rel.empty() || rel.native().starts_with(".."))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);

    if (!enabled) {
        auto facts = prober_.probe();
        if (!facts) return tl::unexpected(facts.error());
        const auto verdict = services::evaluateDisable(*facts, canonical.string());
        if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    }

    auto authorized = authority_.checkAuthorized(caller, kActionSetDeviceEnabled);
    if (!authorized) return tl::unexpected(authorized.error());
    if (!*authorized) return core::makeError(core::Error::Code::Permission, "authorization denied");

    return controller_.setEnabled(canonical.string(), enabled);
}

}  // namespace devmgr::daemon
