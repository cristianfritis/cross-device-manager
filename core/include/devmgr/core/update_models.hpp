#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::core {

// Spec §4.1. Disposition of a completed install() call — success has shapes:
// an offline/scheduled update reports success with NO immediate version bump.
enum class InstallDisposition { Completed, Scheduled, NeedsReboot, NeedsUserAction };

struct InstallOutcome {
    InstallDisposition disposition = InstallDisposition::Completed;
    bool needsReboot = false;
    std::optional<std::string> observedVersion;  // absent for Scheduled/offline
    std::string message;
};

// Stable release identity across metadata refreshes (spec §2: never select or
// match releases by version-string comparison).
struct ReleaseRef {
    std::string remoteId;
    std::string checksum;
    bool operator==(const ReleaseRef&) const = default;
};

struct DeviceUpdateFacts {  // device facts ONLY — no transient/app state here
    bool updatable = false;
    bool supported = false;
    bool needsRebootAfterUpdate = false;
};

struct ReleaseInfo {
    std::string version;
    std::string summary;
    std::string remoteId;
    std::string checksum;
    std::vector<std::string> locations;  // fwupd "Locations", legacy "Uri" folded in
    bool localCab = false;               // resolvable per spec §5.3 ⇒ install verb enabled
    std::uint64_t sizeBytes = 0;
    bool isUpgrade = false;
    std::optional<std::uint32_t> installDurationSec;
    ReleaseRef ref() const { return {remoteId, checksum}; }
};

struct UpdateCandidate {
    std::string providerId;  // "fwupd" | "dkms"
    std::string id;          // fwupd DeviceId | "dkms:<module>/<version>"
    std::string displayName;
    std::string currentVersion;
    std::optional<std::string> candidateVersion;  // = releases.front().version (fwupd order)
    DeviceUpdateFacts facts;
    std::vector<ReleaseInfo> releases;  // empty for dkms
    std::vector<std::pair<std::string, std::string>> details;
};

struct ProviderAvailability {
    bool available = false;
    std::optional<std::string> version;  // e.g. fwupd DaemonVersion
    std::optional<Error> error;          // machine state; UI renders its message
    std::vector<std::string> notices;    // e.g. metadata-age hints
};

struct UpdateProviderState {
    std::string providerId;
    ProviderAvailability availability;
    std::vector<UpdateCandidate> candidates;
    std::optional<Error> refreshError;  // enumerate failed (availability may be true)
};

// Durable pending/reboot record (spec §8.2 / M1): NEVER derived from the live
// candidate list; fed by install outcomes + provider pendingActions().
struct PendingAction {
    std::string providerId;
    std::string deviceId;
    std::string deviceName;
    InstallDisposition disposition = InstallDisposition::Completed;
    std::string version;
};

}  // namespace devmgr::core
