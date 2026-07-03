#pragma once
#include <string>

#include "devmgr/pal/criticality.hpp"

namespace devmgr::platform_linux {

// Gathers CriticalityFacts from /proc/self/mounts + sysfs (spec 2026-07-03):
// - root/boot backing devices: mount source → /dev realpath → basename →
//   <sysfsRoot>/class/block/<name>, expanded recursively through slaves/
//   (dm/LUKS/RAID) down to physical leaves; recorded as canonical paths.
// - keyboards/pointers: <sysfsRoot>/class/input/input*, classified from
//   capabilities/key (KEY_Q..KEY_P all present → keyboard) and
//   capabilities/rel (REL_X+REL_Y → pointer).
// Non-/dev mount sources (tmpfs, network, zfs pools) are skipped — documented
// residual: such roots yield no storage facts. Probe fresh per check.
class LinuxCriticalityProber final : public pal::ICriticalityProber {
   public:
    explicit LinuxCriticalityProber(std::string sysfsRoot = "/sys",
                                    std::string mountsPath = "/proc/self/mounts");
    core::Result<pal::CriticalityFacts> probe() override;

   private:
    std::string sysfsRoot_;
    std::string mountsPath_;
};

}  // namespace devmgr::platform_linux
