#include "devmgr/daemon/request_processor.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>

#include "devmgr/daemon/request_validation.hpp"
#include "devmgr/daemon/sysfs_device_probe.hpp"
#include "devmgr/services/critical_device_guard.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

namespace {
std::int64_t nowUtc() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

RequestProcessor::RequestProcessor(pal::IDeviceController& controller,
                                   pal::ICriticalityProber& prober, IAuthority& authority,
                                   pal::IDriverManager& drivers, pal::IDeviceEnumerator& enumerator,
                                   StateStore& store, SnapshotService& snapshots,
                                   std::mutex& applyMutex, std::string sysfsRoot)
    : controller_(controller),
      prober_(prober),
      authority_(authority),
      drivers_(drivers),
      enumerator_(enumerator),
      store_(store),
      snapshots_(snapshots),
      applyMutex_(applyMutex),
      sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<void> RequestProcessor::snapshotBefore(const char* verb, const std::string& subject) {
    auto snap = snapshots_.create(core::SnapshotTrigger::Auto,
                                  core::SnapshotReason{.verb = verb, .subject = subject});
    if (!snap)
        return core::makeError(core::Error::Code::Io, std::string("snapshot before ") + verb +
                                                          " failed (" + snap.error().message +
                                                          "); mutation refused");
    return {};
}

core::Result<std::string> RequestProcessor::canonicalContained(const std::string& sysfsPath) const {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    const auto rel = canonical.lexically_relative(root);
    if (ec || rel.empty() || rel.native().starts_with(".."))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);
    return canonical.string();
}

core::Result<void> RequestProcessor::authorize(const CallerId& caller, const char* action) {
    auto authorized = authority_.checkAuthorized(caller, action);
    if (!authorized) return tl::unexpected(authorized.error());
    if (!*authorized) return core::makeError(core::Error::Code::Permission, "authorization denied");
    return {};
}

core::Result<void> RequestProcessor::applyDisable(const std::string& canonical) {
    // Key building needs the enumerated Device (vendor/product/serial +
    // cloned-serial downgrade against the present set, spec §5.1). Enumerate
    // failure is tolerated as an empty set, and a device the enumerator
    // misses entirely still gets a key built straight from sysfs attributes
    // (Phase 5 Task 9 Step 1, pulled forward for T7 fix round 1).
    auto allResult = enumerator_.enumerate();
    const std::vector<core::Device> all = allResult ? *allResult : std::vector<core::Device>{};
    const auto device =
        std::ranges::find_if(all, [&](const core::Device& d) { return d.sysfsPath == canonical; });
    core::DisabledDeviceEntry entry;
    if (device != all.end()) {
        entry.key = services::makeDeviceKey(*device, all);
    } else {
        entry.key = services::makeDeviceKey(deviceFromSysfs(canonical));
    }
    auto applied = controller_.setEnabled(canonical, false, "");
    if (!applied) return tl::unexpected(applied.error());
    entry.mechanism = applied->has_value() ? "unbind" : "authorized";
    entry.lastDriver = applied->value_or("");
    entry.lastSysfsPath = canonical;
    entry.disabledAtUtc = nowUtc();
    return store_.upsert(entry);
}

core::Result<void> RequestProcessor::applyEnable(const std::string& canonical) {
    // Enable: delete the entry FIRST, then rebind — a rebind failure must
    // leave "enabled-but-unbound" with a clear error, never a lying store.
    // Match by lastSysfsPath OR by device key: after a daemon-down replug the
    // stored path is stale while the key still identifies the device
    // (Phase 5 review F-3). Guard the sysfs probe on directory existence,
    // same as EnforcementService::maybeReapply's fallback (enforcement_
    // service.cpp) — canonical is already validated by canonicalContained()
    // for every real caller, but applyEnable must not assume that here.
    std::error_code dirEc;
    const bool probable = fs::is_directory(canonical, dirEc);
    const core::Device probed = probable ? deviceFromSysfs(canonical) : core::Device{};
    std::string hint;
    for (const auto& e : store_.entries()) {
        const bool pathMatch = e.lastSysfsPath == canonical;
        const bool keyMatch = probable && services::matchesDevice(e.key, probed);
        if (pathMatch || keyMatch) {
            hint = e.lastDriver;
            auto removed = store_.remove(e.key);
            if (!removed) return removed;
            break;
        }
    }
    auto applied = controller_.setEnabled(canonical, true, hint);
    if (!applied) return tl::unexpected(applied.error());
    return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::setDeviceEnabled(const CallerId& caller,
                                                      const std::string& sysfsPath, bool enabled) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::sysfsPath(sysfsPath); !v) return v;
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());

    if (!enabled) {
        auto facts = prober_.probe();
        if (!facts) return tl::unexpected(facts.error());
        const auto verdict = services::evaluateDisable(*facts, *canonical);
        if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    }
    if (auto auth = authorize(caller, kActionSetDeviceEnabled); !auth) return auth;

    const std::scoped_lock lock(applyMutex_);
    if (auto snap = snapshotBefore("SetDeviceEnabled", *canonical); !snap) return snap;
    return enabled ? applyEnable(*canonical) : applyDisable(*canonical);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::loadModule(const CallerId& caller, const std::string& name) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::name(name, "module"); !v) return v;
    if (auto auth = authorize(caller, kActionManageModules); !auth) return auth;
    const std::scoped_lock lock(applyMutex_);
    if (auto snap = snapshotBefore("LoadModule", name); !snap) return snap;
    return drivers_.loadModule(name);
}

// Guard (spec §6.3): holders/refcount + criticality of the devices bound to
// the module. Split out of unloadModule so the verb body stays validate →
// guard → authorize → act at a glance.
core::Result<void> RequestProcessor::guardModuleUnload(const std::string& name) {
    services::ModuleUnloadFacts moduleFacts;
    auto loaded = drivers_.listLoadedModules();
    if (!loaded) return tl::unexpected(loaded.error());
    const auto mod =
        std::ranges::find_if(*loaded, [&](const core::LoadedModule& m) { return m.name == name; });
    if (mod == loaded->end())
        return core::makeError(core::Error::Code::NotFound, "module '" + name + "' not loaded");
    moduleFacts.holders = mod->holders;
    moduleFacts.refCount = mod->refCount;
    auto affected = drivers_.devicesUsingModule(name);
    if (!affected) return tl::unexpected(affected.error());
    moduleFacts.affectedDevicePaths = *affected;
    auto facts = prober_.probe();
    if (!facts) return tl::unexpected(facts.error());
    const auto verdict = services::evaluateModuleUnload(*facts, moduleFacts);
    if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    return {};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::unloadModule(const CallerId& caller, const std::string& name) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::name(name, "module"); !v) return v;
    if (auto guarded = guardModuleUnload(name); !guarded) return guarded;
    if (auto auth = authorize(caller, kActionManageModules); !auth) return auth;
    const std::scoped_lock lock(applyMutex_);
    if (auto snap = snapshotBefore("UnloadModule", name); !snap) return snap;
    return drivers_.unloadModule(name);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::bindDriver(const CallerId& caller,
                                                const std::string& sysfsPath,
                                                const std::string& driverName) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::sysfsPath(sysfsPath); !v) return v;
    if (auto v = validation::name(driverName, "driver"); !v) return v;
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());
    if (auto auth = authorize(caller, kActionManageDrivers); !auth) return auth;
    const std::scoped_lock lock(applyMutex_);
    if (auto snap = snapshotBefore("BindDriver", *canonical); !snap) return snap;
    return controller_.bindDriver(*canonical, driverName);  // surgical: no store
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::unbindDriver(const CallerId& caller,
                                                  const std::string& sysfsPath) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::sysfsPath(sysfsPath); !v) return v;
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());
    auto facts = prober_.probe();  // unbind ≡ disable risk (spec §6.1)
    if (!facts) return tl::unexpected(facts.error());
    const auto verdict = services::evaluateDisable(*facts, *canonical);
    if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    if (auto auth = authorize(caller, kActionManageDrivers); !auth) return auth;
    const std::scoped_lock lock(applyMutex_);
    if (auto snap = snapshotBefore("UnbindDriver", *canonical); !snap) return snap;
    return controller_.unbindDriver(*canonical);  // surgical: no store
}

std::vector<core::DisabledDeviceEntry> RequestProcessor::listDisabledDevices() const {
    return store_.entries();
}

core::Result<std::vector<core::SnapshotMeta>> RequestProcessor::snapshotList() {
    return snapshots_.list();  // unprivileged: metadata only, no secrets
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<std::string> RequestProcessor::snapshotCreate(const CallerId& caller,
                                                           const std::string& label) {
    if (auto v = validation::caller(caller); !v) return tl::unexpected(v.error());
    if (auto v = validation::snapshotLabel(label); !v) return tl::unexpected(v.error());
    if (auto auth = authorize(caller, kActionManageSnapshots); !auth)
        return tl::unexpected(auth.error());
    const std::scoped_lock lock(applyMutex_);
    return snapshots_.create(core::SnapshotTrigger::Manual,
                             core::SnapshotReason{.verb = "", .subject = label});
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<core::RestoreOutcome> RequestProcessor::snapshotRestore(const CallerId& caller,
                                                                     const std::string& id) {
    if (auto v = validation::caller(caller); !v) return tl::unexpected(v.error());
    if (auto v = validation::snapshotId(id); !v) return tl::unexpected(v.error());
    if (auto auth = authorize(caller, kActionManageSnapshots); !auth)
        return tl::unexpected(auth.error());
    const std::scoped_lock lock(applyMutex_);
    return snapshots_.restore(id);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) — CallerId aliases std::string
core::Result<void> RequestProcessor::snapshotDelete(const CallerId& caller, const std::string& id) {
    if (auto v = validation::caller(caller); !v) return v;
    if (auto v = validation::snapshotId(id); !v) return v;
    if (auto auth = authorize(caller, kActionManageSnapshots); !auth) return auth;
    const std::scoped_lock lock(applyMutex_);
    return snapshots_.remove(id);
}

}  // namespace devmgr::daemon
