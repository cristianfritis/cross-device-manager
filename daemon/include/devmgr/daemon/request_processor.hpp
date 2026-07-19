#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/snapshot_service.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

inline constexpr const char* kActionSetDeviceEnabled = "org.devmgr.set-device-enabled";
inline constexpr const char* kActionManageModules = "org.devmgr.manage-modules";
inline constexpr const char* kActionManageDrivers = "org.devmgr.manage-drivers";
inline constexpr const char* kActionManageSnapshots = "org.devmgr.manage-snapshots";

// validate → guard → authorize → act (unchanged, spec §6.2). New in Phase 5:
// SetDeviceEnabled persists desired state (StateStore); Bind/UnbindDriver are
// surgical and NEVER touch the store; module names are charset-validated; the
// apply mutex serializes every controller/store ACTION with EnforcementService.
// New in Phase 7: every mutating verb takes an automatic snapshot (reason =
// verb + subject) under the apply mutex BEFORE acting; snapshot failure fails
// the verb with Io (fail-closed — no mutation without its safety net).
class RequestProcessor {
   public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — adjacent string params
    RequestProcessor(pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                     IAuthority& authority, pal::IDriverManager& drivers,
                     pal::IDeviceEnumerator& enumerator, StateStore& store,
                     SnapshotService& snapshots, std::mutex& applyMutex,
                     std::string sysfsRoot = "/sys");

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> setDeviceEnabled(const CallerId& caller, const std::string& sysfsPath,
                                        bool enabled);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> loadModule(const CallerId& caller, const std::string& name);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> unloadModule(const CallerId& caller, const std::string& name);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId + three string params
    core::Result<void> bindDriver(const CallerId& caller, const std::string& sysfsPath,
                                  const std::string& driverName);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> unbindDriver(const CallerId& caller, const std::string& sysfsPath);
    std::vector<core::DisabledDeviceEntry> listDisabledDevices() const;  // read-only, no auth

    // ApiVersion 3 snapshot verbs. List is unprivileged (metadata only); the
    // three mutating verbs require org.devmgr.manage-snapshots.
    core::Result<std::vector<core::SnapshotMeta>> snapshotList();
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<std::string> snapshotCreate(const CallerId& caller, const std::string& label);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<core::RestoreOutcome> snapshotRestore(const CallerId& caller,
                                                       const std::string& id);
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
    core::Result<void> snapshotDelete(const CallerId& caller, const std::string& id);

    // ApiVersion 4. Unprivileged like List — a diff exposes configuration
    // state the caller can already read, so requiring a password to preview a
    // restore would train users to approve prompts. An empty `targetId` diffs
    // against live state.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — two snapshot ids
    core::Result<core::SnapshotDiff> snapshotDiff(const std::string& baseId,
                                                  const std::string& targetId);

   private:
    core::Result<std::string> canonicalContained(const std::string& sysfsPath) const;
    core::Result<void> authorize(const CallerId& caller, const char* action);
    core::Result<void> guardModuleUnload(const std::string& name);
    core::Result<void> applyDisable(const std::string& canonical);
    core::Result<void> applyEnable(const std::string& canonical);
    // The Phase 7 pre-mutation hook. Call with the apply mutex held, before
    // the first store/controller write of a mutating verb.
    core::Result<void> snapshotBefore(const char* verb, const std::string& subject);

    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    IAuthority& authority_;
    pal::IDriverManager& drivers_;
    pal::IDeviceEnumerator& enumerator_;
    StateStore& store_;
    SnapshotService& snapshots_;
    std::mutex& applyMutex_;
    std::string sysfsRoot_;
};

}  // namespace devmgr::daemon
