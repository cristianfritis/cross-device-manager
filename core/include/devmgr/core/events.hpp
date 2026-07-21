#pragma once
#include <string>
#include "devmgr/core/models.hpp"

namespace devmgr::core {

struct DeviceAddedEvent {
    Device device;
};
struct DeviceRemovedEvent {
    DeviceId id;
};
struct DeviceChangedEvent {
    Device device;
};
struct TaskProgressEvent {
    std::string taskId;
    int percent = 0;
    std::string stage;
};
struct TaskCompletedEvent {
    std::string taskId;
    bool ok = false;
    std::string message;
};
struct ErrorEvent {
    std::string source;
    std::string message;
};
// Comments sit above the declarations, not trailing them: clang-format 18 and 21
// disagree on trailing-comment alignment in this block, and the CI gate runs 18.
struct ModulesChangedEvent {};
// provider-side change (fwupd signals) → coalesced refresh
struct UpdatesChangedEvent {};
// facade snapshot replaced → VMs rebuild via dispatcher
struct UpdatesRefreshedEvent {};
// snapshot create/restore/delete completed → coalesced refresh
struct SnapshotsChangedEvent {};
// facade snapshot list replaced → VMs rebuild via dispatcher
struct SnapshotsRefreshedEvent {};
// facade cached diff replaced → the VM rebuilds its preview/diff lines. Kept
// separate from SnapshotsRefreshedEvent because a diff is fetched on demand
// (opening a preview or a diff view) and must not force a list rebuild.
struct SnapshotDiffRefreshedEvent {};
struct UpdateRequestEvent {  // fwupd DeviceRequest: durable until dismissed/resolved (spec §9)
    std::string providerId;
    std::string deviceId;
    std::string kind;  // "immediate" | "post" | raw fwupd request-kind number
    std::string message;
};

}  // namespace devmgr::core
