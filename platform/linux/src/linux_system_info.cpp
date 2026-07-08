#include "devmgr/platform/linux/linux_system_info.hpp"

#include <sys/utsname.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

std::string readLockdownMode(const std::string& securityDir) {
    std::ifstream in(fs::path(securityDir) / "lockdown");
    if (!in) return "none";
    std::string content;
    std::getline(in, content);
    const auto open = content.find('[');
    const auto close = content.find(']');
    if (open == std::string::npos || close == std::string::npos || close <= open) return "none";
    return content.substr(open + 1, close - open - 1);
}

bool readSecureBoot(const std::string& efivarsDir) {
    constexpr std::size_t kEfiVarHeaderSize = 4U;
    constexpr std::size_t kEfiVarSize = 5U;
    constexpr int kSecureBootEnabled = 1;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(efivarsDir, ec)) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("SecureBoot-")) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::array<char, kEfiVarSize> bytes{};
        in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return in.gcount() == static_cast<std::streamsize>(kEfiVarSize) &&
               bytes[kEfiVarHeaderSize] == kSecureBootEnabled;
    }
    return false;  // no efivars / no variable => BIOS boot or SB unsupported
}

std::string readPrettyName(const std::string& osReleasePath) {
    constexpr std::string_view kPrettyNameKey = "PRETTY_NAME=";
    std::ifstream in(osReleasePath);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.starts_with(kPrettyNameKey)) continue;
        std::string value = line.substr(kPrettyNameKey.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

LinuxSystemInfo::LinuxSystemInfo(Paths paths) : paths_(std::move(paths)) {}

core::Result<LinuxSystemInfo::Info> LinuxSystemInfo::query() {
    Info info;
    info.osVersion = readPrettyName(paths_.osRelease);
    utsname uts{};
    if (::uname(&uts) == 0) info.kernelVersion = std::string(static_cast<const char*>(uts.release));
    info.secureBoot = readSecureBoot(paths_.efivarsDir);
    info.lockdownMode = readLockdownMode(paths_.securityDir);
    info.rebootPending = false;  // Phase 6 owns update-driven reboot logic
    return info;
}

}  // namespace devmgr::platform_linux
