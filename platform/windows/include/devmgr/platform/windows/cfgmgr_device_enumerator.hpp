#pragma once
#include <optional>
#include <string>

#include "devmgr/pal/interfaces.hpp"
#include "devmgr/platform/windows/windows_device_facts.hpp"

namespace devmgr::platform_windows {

// Present device nodes, read through the configuration manager (design D7:
// CfgMgr32, not SetupAPI — no handle lifetime, no message pump, works
// unelevated, and it is the same family the hotplug monitor registers with).
// This header pulls no Windows header, so consumers stay free of the native
// dependency — the same rule the Linux enumerator header follows.
class CfgMgrDeviceEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override;
};

// One present device node's facts, by device instance identifier. The hotplug
// monitor uses it so an arrival carries exactly the model an enumeration would
// have produced. `nullopt` when the node is not present — which is the normal
// outcome for a device that arrived and left again before it could be read.
std::optional<DevnodeFacts> readDevnodeFacts(const std::string& instanceId);

}  // namespace devmgr::platform_windows
