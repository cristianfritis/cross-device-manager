#pragma once
#include <string>

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

}  // namespace devmgr::services
