#pragma once
#include "devmgr/core/models.hpp"

namespace devmgr::pal {

struct HotplugEvent {
    enum class Action { Added, Removed, Changed };
    Action action;
    core::Device device;
};

}  // namespace devmgr::pal
