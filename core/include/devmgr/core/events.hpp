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
struct ModulesChangedEvent {};
struct UpdatesChangedEvent {};      // provider-side change (fwupd signals) → coalesced refresh
struct UpdatesRefreshedEvent {};    // facade snapshot replaced → VMs rebuild via dispatcher
struct SnapshotsChangedEvent {};    // snapshot create/restore/delete completed → coalesced refresh
struct SnapshotsRefreshedEvent {};  // facade snapshot list replaced → VMs rebuild via dispatcher
struct UpdateRequestEvent {  // fwupd DeviceRequest: durable until dismissed/resolved (spec §9)
    std::string providerId;
    std::string deviceId;
    std::string kind;  // "immediate" | "post" | raw fwupd request-kind number
    std::string message;
};

}  // namespace devmgr::core
