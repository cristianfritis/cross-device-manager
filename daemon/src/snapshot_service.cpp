#include "devmgr/daemon/snapshot_service.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "devmgr/daemon/atomic_file.hpp"
#include "devmgr/daemon/sysfs_device_probe.hpp"
#include "devmgr/services/critical_device_guard.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

namespace {
constexpr const char* kConfigLevelNote =
    "config-level: takes effect on next boot/hotplug; loaded modules are not unloaded";

std::int64_t nowUtc() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A modprobe.d file is devmgr-owned iff it matches devmgr-*.conf. Nothing
// else under the directory is ever captured or rewritten — the snapshot
// engine only touches state devmgr owns.
bool isDevmgrOwned(const std::string& fileName) {
    return fileName.starts_with("devmgr-") && fileName.ends_with(".conf");
}

bool containsKey(const std::vector<core::DisabledDeviceEntry>& entries,
                 const core::DeviceKey& key) {
    return std::ranges::any_of(entries, [&](const auto& e) { return e.key == key; });
}

// Same matching ladder as EnforcementService::sweep: live enumeration by key
// or stored path, then the raw-sysfs presence fallback. Empty => absent.
std::string resolvePathFor(const core::DisabledDeviceEntry& e,
                           const std::vector<core::Device>& all) {
    const auto match = std::ranges::find_if(all, [&](const core::Device& d) {
        return services::matchesDevice(e.key, d) || e.lastSysfsPath == d.sysfsPath;
    });
    if (match != all.end()) return match->sysfsPath;
    std::error_code ec;
    if (!e.lastSysfsPath.empty() && fs::is_directory(e.lastSysfsPath, ec)) return e.lastSysfsPath;
    return {};
}
}  // namespace

SnapshotService::SnapshotService(ISnapshotStore& store, StateStore& state,
                                 pal::IDeviceEnumerator& enumerator,
                                 pal::IDeviceController& controller,
                                 pal::ICriticalityProber& prober, std::string modprobeDir)
    : store_(store),
      state_(state),
      enumerator_(enumerator),
      controller_(controller),
      prober_(prober),
      modprobeDir_(std::move(modprobeDir)) {}

core::SnapshotPayload SnapshotService::capturePayload() const {
    core::SnapshotPayload payload;
    payload.entries = state_.entries();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(modprobeDir_, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const auto name = entry.path().filename().string();
        if (!isDevmgrOwned(name)) continue;
        std::ifstream in(entry.path());
        std::ostringstream content;
        content << in.rdbuf();
        payload.modprobeFiles[name] = content.str();
    }
    return payload;
}

core::Result<std::string> SnapshotService::create(core::SnapshotTrigger trigger,
                                                  const core::SnapshotReason& reason) {
    return store_.put(capturePayload(), trigger, reason, nowUtc());
}

core::Result<std::vector<core::SnapshotMeta>> SnapshotService::list() {
    return store_.list();
}

core::Result<void> SnapshotService::remove(const std::string& id) {
    return store_.remove(id);
}

core::Result<core::RestoreOutcome> SnapshotService::restore(const std::string& id) {
    auto snap = store_.read(id);  // (1) integrity: corrupt/unknown/future all refuse here
    if (!snap) return tl::unexpected(snap.error());

    const core::SnapshotPayload pre = capturePayload();
    // (2) Own safety snapshot BEFORE mutating anything, so restore is undoable.
    auto safety =
        store_.put(pre, core::SnapshotTrigger::Auto,
                   core::SnapshotReason{.verb = "SnapshotRestore", .subject = id}, nowUtc());
    if (!safety) return tl::unexpected(safety.error());

    core::RestoreOutcome outcome;
    outcome.snapshotId = id;
    outcome.safetySnapshotId = *safety;

    // (3) Atomic write-back of everything devmgr owns.
    if (auto replaced = state_.replaceAll(snap->payload.entries); !replaced)
        return tl::unexpected(replaced.error());
    writeBackModprobe(pre, snap->payload, outcome.items);

    // (4) Hardware convergence: entries the restore removed get re-enable
    // attempts; entries now desired get the enforcement re-apply treatment.
    reEnableRemoved(pre, snap->payload, outcome.items);
    reApplyRestored(outcome.items);
    return outcome;
}

void SnapshotService::writeBackModprobe(const core::SnapshotPayload& pre,
                                        const core::SnapshotPayload& target,
                                        std::vector<core::RestoreItemOutcome>& items) {
    for (const auto& [name, content] : pre.modprobeFiles) {
        if (target.modprobeFiles.contains(name)) continue;
        std::error_code ec;
        fs::remove(fs::path(modprobeDir_) / name, ec);
        items.push_back({.subject = name,
                         .action = "modprobe-remove",
                         .status = ec ? "failed" : "ok",
                         .detail = ec ? ec.message() : kConfigLevelNote});
    }
    for (const auto& [name, content] : target.modprobeFiles) {
        const auto pretty = pre.modprobeFiles.find(name);
        if (pretty != pre.modprobeFiles.end() && pretty->second == content) continue;
        auto written = atomicWriteFile(fs::path(modprobeDir_), name, content);
        items.push_back({.subject = name,
                         .action = "modprobe-write",
                         .status = written ? "ok" : "failed",
                         .detail = written ? kConfigLevelNote : written.error().message});
    }
}

void SnapshotService::reEnableRemoved(const core::SnapshotPayload& pre,
                                      const core::SnapshotPayload& target,
                                      std::vector<core::RestoreItemOutcome>& items) {
    auto enumerated = enumerator_.enumerate();
    const std::vector<core::Device> all = enumerated ? *enumerated : std::vector<core::Device>{};
    for (const auto& e : pre.entries) {
        if (containsKey(target.entries, e.key)) continue;
        const std::string path = resolvePathFor(e, all);
        if (path.empty()) {
            items.push_back({.subject = e.lastSysfsPath,
                             .action = "re-enable",
                             .status = "device-absent",
                             .detail = "device not present; nothing to re-enable"});
            continue;
        }
        auto applied = controller_.setEnabled(path, true, e.lastDriver);
        items.push_back({.subject = path,
                         .action = "re-enable",
                         .status = applied ? "ok" : "failed",
                         .detail = applied ? "" : applied.error().message});
    }
}

void SnapshotService::reApplyRestored(std::vector<core::RestoreItemOutcome>& items) {
    auto enumerated = enumerator_.enumerate();
    const std::vector<core::Device> all = enumerated ? *enumerated : std::vector<core::Device>{};
    for (const auto& e : state_.entries()) {
        // Same matching ladder as EnforcementService::sweep: live enumeration
        // by key or stored path, then the raw-sysfs fallback.
        const auto match = std::ranges::find_if(all, [&](const core::Device& d) {
            return services::matchesDevice(e.key, d) || e.lastSysfsPath == d.sysfsPath;
        });
        core::Device device;
        if (match != all.end()) {
            device = *match;
        } else {
            std::error_code ec;
            if (e.lastSysfsPath.empty() || !fs::is_directory(e.lastSysfsPath, ec)) {
                items.push_back(
                    {.subject = e.lastSysfsPath,
                     .action = "re-apply-disable",
                     .status = "device-absent",
                     .detail = "device not present; enforcement re-applies on hotplug"});
                continue;
            }
            device = deviceFromSysfs(e.lastSysfsPath);
        }
        items.push_back(reApplyOne(e, device));
    }
}

core::RestoreItemOutcome SnapshotService::reApplyOne(const core::DisabledDeviceEntry& entry,
                                                     const core::Device& device) {
    const bool needsApply = entry.mechanism == "authorized"
                                ? device.status != core::DeviceStatus::Disabled
                                : device.boundDriver.has_value();
    if (!needsApply)
        return {.subject = device.sysfsPath,
                .action = "re-apply-disable",
                .status = "ok",
                .detail = "already in desired state"};

    // Guard re-check on EVERY re-apply (enforcement discipline): topology may
    // have changed since the snapshot was taken. Refusal is reported, never
    // bypassed — the entry stays, marked guard-suspended.
    auto facts = prober_.probe();
    if (!facts)
        return {.subject = device.sysfsPath,
                .action = "re-apply-disable",
                .status = "failed",
                .detail = "criticality probe failed: " + facts.error().message};
    const auto verdict = services::evaluateDisable(*facts, device.sysfsPath);
    if (!verdict.allowed) {
        if (auto r = state_.setGuardSuspended(entry.key, true); !r) {
            return {
                .subject = device.sysfsPath,
                .action = "re-apply-disable",
                .status = "failed",
                .detail = "guard refused (" + verdict.reason + ") and suspension not persisted"};
        }
        return {.subject = device.sysfsPath,
                .action = "re-apply-disable",
                .status = "guard-refused",
                .detail = verdict.reason};
    }

    auto applied = controller_.setEnabled(device.sysfsPath, false, "");
    if (!applied)
        return {.subject = device.sysfsPath,
                .action = "re-apply-disable",
                .status = "failed",
                .detail = applied.error().message};
    if (entry.lastSysfsPath != device.sysfsPath)
        (void)state_.setLastSysfsPath(entry.key, device.sysfsPath);
    if (entry.guardSuspended) (void)state_.setGuardSuspended(entry.key, false);
    return {
        .subject = device.sysfsPath, .action = "re-apply-disable", .status = "ok", .detail = ""};
}

}  // namespace devmgr::daemon
