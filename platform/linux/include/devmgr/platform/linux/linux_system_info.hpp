#pragma once
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Parsers exposed for tests + shared use (KmodDriverManager reads lockdown).
std::string readLockdownMode(const std::string& securityDir);  // "none" if absent
bool readSecureBoot(const std::string& efivarsDir);            // false if absent (BIOS)
std::string readPrettyName(const std::string& osReleasePath);

class LinuxSystemInfo final : public pal::ISystemInfo {
   public:
    struct Paths {
        std::string osRelease = "/etc/os-release";
        std::string efivarsDir = "/sys/firmware/efi/efivars";
        std::string securityDir = "/sys/kernel/security";
    };
    LinuxSystemInfo() : LinuxSystemInfo(Paths()) {}
    explicit LinuxSystemInfo(Paths paths);
    core::Result<Info> query() override;  // rebootPending stays false (Phase 6)

   private:
    Paths paths_;
};

}  // namespace devmgr::platform_linux
