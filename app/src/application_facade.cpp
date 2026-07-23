#include "devmgr/app/application_facade.hpp"

#include <atomic>
#include <set>
#include <utility>

#include "devmgr/app/disabled_overlay.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/snapshot_presentation.hpp"

namespace devmgr::app {

namespace {

// Releases the serialization gate on EVERY worker exit path (spec §5.4).
struct InstallActiveGuard {
    std::atomic<bool>& flag;
    explicit InstallActiveGuard(std::atomic<bool>& f) : flag(f) {}
    InstallActiveGuard(const InstallActiveGuard&) = delete;
    InstallActiveGuard& operator=(const InstallActiveGuard&) = delete;
    InstallActiveGuard(InstallActiveGuard&&) = delete;
    InstallActiveGuard& operator=(InstallActiveGuard&&) = delete;
    ~InstallActiveGuard() { flag.store(false); }
};
}  // namespace

std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (!result) {
            bus_.publish(
                core::ErrorEvent{.source = "enumerate", .message = result.error().message});
            return;
        }
        if (channel_ != nullptr) {
            // ONE bulk fetch per refresh (spec §6.1); daemon-down or API-1
            // degrades silently to Phase 4 rendering.
            if (auto disabled = channel_->listDisabledDevices(); disabled)
                applyDisabledOverlay(*result, *disabled);
        }
        service_.applyEnumeration(std::move(*result));
    });
}

std::future<void> ApplicationFacade::setDeviceEnabled(const core::DeviceId& id, bool enabled) {
    return scheduler_.submit([this, id, enabled] {
        const std::string taskId = "set-enabled:" + id.value;
        const auto device = service_.findById(id);  // resolve at execution time
        if (!device) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = "device no longer present"});
            return;
        }
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = channel_->setDeviceEnabled(*device, enabled);
        if (result) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId,
                .ok = true,
                .message = (enabled ? "Enabled " : "Disabled ") + device->name});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

services::GuardVerdict ApplicationFacade::canDisable(const core::DeviceId& id) const {
    if (prober_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto facts = prober_->probe();
    if (!facts) return {};  // advisory unavailable → allowed; daemon is authoritative
    return services::evaluateDisable(*facts, device->sysfsPath);
}

std::optional<pal::CriticalityFacts> ApplicationFacade::criticalityFacts() const {
    if (prober_ == nullptr) return std::nullopt;
    auto facts = prober_->probe();
    if (!facts) return std::nullopt;
    return *facts;
}

std::vector<core::Driver> ApplicationFacade::driverInfo(const core::DeviceId& id) const {
    if (drivers_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto result = drivers_->driversFor(*device);
    return result ? *result : std::vector<core::Driver>{};
}

core::Result<std::vector<core::LoadedModule>> ApplicationFacade::listModules() const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->listLoadedModules();
}

core::Result<core::Driver> ApplicationFacade::moduleDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->moduleInfo(name);
}

core::Result<core::ModprobeInfo> ApplicationFacade::modprobeDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->modprobeInfo(name);
}

std::optional<pal::ISystemInfo::Info> ApplicationFacade::systemInfo() const {
    if (systemInfo_ == nullptr) return std::nullopt;
    auto info = systemInfo_->query();
    if (!info) return std::nullopt;
    return *info;
}

services::GuardVerdict ApplicationFacade::canUnloadModule(const std::string& name) const {
    if (drivers_ == nullptr || prober_ == nullptr) return {};  // advisory unavailable
    services::ModuleUnloadFacts moduleFacts;
    auto loaded = drivers_->listLoadedModules();
    if (!loaded) return {};
    for (const auto& m : *loaded) {
        if (m.name != name) continue;
        moduleFacts.holders = m.holders;
        moduleFacts.refCount = m.refCount;
    }
    if (auto affected = drivers_->devicesUsingModule(name); affected)
        moduleFacts.affectedDevicePaths = *affected;
    auto facts = prober_->probe();
    if (!facts) return {};
    return services::evaluateModuleUnload(*facts, moduleFacts);
}

std::future<void> ApplicationFacade::runChannelTask(
    std::string taskId, std::string okMessage, bool modulesChanged,
    std::function<core::Result<void>(pal::IPrivilegedChannel&)> call) {
    return scheduler_.submit([this, taskId = std::move(taskId), okMessage = std::move(okMessage),
                              modulesChanged, call = std::move(call)] {
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = call(*channel_);
        if (result) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId, .ok = true, .message = okMessage});
            if (modulesChanged) bus_.publish(core::ModulesChangedEvent{});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

std::future<void> ApplicationFacade::loadModule(const std::string& name) {
    return runChannelTask("load-module:" + name, "Loaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.loadModule(name); });
}

std::future<void> ApplicationFacade::unloadModule(const std::string& name) {
    return runChannelTask("unload-module:" + name, "Unloaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.unloadModule(name); });
}

std::future<void> ApplicationFacade::bindDriver(const core::DeviceId& id,
                                                const std::string& driverName) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("bind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask("bind-driver:" + id.value, "Bound " + driverName + " to " + device->name,
                          false, [device = *device, driverName](pal::IPrivilegedChannel& c) {
                              return c.bindDriver(device, driverName);
                          });
}

std::future<void> ApplicationFacade::unbindDriver(const core::DeviceId& id) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("unbind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask(
        "unbind-driver:" + id.value, "Unbound driver from " + device->name, false,
        [device = *device](pal::IPrivilegedChannel& c) { return c.unbindDriver(device); });
}

// ---- Phase 6: firmware/driver updates ----

core::UpdateProviderState ApplicationFacade::buildProviderState(pal::IUpdateProvider& provider) {
    core::UpdateProviderState st;
    st.providerId = provider.providerId();
    st.availability = provider.availability();
    if (!st.availability.available) {
        st.candidates = lastGoodCandidates(st.providerId);
        return st;
    }
    if (auto candidates = provider.enumerate()) {
        st.candidates = std::move(*candidates);
    } else {
        st.refreshError = candidates.error();
        st.candidates = lastGoodCandidates(st.providerId);  // §8.1 deliberate retain
    }
    reconcilePending(provider, st.providerId, !st.refreshError.has_value());
    return st;
}

void ApplicationFacade::reconcilePending(pal::IUpdateProvider& provider,
                                         const std::string& providerId, bool enumerateSucceeded) {
    // M1 reconcile (§8.2): the provider's durable history (fwupd
    // GetHistory/GetResults) is authoritative. Upsert every reported action,
    // then clear an entry ONLY on positive evidence — the pending query
    // succeeded, enumerate succeeded, AND the key is absent from the report. A
    // failed pending query or a failed enumerate is NO evidence, so the sticky
    // entry is retained. Never derived from the candidate list (V4).
    auto pending = provider.pendingActions();
    if (!pending) return;
    std::set<std::pair<std::string, std::string>> reported;
    for (const auto& action : *pending) {
        // Key under the reconciling provider's id so the positive-evidence
        // clear (erasePendingNotIn, same id) can always reach these entries —
        // defense-in-depth against a provider reporting a foreign providerId.
        core::PendingAction owned = action;
        owned.providerId = providerId;
        reported.insert({owned.providerId, owned.deviceId});
        upsertPending(owned);
    }
    if (enumerateSucceeded) erasePendingNotIn(providerId, reported);
}

std::future<void> ApplicationFacade::refreshUpdates() {
    return scheduler_.submit([this] {
        std::vector<core::UpdateProviderState> next;
        next.reserve(updateProviders_.size());
        for (auto* provider : updateProviders_) next.push_back(buildProviderState(*provider));
        {
            std::scoped_lock lock(updatesMutex_);
            updatesSnapshot_ = std::move(next);
        }
        bus_.publish(core::UpdatesRefreshedEvent{});
    });
}

std::future<void> ApplicationFacade::installUpdate(std::string providerId, std::string candidateId,
                                                   core::ReleaseRef release) {
    const std::string taskId = "install-update:" + candidateId;
    // Serialize in-process (spec §5.4): claim the slot synchronously, before
    // scheduling, so a racing second caller observes it taken and the provider
    // never sees a concurrent install.
    if (installActive_.exchange(true)) {
        bus_.publish(core::TaskCompletedEvent{
            .taskId = taskId, .ok = false, .message = "another update is already in progress"});
        std::promise<void> done;
        done.set_value();
        return done.get_future();
    }
    try {
        return scheduler_.submit([this, providerId = std::move(providerId),
                                  candidateId = std::move(candidateId),
                                  release = std::move(release), taskId] {
            InstallActiveGuard guard{installActive_};  // release the slot on every exit
            auto* provider = findProvider(providerId);
            if (provider == nullptr) {
                bus_.publish(core::TaskCompletedEvent{
                    .taskId = taskId,
                    .ok = false,
                    .message = "update provider not found: " + providerId});
                return;
            }
            // V1 defense-in-depth: refuse a status-only provider even though the UI
            // pre-gates the install verb.
            if (!pal::hasCap(provider->capabilities(), pal::UpdateProviderCaps::Install)) {
                bus_.publish(
                    core::TaskCompletedEvent{.taskId = taskId,
                                             .ok = false,
                                             .message = "provider is status-only: " + providerId});
                return;
            }
            auto outcome = provider->install(
                candidateId, release, [this, taskId](const runtime::ProgressUpdate& update) {
                    bus_.publish(core::TaskProgressEvent{
                        .taskId = taskId, .percent = update.percent, .stage = update.stage});
                });
            if (outcome) {
                if (outcome->disposition != core::InstallDisposition::Completed) {
                    // Seed the durable record immediately (§8.2 (a)); the next
                    // refresh reconciles it against provider history. deviceName is
                    // the candidate id here — provider-reported entries carry the
                    // human name; this session seed keys and surfaces by id.
                    upsertPending(
                        core::PendingAction{.providerId = providerId,
                                            .deviceId = candidateId,
                                            .deviceName = candidateId,
                                            .disposition = outcome->disposition,
                                            .version = outcome->observedVersion.value_or("")});
                }
                bus_.publish(core::TaskCompletedEvent{
                    .taskId = taskId, .ok = true, .message = outcome->message});
            } else {
                bus_.publish(core::TaskCompletedEvent{
                    .taskId = taskId, .ok = false, .message = outcome.error().message});
            }
            bus_.publish(core::UpdatesChangedEvent{});
        });
    } catch (...) {
        installActive_.store(false);  // scheduler stopping: release the slot we claimed
        throw;
    }
}

// ---- Phase 7: snapshots ----

std::future<void> ApplicationFacade::refreshSnapshots() {
    return scheduler_.submit([this] {
        if (channel_ == nullptr) {
            // Read degradation: no channel, no snapshots — publish so VMs
            // still rebuild to the empty state.
            {
                std::scoped_lock lock(snapshotsMutex_);
                snapshots_.clear();
            }
            bus_.publish(core::SnapshotsRefreshedEvent{});
            return;
        }
        auto result = channel_->snapshotList();
        if (!result) {
            bus_.publish(
                core::ErrorEvent{.source = "snapshot-list", .message = result.error().message});
            return;  // last list stays intact, mirroring refresh()
        }
        {
            std::scoped_lock lock(snapshotsMutex_);
            snapshots_ = std::move(*result);
        }
        bus_.publish(core::SnapshotsRefreshedEvent{});
    });
}

std::vector<core::SnapshotMeta> ApplicationFacade::snapshots() const {
    std::scoped_lock lock(snapshotsMutex_);
    return snapshots_;
}

std::future<void> ApplicationFacade::refreshSnapshotDiff(std::string baseId, std::string targetId) {
    return scheduler_.submit([this, baseId = std::move(baseId), targetId = std::move(targetId)] {
        if (channel_ == nullptr) {
            // Read degradation, same shape as refreshSnapshots(): clear and
            // publish so the view leaves its loading state.
            {
                std::scoped_lock lock(snapshotsMutex_);
                diff_.reset();
            }
            bus_.publish(core::SnapshotDiffRefreshedEvent{});
            return;
        }
        auto result = channel_->snapshotDiff(baseId, targetId);
        if (!result) {
            // Unlike the list, a failed diff clears the cache: a preview that
            // silently showed the previous pair's diff would misdescribe what
            // a restore is about to do.
            {
                std::scoped_lock lock(snapshotsMutex_);
                diff_.reset();
            }
            bus_.publish(
                core::ErrorEvent{.source = "snapshot-diff", .message = result.error().message});
            bus_.publish(core::SnapshotDiffRefreshedEvent{});
            return;
        }
        {
            std::scoped_lock lock(snapshotsMutex_);
            diff_ = std::move(*result);
        }
        bus_.publish(core::SnapshotDiffRefreshedEvent{});
    });
}

std::optional<core::SnapshotDiff> ApplicationFacade::snapshotDiff() const {
    std::scoped_lock lock(snapshotsMutex_);
    return diff_;
}

std::optional<core::RestoreOutcome> ApplicationFacade::lastRestoreOutcome() const {
    std::scoped_lock lock(snapshotsMutex_);
    return lastRestore_;
}

std::future<void> ApplicationFacade::runSnapshotMutation(
    std::string taskId, std::function<core::Result<std::string>(pal::IPrivilegedChannel&)> call) {
    return scheduler_.submit([this, taskId = std::move(taskId), call = std::move(call)] {
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = call(*channel_);
        if (result) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId, .ok = true, .message = *result});
            bus_.publish(core::SnapshotsChangedEvent{});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

std::future<void> ApplicationFacade::createSnapshot(std::string label) {
    std::string taskId = "snapshot-create:" + label;
    return runSnapshotMutation(
        std::move(taskId),
        [label = std::move(label)](pal::IPrivilegedChannel& c) -> core::Result<std::string> {
            auto id = c.snapshotCreate(label);
            if (!id) return tl::unexpected(id.error());
            return "Created snapshot " + core::snapshotShortId(*id);
        });
}

std::future<void> ApplicationFacade::restoreSnapshot(std::string id) {
    std::string taskId = "snapshot-restore:" + id;
    return runSnapshotMutation(
        std::move(taskId),
        [this, id = std::move(id)](pal::IPrivilegedChannel& c) -> core::Result<std::string> {
            auto outcome = c.snapshotRestore(id);
            {
                // Retained for the UIs' recovery guidance. A failed restore
                // clears it: there is no outcome to compose guidance from, and
                // a stale one would name the wrong safety snapshot.
                std::scoped_lock lock(snapshotsMutex_);
                lastRestore_ = outcome ? std::optional{*outcome} : std::nullopt;
            }
            if (!outcome) return tl::unexpected(outcome.error());
            return core::restoreSummary(*outcome);
        });
}

std::future<void> ApplicationFacade::deleteSnapshot(std::string id) {
    std::string taskId = "snapshot-delete:" + id;
    return runSnapshotMutation(
        std::move(taskId),
        [id = std::move(id)](pal::IPrivilegedChannel& c) -> core::Result<std::string> {
            auto result = c.snapshotDelete(id);
            if (!result) return tl::unexpected(result.error());
            return "Deleted snapshot " + core::snapshotShortId(id);
        });
}

std::vector<core::UpdateProviderState> ApplicationFacade::updatesSnapshot() const {
    std::scoped_lock lock(updatesMutex_);
    return updatesSnapshot_;
}

std::vector<core::PendingAction> ApplicationFacade::pendingUpdateActions() const {
    std::scoped_lock lock(updatesMutex_);
    std::vector<core::PendingAction> actions;
    actions.reserve(pending_.size());
    for (const auto& [key, action] : pending_) actions.push_back(action);
    return actions;
}

bool ApplicationFacade::rebootPendingEffective() const {
    {
        std::scoped_lock lock(updatesMutex_);
        for (const auto& [key, action] : pending_)
            if (action.disposition == core::InstallDisposition::NeedsReboot) return true;
    }
    const auto info = systemInfo();  // outside the lock: may query the PAL
    return info.has_value() && info->rebootPending;
}

bool ApplicationFacade::installActive() const {
    return installActive_.load();
}

pal::IUpdateProvider* ApplicationFacade::findProvider(const std::string& providerId) const {
    for (auto* provider : updateProviders_)
        if (provider->providerId() == providerId) return provider;
    return nullptr;
}

std::vector<core::UpdateCandidate> ApplicationFacade::lastGoodCandidates(
    const std::string& providerId) const {
    std::scoped_lock lock(updatesMutex_);
    for (const auto& st : updatesSnapshot_)
        if (st.providerId == providerId) return st.candidates;
    return {};
}

void ApplicationFacade::upsertPending(const core::PendingAction& action) {
    std::scoped_lock lock(updatesMutex_);
    pending_[{action.providerId, action.deviceId}] = action;
}

void ApplicationFacade::erasePendingNotIn(
    const std::string& providerId, const std::set<std::pair<std::string, std::string>>& reported) {
    std::scoped_lock lock(updatesMutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->first.first == providerId && !reported.contains(it->first))
            it = pending_.erase(it);
        else
            ++it;
    }
}

}  // namespace devmgr::app
