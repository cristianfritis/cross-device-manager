#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/core/update_models.hpp"

namespace devmgr::platform_linux::fwupd {

inline constexpr const char* kBusName = "org.freedesktop.fwupd";
inline constexpr const char* kObjectPath = "/";
inline constexpr const char* kInterface = "org.freedesktop.fwupd";

// Values pinned from /usr/include/fwupd-3/libfwupd/fwupd-enums.h (T3 Step 1;
// running DaemonVersion 2.0.20). Confirmed by grep against the installed
// header — do NOT re-derive from an fwupd-2 path, it doesn't exist here.
inline constexpr std::uint64_t kDeviceFlagUpdatable = 1ULL << 1U;
inline constexpr std::uint64_t kDeviceFlagSupported = 1ULL << 5U;
inline constexpr std::uint64_t kDeviceFlagNeedsReboot = 1ULL << 8U;
inline constexpr std::uint64_t kReleaseFlagIsUpgrade = 1ULL << 2U;
inline constexpr std::uint32_t kUpdateStatePending = 1;
inline constexpr std::uint32_t kUpdateStateSuccess = 2;
inline constexpr std::uint32_t kUpdateStateFailed = 3;
inline constexpr std::uint32_t kUpdateStateNeedsReboot = 4;

using Dict = std::map<std::string, sdbus::Variant>;

struct ParsedDevice {
    std::string deviceId;
    std::string name;
    std::string vendor;
    std::string version;
    core::DeviceUpdateFacts facts;
};

// nullopt ⇒ drop row (empty DeviceId / missing Version) — spec §5.1.
std::optional<ParsedDevice> parseDevice(const Dict& dict);
// localCab stays false here; the provider fills it via CabResolver (T4/T6).
std::optional<core::ReleaseInfo> parseRelease(const Dict& dict);
core::Error mapError(const std::string& name, const std::string& message);
bool isNothingToDo(const std::string& name);
core::InstallDisposition dispositionFromUpdateState(std::uint32_t state);
// nullopt unless UpdateState ∈ {Pending, NeedsReboot} — completed/failed history
// rows are not pending actions (failed rows surface via availability() notices).
std::optional<core::PendingAction> parseHistoryEntry(const Dict& dict);
// FwupdStatus (fwupd-enums.h) → fwupd's own kebab-case string form (T8 §5.4
// progress attribution). Unknown/out-of-range values fall back to "unknown"
// rather than throwing — tolerant like every other parse-layer helper here.
const char* statusName(std::uint32_t status);

}  // namespace devmgr::platform_linux::fwupd
