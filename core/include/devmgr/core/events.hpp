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

}  // namespace devmgr::core
