#include "devmgr/core/snapshot_models.hpp"

namespace devmgr::core {

const char* to_string(SnapshotTrigger trigger) {
    switch (trigger) {
        case SnapshotTrigger::Manual:
            return "manual";
        case SnapshotTrigger::Auto:
            return "auto";
    }
    return "auto";
}

std::optional<SnapshotTrigger> snapshotTriggerFrom(std::string_view text) {
    if (text == "manual") return SnapshotTrigger::Manual;
    if (text == "auto") return SnapshotTrigger::Auto;
    return std::nullopt;
}

const char* to_string(SnapshotHealth health) {
    switch (health) {
        case SnapshotHealth::Ok:
            return "ok";
        case SnapshotHealth::Corrupt:
            return "corrupt";
        case SnapshotHealth::Unsupported:
            return "unsupported";
    }
    return "ok";
}

}  // namespace devmgr::core
