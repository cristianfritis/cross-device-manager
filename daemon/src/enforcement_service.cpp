#include "devmgr/daemon/enforcement_service.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

#include "devmgr/daemon/sysfs_device_probe.hpp"
#include "devmgr/services/critical_device_guard.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

EnforcementService::EnforcementService(pal::IDeviceEnumerator& enumerator,
                                       pal::IDeviceController& controller,
                                       pal::ICriticalityProber& prober, StateStore& store,
                                       std::mutex& applyMutex)
    : enumerator_(enumerator),
      controller_(controller),
      prober_(prober),
      store_(store),
      applyMutex_(applyMutex) {}

void EnforcementService::sweep() {
    auto enumerated = enumerator_.enumerate();
    if (!enumerated) {
        spdlog::warn("enforcement sweep: enumeration failed: {}", enumerated.error().message);
        enumerated = std::vector<core::Device>{};  // every entry takes the sysfs fallback below
    }
    for (const auto& entry : store_.entries()) {
        const auto match = std::ranges::find_if(*enumerated, [&](const core::Device& device) {
            return services::matchesDevice(entry.key, device) ||
                   entry.lastSysfsPath == device.sysfsPath;
        });
        if (match != enumerated->end()) {
            maybeReapply(entry, *match);
            continue;
        }
        // The live enumerator does not list this device: a fake --sysfs-root
        // tree, or udev has not settled yet at startup. Re-apply straight from
        // the persisted path when it is still present on disk — the same
        // fallback RequestProcessor uses to key a device udev misses.
        std::error_code ec;
        if (entry.lastSysfsPath.empty() || !fs::is_directory(entry.lastSysfsPath, ec)) continue;
        maybeReapply(entry, deviceFromSysfs(entry.lastSysfsPath));
    }
}

void EnforcementService::onHotplug(const pal::HotplugEvent& event) {
    if (event.action == pal::HotplugEvent::Action::Removed) return;
    const auto entry = store_.findFor(event.device);
    if (!entry) return;
    maybeReapply(*entry, event.device);
}

void EnforcementService::maybeReapply(const core::DisabledDeviceEntry& entry,
                                      const core::Device& device) {
    // Already in the desired state? authorized mechanism shows as Disabled in
    // the mapper; unbind mechanism shows as no bound driver.
    const bool needsApply = entry.mechanism == "authorized"
                                ? device.status != core::DeviceStatus::Disabled
                                : device.boundDriver.has_value();
    if (!needsApply) return;

    // Guard re-check on EVERY re-apply (spec §5.3): topology may have changed.
    auto facts = prober_.probe();
    if (!facts) {
        spdlog::warn("enforcement: prober failed for {}: {}", device.sysfsPath,
                     facts.error().message);
        return;
    }
    const auto verdict = services::evaluateDisable(*facts, device.sysfsPath);
    if (!verdict.allowed) {
        spdlog::warn("enforcement suspended for {}: {}", device.sysfsPath, verdict.reason);
        if (auto r = store_.setGuardSuspended(entry.key, true); !r)
            spdlog::warn("enforcement: cannot persist suspension: {}", r.error().message);
        return;
    }

    const std::scoped_lock lock(applyMutex_);
    auto applied = controller_.setEnabled(device.sysfsPath, false, "");
    if (!applied) {  // log-and-continue: never crash the daemon over one device
        spdlog::warn("enforcement: re-apply failed for {}: {}", device.sysfsPath,
                     applied.error().message);
        return;
    }
    spdlog::info("enforcement: re-disabled {}", device.sysfsPath);
    if (entry.lastSysfsPath != device.sysfsPath) {
        if (auto r = store_.setLastSysfsPath(entry.key, device.sysfsPath); !r)
            spdlog::warn("enforcement: cannot update path: {}", r.error().message);
    }
    if (entry.guardSuspended) {
        if (auto r = store_.setGuardSuspended(entry.key, false); !r)
            spdlog::warn("enforcement: cannot clear suspension: {}", r.error().message);
    }
}

}  // namespace devmgr::daemon
