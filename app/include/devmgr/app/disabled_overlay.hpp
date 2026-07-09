#pragma once
#include <vector>
#include "devmgr/core/models.hpp"
namespace devmgr::app {
// Daemon-owned truth merge (spec §6.1/§9.1): a device matching a desired-
// disabled entry renders Disabled — including during the replug flicker while
// it is transiently bound. guardSuspended surfaces via errorNote.
void applyDisabledOverlay(std::vector<core::Device>& devices,
                          const std::vector<core::DisabledDeviceEntry>& entries);
}  // namespace devmgr::app
