#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/criticality.hpp"

namespace devmgr::services {

struct GuardVerdict {
    bool allowed = true;
    std::string reason;  // set when refused
};

// Pure policy (Phase 4 spec): may the device at targetSysfsPath be DISABLED?
// Refuses when the target subtree contains a root/boot backing device or the
// sole remaining keyboard/pointer. Used authoritatively by devmgrd and
// advisorily by the frontends — same function, one behavior.
GuardVerdict evaluateDisable(const pal::CriticalityFacts& facts,
                             const std::string& targetSysfsPath);

struct ModuleUnloadFacts {
    std::vector<std::string> affectedDevicePaths;  // canonical sysfs paths bound via the module
    std::vector<std::string> holders;              // dependent modules (/sys/module/<m>/holders)
    long refCount = 0;
};

// Pure policy (spec §4.3, order matters): holders → refcount → per-device
// evaluateDisable. Authoritative in devmgrd, advisory in the frontends.
GuardVerdict evaluateModuleUnload(const pal::CriticalityFacts& facts,
                                  const ModuleUnloadFacts& module);

}  // namespace devmgr::services
