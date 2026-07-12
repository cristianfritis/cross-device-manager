#include "devmgr/platform/linux/fwupd_contract.hpp"

#include <spdlog/spdlog.h>

namespace devmgr::platform_linux::fwupd {
namespace {

// Known key, wrong variant type ⇒ treat as absent + debug log; never throw.
template <typename T>
std::optional<T> get(const Dict& d, const char* key) {
    const auto it = d.find(key);
    if (it == d.end()) return std::nullopt;
    try {
        return it->second.get<T>();
    } catch (const std::exception& e) {
        spdlog::debug("fwupd: key '{}' has unexpected variant type: {}", key, e.what());
        return std::nullopt;
    }
}

}  // namespace

std::optional<ParsedDevice> parseDevice(const Dict& dict) {
    ParsedDevice out;
    out.deviceId = get<std::string>(dict, "DeviceId").value_or("");
    const auto version = get<std::string>(dict, "Version");
    if (out.deviceId.empty() || !version) return std::nullopt;
    out.version = *version;
    out.name = get<std::string>(dict, "Name").value_or("(unnamed device)");
    out.vendor = get<std::string>(dict, "Vendor").value_or("");
    const auto flags = get<std::uint64_t>(dict, "Flags").value_or(0);
    out.facts.updatable = (flags & kDeviceFlagUpdatable) != 0;
    out.facts.supported = (flags & kDeviceFlagSupported) != 0;
    out.facts.needsRebootAfterUpdate = (flags & kDeviceFlagNeedsReboot) != 0;
    return out;
}

std::optional<core::ReleaseInfo> parseRelease(const Dict& dict) {
    core::ReleaseInfo out;
    const auto version = get<std::string>(dict, "Version");
    if (!version) return std::nullopt;
    out.version = *version;
    out.summary = get<std::string>(dict, "Summary").value_or("");
    out.remoteId = get<std::string>(dict, "RemoteId").value_or("");
    out.checksum = get<std::string>(dict, "Checksum").value_or("");
    if (auto locs = get<std::vector<std::string>>(dict, "Locations"); locs && !locs->empty()) {
        out.locations = std::move(*locs);
    } else if (auto uri = get<std::string>(dict, "Uri"); uri && !uri->empty()) {
        out.locations = {std::move(*uri)};  // pre-1.5 daemons (spec §5.1 skew note)
    }
    out.sizeBytes = get<std::uint64_t>(dict, "Size").value_or(0);
    const auto flags = get<std::uint64_t>(dict, "Flags").value_or(0);
    out.isUpgrade = (flags & kReleaseFlagIsUpgrade) != 0;
    if (auto dur = get<std::uint32_t>(dict, "InstallDuration")) out.installDurationSec = dur;
    return out;
}

bool isNothingToDo(const std::string& name) {
    return name == "org.freedesktop.fwupd.NothingToDo";
}

core::Error mapError(const std::string& name, const std::string& message) {
    // Keep name AND message (review: don't flatten early) — "<name>: <message>".
    const auto text = (name.empty() ? std::string{"fwupd"} : name) +
                      (message.empty() ? std::string{": unknown error"} : ": " + message);
    // core::Error is an aggregate {code, message}; core::makeError() returns
    // the tl::unexpected wrapper for Result returns and is NOT usable here —
    // construct core::Error directly (see core/include/devmgr/core/result.hpp).
    if (name == "org.freedesktop.fwupd.AuthFailed")
        return core::Error{.code = core::Error::Code::Permission, .message = text};
    if (name == "org.freedesktop.fwupd.NothingToDo" || name == "org.freedesktop.fwupd.VersionNewer")
        return core::Error{.code = core::Error::Code::Conflict, .message = text};
    if (name == "org.freedesktop.fwupd.NeedsUserAction")
        return core::Error{.code = core::Error::Code::Busy, .message = text};
    return core::Error{.code = core::Error::Code::Io, .message = text};
}

core::InstallDisposition dispositionFromUpdateState(std::uint32_t state) {
    switch (state) {
        case kUpdateStateNeedsReboot:
            return core::InstallDisposition::NeedsReboot;
        case kUpdateStatePending:
            return core::InstallDisposition::Scheduled;
        default:
            return core::InstallDisposition::Completed;
    }
}

std::optional<core::PendingAction> parseHistoryEntry(const Dict& dict) {
    const auto state = get<std::uint32_t>(dict, "UpdateState").value_or(0);
    if (state != kUpdateStatePending && state != kUpdateStateNeedsReboot) return std::nullopt;
    return core::PendingAction{
        .providerId = "fwupd",
        .deviceId = get<std::string>(dict, "DeviceId").value_or(""),
        .deviceName = get<std::string>(dict, "Name").value_or(""),
        .disposition = dispositionFromUpdateState(state),
        .version = get<std::string>(dict, "Version").value_or(""),
    };
}

}  // namespace devmgr::platform_linux::fwupd
