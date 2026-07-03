#pragma once
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::pal {

// Inputs to services::evaluateDisable. All entries are canonical sysfs device
// paths (symlinks resolved), so they are prefix-comparable with a target
// device's sysfsPath. Gathered fresh per check — never cached.
struct CriticalityFacts {
    std::vector<std::string> rootBackingPaths;  // devices backing the / filesystem
    std::vector<std::string> bootBackingPaths;  // devices backing /boot (empty if none)
    std::vector<std::string> keyboardPaths;     // live keyboard input devices
    std::vector<std::string> pointerPaths;      // live pointer input devices
};

// Platform contract that gathers CriticalityFacts. Linux impl:
// platform_linux::LinuxCriticalityProber. Injected into devmgrd
// (authoritative) and ApplicationFacade (advisory).
class ICriticalityProber {
   public:
    virtual ~ICriticalityProber() = default;
    virtual core::Result<CriticalityFacts> probe() = 0;
};

}  // namespace devmgr::pal
