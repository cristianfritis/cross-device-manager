#pragma once
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "devmgr/services/critical_device_guard.hpp"

namespace devmgr::app {

// The single command/read surface the frontends use. refresh() runs enumeration
// on the TaskScheduler so the UI thread never blocks on I/O.
class ApplicationFacade {
   public:
    // channel/prober/drivers/systemInfo are optional (null in D-Bus-free or
    // kmod-less builds): without a channel setDeviceEnabled reports
    // Unsupported; without a prober canDisable/canUnloadModule are
    // advisory-unavailable and answer "allowed" (devmgrd remains
    // authoritative); without drivers/systemInfo the read methods degrade to
    // {} / nullopt / Unsupported.
    ApplicationFacade(pal::IDeviceEnumerator& enumerator, runtime::TaskScheduler& scheduler,
                      runtime::EventBus& bus, DeviceService& service,
                      pal::IPrivilegedChannel* channel = nullptr,
                      pal::ICriticalityProber* prober = nullptr,
                      pal::IDriverManager* drivers = nullptr,
                      pal::ISystemInfo* systemInfo = nullptr,
                      std::vector<pal::IUpdateProvider*> updateProviders = {})
        : enumerator_(enumerator),
          scheduler_(scheduler),
          bus_(bus),
          service_(service),
          channel_(channel),
          prober_(prober),
          drivers_(drivers),
          systemInfo_(systemInfo),
          updateProviders_(std::move(updateProviders)) {}

    // Runs enumeration on the TaskScheduler. The caller MUST wait on (or get)
    // the returned future before destroying this facade — the worker task
    // captures `this`, so discarding the future and destroying the facade would
    // dereference a dangling pointer.
    std::future<void> refresh();

    // Phase 4 mutation: resolves the device, calls the privileged channel on a
    // worker, publishes exactly ONE TaskCompletedEvent{taskId =
    // "set-enabled:" + id.value} — success and every failure mode alike.
    // Same future-custody contract as refresh(). The channel call may block
    // for ~minutes on interactive polkit auth; never wait on the UI thread.
    std::future<void> setDeviceEnabled(const core::DeviceId& id, bool enabled);

    // Advisory guard for UX (grey-out/annotate): pure core policy over
    // freshly probed facts. devmgrd re-checks authoritatively on every
    // request — this result is never a substitute for that.
    services::GuardVerdict canDisable(const core::DeviceId& id) const;

    std::vector<core::Device> devices() const { return service_.devices(); }
    std::optional<core::Device> findById(const core::DeviceId& id) const {
        return service_.findById(id);
    }

    // Reads (sync; {} / advisory-allowed degradation when seams are null).
    std::vector<core::Driver> driverInfo(const core::DeviceId& id) const;
    core::Result<std::vector<core::LoadedModule>> listModules() const;
    core::Result<core::Driver> moduleDetail(const std::string& name) const;
    core::Result<core::ModprobeInfo> modprobeDetail(const std::string& name) const;
    std::optional<pal::ISystemInfo::Info> systemInfo() const;
    services::GuardVerdict canUnloadModule(const std::string& name) const;
    // Mutations (async, Phase 4 pattern): ONE TaskCompletedEvent each, taskId
    // prefixes "load-module:", "unload-module:", "bind-driver:", "unbind-driver:".
    // Module mutations ALSO publish ModulesChangedEvent on success.
    std::future<void> loadModule(const std::string& name);
    std::future<void> unloadModule(const std::string& name);
    std::future<void> bindDriver(const core::DeviceId& id, const std::string& driverName);
    std::future<void> unbindDriver(const core::DeviceId& id);

    // ---- Phase 6: firmware/driver updates (spec §8) ----
    // Enumerates every update provider on the TaskScheduler, rebuilds the
    // per-provider snapshot (partial failure first-class, §8.1), reconciles the
    // durable pending/reboot state (M1, §8.2), then publishes
    // UpdatesRefreshedEvent. Same future-custody contract as refresh().
    std::future<void> refreshUpdates();
    std::vector<core::UpdateProviderState> updatesSnapshot() const;  // mutex-guarded copy
    std::vector<core::PendingAction> pendingUpdateActions() const;   // durable pending set
    // systemInfo().rebootPending || ∃ pending NeedsReboot. V4: never derived
    // from the live candidate list.
    bool rebootPendingEffective() const;
    bool installActive() const;  // quit-guard input (spec §5.5)
    // Serialized in-process (spec §5.4): a second call while one is active
    // completes immediately with ok=false and never reaches the provider.
    // Publishes exactly ONE TaskCompletedEvent{taskId="install-update:"+
    // candidateId} — success and every failure alike (Phase 4 pattern) —
    // TaskProgressEvent{same taskId} during, and UpdatesChangedEvent on a
    // terminal outcome. Same future-custody contract as refresh().
    std::future<void> installUpdate(std::string providerId, std::string candidateId,
                                    core::ReleaseRef release);

    // ---- Phase 7: snapshots (backup-rollback-engine, snapshot-ui spec) ----
    // Lists snapshot metadata via the privileged channel on the TaskScheduler,
    // replaces the mutex-guarded copy, then publishes SnapshotsRefreshedEvent.
    // Null channel degrades to an empty list (read degradation, like the other
    // seams); a channel error leaves the last list intact and publishes
    // ErrorEvent instead. Same future-custody contract as refresh().
    std::future<void> refreshSnapshots();
    std::vector<core::SnapshotMeta> snapshots() const;  // mutex-guarded copy
    // Mutations (async, Phase 4 pattern): ONE TaskCompletedEvent each — success
    // and every failure alike — taskId prefixes "snapshot-create:",
    // "snapshot-restore:", "snapshot-delete:"; SnapshotsChangedEvent
    // additionally on success so every frontend view refreshes. Restore's
    // message is the per-item convergence summary: guard refusals are items in
    // it, never a failed task (ok stays true). Same future-custody contract as
    // refresh().
    std::future<void> createSnapshot(std::string label);
    std::future<void> restoreSnapshot(std::string id);
    std::future<void> deleteSnapshot(std::string id);

   private:
    std::future<void> runChannelTask(
        std::string taskId, std::string okMessage, bool modulesChanged,
        std::function<core::Result<void>(pal::IPrivilegedChannel&)> call);

    // Snapshot mutation worker: null-channel degradation, ONE
    // TaskCompletedEvent, SnapshotsChangedEvent on success. `call` returns the
    // ok-message (restore: the per-item convergence summary).
    std::future<void> runSnapshotMutation(
        std::string taskId,
        std::function<core::Result<std::string>(pal::IPrivilegedChannel&)> call);

    pal::IUpdateProvider* findProvider(const std::string& providerId) const;
    // One provider's availability + candidates + M1 reconcile (refreshUpdates worker step).
    core::UpdateProviderState buildProviderState(pal::IUpdateProvider& provider);
    void reconcilePending(pal::IUpdateProvider& provider, const std::string& providerId,
                          bool enumerateSucceeded);
    // Candidates from the last successful snapshot for a provider (§8.1 retain).
    std::vector<core::UpdateCandidate> lastGoodCandidates(const std::string& providerId) const;
    void upsertPending(const core::PendingAction& action);
    // Erase this provider's pending entries whose key is absent from `reported`
    // — the positive-evidence clear (§8.2). Caller gates on query success.
    void erasePendingNotIn(const std::string& providerId,
                           const std::set<std::pair<std::string, std::string>>& reported);

    pal::IDeviceEnumerator& enumerator_;
    runtime::TaskScheduler& scheduler_;
    runtime::EventBus& bus_;
    DeviceService& service_;
    pal::IPrivilegedChannel* channel_ = nullptr;
    pal::ICriticalityProber* prober_ = nullptr;
    pal::IDriverManager* drivers_ = nullptr;
    pal::ISystemInfo* systemInfo_ = nullptr;

    std::vector<pal::IUpdateProvider*> updateProviders_;
    mutable std::mutex updatesMutex_;  // guards updatesSnapshot_ and pending_
    std::vector<core::UpdateProviderState> updatesSnapshot_;
    // Durable pending/reboot record (M1, §8.2), keyed (providerId, deviceId).
    // THIS is the sticky set: fed by install outcomes (disposition ≠ Completed)
    // and by pendingActions() reconciliation, cleared only on positive
    // evidence. Never derived from the live candidate list (V4).
    std::map<std::pair<std::string, std::string>, core::PendingAction> pending_;
    std::atomic<bool> installActive_{false};  // serialization gate (spec §5.4)

    mutable std::mutex snapshotsMutex_;  // guards snapshots_
    std::vector<core::SnapshotMeta> snapshots_;
};

}  // namespace devmgr::app
