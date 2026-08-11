#pragma once
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_windows {

// Reports what it can verify and nothing else.
//
// Secure Boot is `nullopt` when the firmware state cannot be read, never
// `false`: "off" is a claim that unsigned code loads, and a failed read is not
// evidence for it. Fields describing Linux-only concepts (lockdown mode) keep
// their documented neutral value rather than having a Windows notion
// substituted into them.
//
// This header pulls no Windows header, the same rule the other backend headers
// follow.
class WindowsSystemInfo final : public pal::ISystemInfo {
   public:
    core::Result<Info> query() override;
};

}  // namespace devmgr::platform_windows
