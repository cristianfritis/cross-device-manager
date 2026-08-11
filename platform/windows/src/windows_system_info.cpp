#include "devmgr/platform/windows/windows_system_info.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "windows_text.hpp"

namespace devmgr::platform_windows {
namespace {

constexpr const wchar_t* kCurrentVersionKey = LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion)";
// Populated by the firmware on UEFI systems. Its ABSENCE is not evidence that
// Secure Boot is off — it is evidence that the state could not be read, which
// is a different sentence for the user.
constexpr const wchar_t* kSecureBootKey = LR"(SYSTEM\CurrentControlSet\Control\SecureBoot\State)";

std::string registryString(const wchar_t* key, const wchar_t* value) {
    DWORD bytes = 0;
    if (::RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) !=
            ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
    if (::RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buffer.data(),
                       &bytes) != ERROR_SUCCESS) {
        return {};
    }
    while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    return toUtf8(buffer);
}

std::optional<std::uint32_t> registryDword(const wchar_t* key, const wchar_t* value) {
    DWORD data = 0;
    DWORD bytes = sizeof data;
    if (::RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_DWORD, nullptr, &data, &bytes) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(data);
}

// "Windows 11 Pro 23H2" — the product name plus the display version when the
// machine reports one. Product-facing words only; no registry path reaches it.
std::string osVersion() {
    std::string name = registryString(kCurrentVersionKey, L"ProductName");
    const std::string display = registryString(kCurrentVersionKey, L"DisplayVersion");
    if (!display.empty()) name += name.empty() ? display : " " + display;
    return name;
}

// "10.0.22631.3007" — major.minor.build.revision, the form every Windows
// support statement and bug report uses.
std::string buildVersion() {
    const auto major = registryDword(kCurrentVersionKey, L"CurrentMajorVersionNumber");
    const auto minor = registryDword(kCurrentVersionKey, L"CurrentMinorVersionNumber");
    const std::string build = registryString(kCurrentVersionKey, L"CurrentBuildNumber");
    if (!major || build.empty()) return build;  // partial is better than invented
    std::string out =
        std::to_string(*major) + "." + std::to_string(minor.value_or(0)) + "." + build;
    if (const auto revision = registryDword(kCurrentVersionKey, L"UBR"))
        out += "." + std::to_string(*revision);
    return out;
}

}  // namespace

core::Result<WindowsSystemInfo::Info> WindowsSystemInfo::query() {
    Info info;
    info.osVersion = osVersion();
    info.kernelVersion = buildVersion();
    // nullopt when the value is absent or unreadable — see kSecureBootKey.
    if (const auto enabled = registryDword(kSecureBootKey, L"UEFISecureBootEnabled"))
        info.secureBoot = *enabled != 0U;
    // rebootPending stays false here for the same reason it does on Linux:
    // update-driven reboot state is the update providers' to report, and this
    // platform has none. lockdownMode stays "none" because lockdown is a Linux
    // concept and no Windows notion is substituted into it.
    return info;
}

}  // namespace devmgr::platform_windows
