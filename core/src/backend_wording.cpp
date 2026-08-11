#include "devmgr/core/backend_wording.hpp"

namespace devmgr::core {

UnavailabilityKind kindFor(Error::Code code) {
    switch (code) {
        case Error::Code::NotFound:
            return UnavailabilityKind::Absent;
        case Error::Code::Permission:
            return UnavailabilityKind::NotPermitted;
        case Error::Code::Unsupported:
            return UnavailabilityKind::Unsupported;
        case Error::Code::Io:
        case Error::Code::Network:
        case Error::Code::Busy:
        case Error::Code::Conflict:
        case Error::Code::InvalidArgs:
            break;
    }
    // Not an availability code in its own right: something answered and did not
    // serve, which is what "unreachable" says to a user. Conservative, too — it
    // warns instead of reading as an optional service that was never installed.
    return UnavailabilityKind::Unreachable;
}

std::string_view backendName(BackendId backend) {
    switch (backend) {
        case BackendId::Devmgrd:
            return "Device service";
        case BackendId::Fwupd:
            return "Firmware updates";
        case BackendId::Dkms:
            return "DKMS status";
        case BackendId::Snapshots:
            return "Snapshots";
    }
    return "This information";
}

// An `unsupported` sentence describes the platform, so it must stay true on any
// platform that later lacks the same backend: it invites no action, names no
// installable package, and names no platform mechanism (backend-availability
// spec, "Unsupported wording promises nothing"). Contrast the `absent` rows,
// which are the only ones that may mention installation — DKMS absent says
// "not installed", DKMS unsupported does not.
std::string unavailabilityText(BackendId backend, UnavailabilityKind kind) {
    switch (backend) {
        case BackendId::Devmgrd:
            if (kind == UnavailabilityKind::Unreachable)
                return "Device service unavailable — showing read-only system state.";
            if (kind == UnavailabilityKind::Unsupported)
                return "Device management is not available on this platform — showing read-only "
                       "system state.";
            break;
        case BackendId::Fwupd:
            if (kind == UnavailabilityKind::Unreachable)
                return "Firmware updates unavailable — the fwupd service is not responding.";
            if (kind == UnavailabilityKind::Unsupported)
                return "Firmware updates are not available on this platform.";
            break;
        case BackendId::Dkms:
            if (kind == UnavailabilityKind::Absent)
                return "DKMS status unavailable — DKMS is not installed on this system.";
            if (kind == UnavailabilityKind::Unsupported)
                return "Driver module status is not available on this platform.";
            break;
        case BackendId::Snapshots:
            if (kind == UnavailabilityKind::Unsupported)
                return "Snapshots are not available on this platform.";
            break;
    }
    // Generic fallback: names the backend, states the consequence, substitutes
    // no diagnostic. A new backend or a newly reachable kind therefore degrades
    // to a calm sentence rather than to an empty region — and adding a specific
    // sentence for it is a row in this table, never a branch in a frontend.
    return std::string(backendName(backend)) + " unavailable — this cannot be read right now.";
}

std::optional<BackendId> backendForProvider(std::string_view providerId) {
    if (providerId == "fwupd") return BackendId::Fwupd;
    if (providerId == "dkms") return BackendId::Dkms;
    return std::nullopt;
}

}  // namespace devmgr::core
