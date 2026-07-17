#pragma once
#include <nlohmann/json.hpp>

#include "devmgr/core/models.hpp"

namespace devmgr::daemon {

// DisabledDeviceEntry <-> JSON, the state.json v1 wire shape. Shared by
// StateStore and JsonSnapshotStore so snapshot payload entries serialize
// byte-identically to state.json entries (one shape, one parser).
// entryFromJson throws nlohmann::json::exception on missing/mistyped keys;
// callers own the quarantine decision.
nlohmann::json entryToJson(const core::DisabledDeviceEntry& e);
core::DisabledDeviceEntry entryFromJson(const nlohmann::json& j);

}  // namespace devmgr::daemon
