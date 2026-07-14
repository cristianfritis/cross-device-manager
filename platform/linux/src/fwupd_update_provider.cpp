#include "devmgr/platform/linux/fwupd_update_provider.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/spdlog.h>

#include "devmgr/core/events.hpp"
#include "devmgr/platform/linux/cab_resolver.hpp"
#include "devmgr/platform/linux/fwupd_contract.hpp"

namespace devmgr::platform_linux {
namespace {

using Dict = fwupd::Dict;

constexpr const char* kDbusBusName = "org.freedesktop.DBus";
constexpr const char* kDbusObjectPath = "/org/freedesktop/DBus";
constexpr const char* kDbusInterface = "org.freedesktop.DBus";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";

// FwupdRemoteKind (libfwupd/fwupd-remote.h, fwupd-2.0.20 source — matches the
// running daemon): UNKNOWN=0, DOWNLOAD=1, LOCAL=2, DIRECTORY=3. Confirmed
// against a live `GetRemotes` reply on this host (vendor-directory remote:
// Type=3; lvfs/lvfs-testing: Type=1) — see task-6-report.md.
constexpr std::uint32_t kRemoteKindDownload = 1;
constexpr std::uint32_t kRemoteKindDirectory = 3;

// FwupdRequestKind (libfwupd/fwupd-request.h): UNKNOWN=0, POST=1, IMMEDIATE=2.
constexpr std::uint32_t kRequestKindPost = 1;
constexpr std::uint32_t kRequestKindImmediate = 2;

constexpr std::uint64_t kStaleMetadataDays = 30;
constexpr std::uint64_t kSecondsPerDay = 86400;

// Known key, wrong variant type ⇒ treat as absent + debug log; never throw.
// Mirrors fwupd_contract.cpp's private helper — remote/request dicts aren't
// part of the pinned parse-layer contract (RemoteRef lives in cab_resolver,
// not fwupd::), so the provider keeps its own tolerant reader for them.
template <typename T>
std::optional<T> get(const Dict& d, const char* key) {
    const auto it = d.find(key);
    if (it == d.end()) return std::nullopt;
    try {
        return it->second.get<T>();
    } catch (const std::exception& e) {
        spdlog::debug("fwupd: key '{}' has unexpected variant type: {}", key, e.what());
        return std::nullopt;
    }
}

std::string coreErrorFromSdbus(const sdbus::Error& e) {
    return fwupd::mapError(std::string{e.getName()}, std::string{e.getMessage()}).message;
}

// GetRemotes row → cab-resolution RemoteRef (spec §5.3). Non-directory kinds
// (download/local/unknown) all behave identically for cab_resolver (only
// "directory" unlocks relative-location resolution), so they fold to a single
// non-"directory" tag here.
RemoteRef parseRemoteRef(const Dict& d) {
    const auto kind = get<std::uint32_t>(d, "Type").value_or(0);
    return RemoteRef{
        .id = get<std::string>(d, "RemoteId").value_or(""),
        .kind = kind == kRemoteKindDirectory ? "directory" : "download",
        .filenameCache = get<std::string>(d, "FilenameCache").value_or(""),
    };
}

// Tolerant read of the remote's metadata-age timestamp. Pinned key
// (task-6-report.md, live GetRemotes): "ModificationTime". "Mtime" kept as a
// fallback for skew we haven't observed, never for a key we've confirmed.
std::optional<std::uint64_t> remoteModificationTime(const Dict& d) {
    if (auto t = get<std::uint64_t>(d, "ModificationTime")) return t;
    return get<std::uint64_t>(d, "Mtime");
}

// One stale-metadata notice line for an enabled download-kind remote, or
// nullopt (spec §8.1: absent/unusable mtime ⇒ ⊥ guess, no notice).
std::optional<std::string> staleMetadataNotice(const Dict& d, std::uint64_t nowSec) {
    if (get<std::uint32_t>(d, "Type").value_or(0) != kRemoteKindDownload) return std::nullopt;
    if (!get<bool>(d, "Enabled").value_or(false)) return std::nullopt;
    const auto mtime = remoteModificationTime(d);
    // mtime >= now covers both clock skew and fwupd's UINT64_MAX "never
    // refreshed" sentinel (observed live on a disabled remote) — neither is a
    // sane age to report, so this is ⊥ guess rather than an overflow bug.
    if (!mtime || *mtime >= nowSec) return std::nullopt;
    const auto ageDays = (nowSec - *mtime) / kSecondsPerDay;
    if (ageDays <= kStaleMetadataDays) return std::nullopt;
    const auto id = get<std::string>(d, "RemoteId").value_or("(unknown remote)");
    return id + " metadata " + std::to_string(ageDays) + " days old — run fwupdmgr refresh";
}

std::optional<std::string> failedHistoryNotice(const std::vector<Dict>& history) {
    const auto failed = std::count_if(history.begin(), history.end(), [](const Dict& d) {
        return get<std::uint32_t>(d, "UpdateState").value_or(0) == fwupd::kUpdateStateFailed;
    });
    if (failed <= 0) return std::nullopt;
    return std::to_string(failed) + " previous update(s) failed — see fwupdmgr history";
}

// Release identity dedupe (spec §2/§5.1): (remoteId, checksum), never version.
std::vector<core::ReleaseInfo> dedupeReleases(std::vector<core::ReleaseInfo> in) {
    std::vector<core::ReleaseInfo> out;
    out.reserve(in.size());
    for (auto& r : in) {
        const bool dup = std::ranges::any_of(out, [&](const core::ReleaseInfo& e) {
            return e.remoteId == r.remoteId && e.checksum == r.checksum;
        });
        if (!dup) out.push_back(std::move(r));
    }
    return out;
}

// One device's GetUpgrades outcome. `NothingToDo` ⇒ empty releases, not a
// failure; any other error ⇒ empty releases + a message for the caller to
// attach to the candidate's details (spec §5.1: the device row survives).
struct UpgradesOutcome {
    std::vector<core::ReleaseInfo> releases;
    std::optional<std::string> queryError;
};

UpgradesOutcome fetchUpgrades(sdbus::IProxy& proxy, const std::string& deviceId,
                              const std::vector<RemoteRef>& remotes) {
    UpgradesOutcome outcome;
    std::vector<Dict> raw;
    try {
        proxy.callMethod("GetUpgrades")
            .onInterface(sdbus::InterfaceName{fwupd::kInterface})
            .withArguments(deviceId)
            .storeResultsTo(raw);
    } catch (const sdbus::Error& e) {
        if (fwupd::isNothingToDo(std::string{e.getName()})) return outcome;
        outcome.queryError = coreErrorFromSdbus(e);
        return outcome;
    }
    std::vector<core::ReleaseInfo> parsed;
    parsed.reserve(raw.size());
    for (const auto& d : raw) {
        if (auto r = fwupd::parseRelease(d)) parsed.push_back(std::move(*r));
    }
    outcome.releases = dedupeReleases(std::move(parsed));
    for (auto& r : outcome.releases)
        r.localCab = isLocallyResolvable(r.locations, remotes, r.remoteId);
    return outcome;
}

core::UpdateCandidate buildCandidate(const fwupd::ParsedDevice& device,
                                     const std::vector<RemoteRef>& remotes, sdbus::IProxy& proxy) {
    core::UpdateCandidate c{
        .providerId = "fwupd",
        .id = device.deviceId,
        .displayName = device.name,
        .currentVersion = device.version,
        .facts = device.facts,
    };
    c.details.emplace_back("vendor", device.vendor);
    if (device.facts.updatable) {
        auto outcome = fetchUpgrades(proxy, device.deviceId, remotes);
        c.releases = std::move(outcome.releases);
        if (outcome.queryError)
            c.details.emplace_back("upgrades", "query failed: " + *outcome.queryError);
    }
    if (!c.releases.empty()) c.candidateVersion = c.releases.front().version;
    return c;
}

std::string requestKindToString(std::uint32_t kind) {
    if (kind == kRequestKindPost) return "post";
    if (kind == kRequestKindImmediate) return "immediate";
    return std::to_string(kind);  // unknown kind: pass the raw number through
}

}  // namespace

class FwupdUpdateProvider::Impl {
    // install() (T8, M3 state machine) lives on the outer class so the RAII
    // Gate can close over the by-value `progress` parameter directly; it
    // manipulates proxy_/installing_/progressSink_ as `impl->member` and
    // therefore needs access to them without widening this class's public API.
    friend class FwupdUpdateProvider;

   public:
    Impl(runtime::EventBus& bus, Config cfg) : bus_(bus) {
        try {
            connection_ = cfg.useSessionBus ? sdbus::createSessionBusConnection()
                                            : sdbus::createSystemBusConnection();
            proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{fwupd::kBusName},
                                        sdbus::ObjectPath{fwupd::kObjectPath});
            nameOwnerProxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{kDbusBusName},
                                                 sdbus::ObjectPath{kDbusObjectPath});
            registerSignalHandlers();  // BEFORE the loop starts
            connection_->enterEventLoopAsync();
        } catch (const std::exception& e) {
            initError_ = e.what();
            nameOwnerProxy_.reset();
            proxy_.reset();
            connection_.reset();
        }
    }

    ~Impl() {
        // Spec §5.5 teardown order — do not reorder:
        accepting_.store(false);                         // 1. stop accepting ops
        nameOwnerProxy_.reset();                         // 2. drop signal registrations
        proxy_.reset();                                  //    (both proxies, same step)
        if (connection_) connection_->leaveEventLoop();  // 3. stop + join async loop
        connection_.reset();                             // 4. destroy connection last
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    [[nodiscard]] core::ProviderAvailability availability() const {
        core::ProviderAvailability out;
        if (!connection_ || !proxy_) {
            out.error = core::Error{.code = core::Error::Code::Io, .message = initError_};
            return out;
        }
        try {
            const sdbus::Variant v = proxy_->getProperty("DaemonVersion")
                                         .onInterface(sdbus::InterfaceName{fwupd::kInterface});
            out.available = true;
            out.version = v.get<std::string>();
        } catch (const sdbus::Error& e) {
            out.error = fwupd::mapError(std::string{e.getName()}, std::string{e.getMessage()});
            return out;
        } catch (const std::exception& e) {
            out.error = core::Error{.code = core::Error::Code::Io, .message = e.what()};
            return out;
        }
        collectNotices(out.notices);
        return out;
    }

    core::Result<std::vector<core::UpdateCandidate>> enumerate() {
        if (!proxy_) return core::makeError(core::Error::Code::Io, initError_);
        try {
            std::vector<Dict> deviceDicts;
            proxy_->callMethod("GetDevices")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .storeResultsTo(deviceDicts);
            std::vector<Dict> remoteDicts;
            proxy_->callMethod("GetRemotes")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .storeResultsTo(remoteDicts);

            std::vector<RemoteRef> remotes;
            remotes.reserve(remoteDicts.size());
            for (const auto& rd : remoteDicts) remotes.push_back(parseRemoteRef(rd));

            std::vector<core::UpdateCandidate> out;
            out.reserve(deviceDicts.size());
            for (const auto& dd : deviceDicts) {
                auto parsed = fwupd::parseDevice(dd);
                if (!parsed) continue;
                out.push_back(buildCandidate(*parsed, remotes, *proxy_));
            }
            return out;
        } catch (const sdbus::Error& e) {
            return tl::unexpected(
                fwupd::mapError(std::string{e.getName()}, std::string{e.getMessage()}));
        } catch (const std::exception& e) {
            return core::makeError(core::Error::Code::Io, e.what());
        }
    }

    core::Result<std::vector<core::PendingAction>> pendingActions() {
        if (!proxy_) return core::makeError(core::Error::Code::Io, initError_);
        try {
            std::vector<Dict> raw;
            proxy_->callMethod("GetHistory")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .storeResultsTo(raw);
            std::vector<core::PendingAction> out;
            for (const auto& d : raw) {
                if (auto p = fwupd::parseHistoryEntry(d)) out.push_back(std::move(*p));
            }
            return out;
        } catch (const sdbus::Error& e) {
            // Verified live (task-6-report.md): fwupd throws NothingToDo for
            // GetHistory when the history db is empty, same as GetUpgrades —
            // "no history" is not a pendingActions() failure.
            if (fwupd::isNothingToDo(std::string{e.getName()}))
                return std::vector<core::PendingAction>{};
            return tl::unexpected(
                fwupd::mapError(std::string{e.getName()}, std::string{e.getMessage()}));
        } catch (const std::exception& e) {
            return core::makeError(core::Error::Code::Io, e.what());
        }
    }

   private:
    // Kept low cognitive-complexity by delegating every handler's actual
    // logic (gate check + work) to a named method — each .call() lambda body
    // is a single non-branching forwarding statement.
    void registerSignalHandlers() {
        const sdbus::InterfaceName fwupdIface{fwupd::kInterface};
        proxy_->uponSignal("DeviceAdded").onInterface(fwupdIface).call([this](const Dict&) {
            onDeviceListSignal();
        });
        proxy_->uponSignal("DeviceRemoved").onInterface(fwupdIface).call([this](const Dict&) {
            onDeviceListSignal();
        });
        proxy_->uponSignal("DeviceChanged").onInterface(fwupdIface).call([this](const Dict&) {
            onDeviceListSignal();
        });
        proxy_->uponSignal("Changed").onInterface(fwupdIface).call([this] {
            onDeviceListSignal();
        });
        proxy_->uponSignal("DeviceRequest").onInterface(fwupdIface).call([this](const Dict& d) {
            onDeviceRequestSignal(d);
        });
        // V5 progress gating (T8, spec §5.4): registered here alongside every
        // other signal, BEFORE enterEventLoopAsync — same discipline as above.
        const sdbus::InterfaceName propsIface{kPropertiesInterface};
        proxy_->uponSignal("PropertiesChanged")
            .onInterface(propsIface)
            .call([this](const std::string& interfaceName, const Dict& changedProps,
                         const std::vector<std::string>& /*invalidatedProps*/) {
                onPropertiesChanged(interfaceName, changedProps);
            });

        const sdbus::InterfaceName dbusIface{kDbusInterface};
        nameOwnerProxy_->uponSignal("NameOwnerChanged")
            .onInterface(dbusIface)
            .call([this](const std::string& name, const std::string& /*oldOwner*/,
                         const std::string& /*newOwner*/) { onNameOwnerChanged(name); });
    }

    // Every signal handler body starts with the accepting_ teardown gate.
    void onDeviceListSignal() {
        if (!accepting_.load()) return;
        publishChanged();
    }

    void onDeviceRequestSignal(const Dict& d) {
        if (!accepting_.load()) return;
        try {
            publishUpdateRequest(d);
        } catch (const std::exception& e) {
            spdlog::debug("fwupd: DeviceRequest handler failed: {}", e.what());
        }
    }

    void onNameOwnerChanged(const std::string& name) {
        if (!accepting_.load()) return;
        if (name == fwupd::kBusName) publishChanged();
    }

    // V5 (spec §5.4): forwarded to our own install()'s progress sink ⇔ one of
    // our installs is active — never while idle, never after teardown, never
    // for another interface's properties (defense in depth: only our object
    // path is watched, but the interface name is still worth checking).
    void onPropertiesChanged(const std::string& interfaceName, const Dict& changed) {
        if (!accepting_.load()) return;
        if (interfaceName != fwupd::kInterface) return;
        std::scoped_lock lk(progressMutex_);
        // Null pointer = no install of ours in flight; empty std::function = a
        // caller that opted out of progress. Both ⇒ ignore (V5).
        if (progressSink_ == nullptr || !*progressSink_) return;
        const auto percentage = get<std::uint32_t>(changed, "Percentage");
        const int percent = (!percentage || *percentage > 100) ? -1 : static_cast<int>(*percentage);
        const auto status = get<std::uint32_t>(changed, "Status").value_or(0U);
        // Invoked while holding progressMutex_ — that is what lets install()'s
        // Gate safely block until an in-flight callback finishes before clearing
        // the sink. Contract: a caller's ProgressReporter must not block or
        // re-enter anything that takes progressMutex_ (nothing else does).
        (*progressSink_)(
            runtime::ProgressUpdate{.percent = percent, .stage = fwupd::statusName(status)});
    }

    void publishChanged() {
        if (!accepting_.load()) return;
        bus_.publish(core::UpdatesChangedEvent{});
    }

    void publishUpdateRequest(const Dict& d) {
        if (!accepting_.load()) return;
        core::UpdateRequestEvent event{
            .providerId = "fwupd",
            .deviceId = get<std::string>(d, "DeviceId").value_or(""),
            .kind = requestKindToString(get<std::uint32_t>(d, "RequestKind").value_or(0)),
            .message = get<std::string>(d, "UpdateMessage").value_or(""),
        };
        bus_.publish(event);
    }

    // Best-effort notices (spec §8.1): a failure here must not flip
    // `available` — the DaemonVersion probe already succeeded by the time
    // this runs, so a GetHistory/GetRemotes hiccup just costs a notice.
    void collectNotices(std::vector<std::string>& notices) const {
        try {
            std::vector<Dict> history;
            proxy_->callMethod("GetHistory")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .storeResultsTo(history);
            if (auto n = failedHistoryNotice(history)) notices.push_back(*n);
        } catch (const std::exception& e) {
            spdlog::debug("fwupd: GetHistory notice query failed: {}", e.what());
        }
        try {
            std::vector<Dict> remotes;
            proxy_->callMethod("GetRemotes")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .storeResultsTo(remotes);
            const auto now =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
            for (const auto& r : remotes) {
                if (auto n = staleMetadataNotice(r, now)) notices.push_back(*n);
            }
        } catch (const std::exception& e) {
            spdlog::debug("fwupd: GetRemotes notice query failed: {}", e.what());
        }
    }

    // ---- T8 install lifecycle (spec §5.5) helpers ----
    // Preflight: fresh GetDevices scan for one device — ⊥ trust the caller's
    // snapshot. "Not found" is a normal (non-throwing) outcome; a transport
    // failure propagates to install()'s outer catch (→ Io via mapError), it
    // is deliberately NOT folded into nullopt here.
    std::optional<fwupd::ParsedDevice> fetchDevice(const std::string& deviceId) {
        std::vector<Dict> deviceDicts;
        proxy_->callMethod("GetDevices")
            .onInterface(sdbus::InterfaceName{fwupd::kInterface})
            .storeResultsTo(deviceDicts);
        for (const auto& d : deviceDicts) {
            auto parsed = fwupd::parseDevice(d);
            if (parsed && parsed->deviceId == deviceId) return parsed;
        }
        return std::nullopt;
    }

    // Preflight: fresh GetUpgrades(deviceId), matched by (remoteId, checksum)
    // — never by version (spec §2). Reuses the same tolerant fetchUpgrades()
    // enumerate() relies on; a query failure fails closed as "not found" (⇒
    // caller's Conflict), consistent with "trust nothing from before this call".
    std::optional<core::ReleaseInfo> fetchRelease(const std::string& deviceId,
                                                  const core::ReleaseRef& ref) {
        const auto outcome = fetchUpgrades(*proxy_, deviceId, {});
        if (outcome.queryError) {
            // Fails closed to "not found" (⇒ caller's Conflict). The Conflict
            // message says "release changed", so log the real cause here — a
            // transport fault and a genuinely-vanished release look identical
            // to the caller otherwise.
            spdlog::debug("fwupd: preflight GetUpgrades for {} failed ({}) — treating as vanished",
                          deviceId, *outcome.queryError);
            return std::nullopt;
        }
        for (const auto& r : outcome.releases)
            if (r.remoteId == ref.remoteId && r.checksum == ref.checksum) return r;
        return std::nullopt;
    }

    std::vector<RemoteRef> fetchRemotes() {
        std::vector<Dict> remoteDicts;
        proxy_->callMethod("GetRemotes")
            .onInterface(sdbus::InterfaceName{fwupd::kInterface})
            .storeResultsTo(remoteDicts);
        std::vector<RemoteRef> remotes;
        remotes.reserve(remoteDicts.size());
        for (const auto& rd : remoteDicts) remotes.push_back(parseRemoteRef(rd));
        return remotes;
    }

    // Finalizing (spec §5.5): the Install() reply is not proof of anything —
    // GetResults is authoritative. `NothingToDo` (daemon recorded no result
    // for this device) keeps the reply-based Completed disposition rather
    // than failing the whole install(); any other GetResults error rethrows
    // to install()'s outer catch (→ mapped Result).
    core::Result<core::InstallOutcome> finalize(const std::string& deviceId,
                                                const fwupd::ParsedDevice& preDevice) {
        std::uint32_t state = fwupd::kUpdateStateSuccess;
        try {
            Dict results;
            proxy_->callMethod("GetResults")
                .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                .withArguments(deviceId)
                .storeResultsTo(results);
            state = get<std::uint32_t>(results, "UpdateState").value_or(fwupd::kUpdateStateSuccess);
        } catch (const sdbus::Error& e) {
            if (!fwupd::isNothingToDo(std::string{e.getName()})) throw;
        }
        const auto disposition = fwupd::dispositionFromUpdateState(state);
        const auto freshDevice = fetchDevice(deviceId);
        const std::string& newVersion = freshDevice ? freshDevice->version : preDevice.version;
        std::optional<std::string> observedVersion;
        if (newVersion != preDevice.version) observedVersion = newVersion;
        const bool needsReboot = disposition == core::InstallDisposition::NeedsReboot ||
                                 (freshDevice ? freshDevice->facts.needsRebootAfterUpdate
                                              : preDevice.facts.needsRebootAfterUpdate);
        std::string message;
        if (needsReboot)
            message = "reboot required to apply " + observedVersion.value_or(newVersion);
        else if (disposition == core::InstallDisposition::Scheduled)
            message = "scheduled for next boot";
        else
            message = "installed " + observedVersion.value_or(newVersion);
        return core::InstallOutcome{
            .disposition = disposition,
            .needsReboot = needsReboot,
            .observedVersion = observedVersion,
            .message = message,
        };
    }

    runtime::EventBus& bus_;
    std::unique_ptr<sdbus::IConnection> connection_;
    std::unique_ptr<sdbus::IProxy> proxy_;
    std::unique_ptr<sdbus::IProxy> nameOwnerProxy_;
    std::string initError_;
    std::atomic<bool> accepting_{true};
    std::atomic<bool> installing_{false};                // V5 gate — wired by T8
    std::mutex progressMutex_;                           // guards progressSink_ (T8)
    runtime::ProgressReporter* progressSink_ = nullptr;  // non-null ⇔ our install is active
};

FwupdUpdateProvider::FwupdUpdateProvider(runtime::EventBus& bus, Config cfg)
    : impl_(std::make_unique<Impl>(bus, cfg)) {}

FwupdUpdateProvider::~FwupdUpdateProvider() = default;

std::string FwupdUpdateProvider::providerId() const {
    return "fwupd";
}

pal::UpdateProviderCaps FwupdUpdateProvider::capabilities() const {
    return pal::UpdateProviderCaps::Query | pal::UpdateProviderCaps::Install;
}

core::ProviderAvailability FwupdUpdateProvider::availability() const {
    return impl_->availability();
}

core::Result<std::vector<core::UpdateCandidate>> FwupdUpdateProvider::enumerate() {
    return impl_->enumerate();
}

core::Result<std::vector<core::PendingAction>> FwupdUpdateProvider::pendingActions() {
    return impl_->pendingActions();
}

core::Result<core::InstallOutcome> FwupdUpdateProvider::install(
    const std::string& candidateId, const core::ReleaseRef& release,
    runtime::ProgressReporter progress) {
    auto* impl = impl_.get();
    if (!impl || !impl->proxy_) return core::makeError(core::Error::Code::Io, "fwupd unavailable");
    bool expected = false;
    if (!impl->installing_.compare_exchange_strong(expected, true))
        return core::makeError(core::Error::Code::Busy, "another update is in progress");
    // RAII: clear installing_ + progress sink on every exit path.
    struct Gate {
        Impl* i;
        explicit Gate(Impl* impl) : i(impl) {}
        Gate(const Gate&) = delete;
        Gate& operator=(const Gate&) = delete;
        Gate(Gate&&) = delete;
        Gate& operator=(Gate&&) = delete;
        ~Gate() {
            {
                std::scoped_lock lk(i->progressMutex_);
                i->progressSink_ = nullptr;
            }
            i->installing_.store(false);
        }
    } gate{impl};

    try {
        // ---- Preflight (spec §5.5): fresh queries, ⊥ trust the UI snapshot ----
        const auto device = impl->fetchDevice(candidateId);  // fresh GetDevices scan
        if (!device || !device->facts.updatable)
            return core::makeError(core::Error::Code::Conflict,
                                   "device changed since refresh — refresh & retry");
        const auto fresh = impl->fetchRelease(candidateId, release);  // fresh GetUpgrades,
        if (!fresh)                                                   // match (remoteId,checksum)
            return core::makeError(core::Error::Code::Conflict,
                                   "release changed since refresh — refresh & retry");
        // ---- Resolving (M2, T4 contract) ----
        const auto remotes = impl->fetchRemotes();
        auto cab = resolveAndOpenCab(fresh->locations, remotes, fresh->remoteId, fresh->sizeBytes);
        if (!cab) return tl::unexpected(cab.error());
        // ---- Installing: async call, gated progress (V5) ----
        {
            std::scoped_lock lk(impl->progressMutex_);
            impl->progressSink_ = &progress;  // PropertiesChanged handler forwards
        }  // Status/Percentage ⇔ sink non-null
        const auto timeout = std::chrono::seconds(
            std::max<std::uint32_t>(fresh->installDurationSec.value_or(0) * 2, 600));
        std::map<std::string, sdbus::Variant> options;
        options["reason"] = sdbus::Variant{std::string{"device-manager update"}};
        // sdbus::UnixFd(int) dups the fd on construction (verified:
        // sdbus-c++/Types.h — "explicit UnixFd(int fd) : fd_(checkedDup(fd))")
        // — cab stays alive (owns the original) until this scope ends either way
        // (M2 fd-ownership rule).
        auto call = impl->proxy_->callMethodAsync("Install")
                        .onInterface(sdbus::InterfaceName{fwupd::kInterface})
                        .withArguments(candidateId, sdbus::UnixFd{cab->fd.get()}, options)
                        .getResultAsFuture<>();
        if (call.wait_for(timeout) != std::future_status::ready)
            return core::makeError(core::Error::Code::Io,
                                   "install timed out — state reconciled on next refresh");
        call.get();  // throws sdbus::Error on daemon-reported failure
        // ---- Finalizing: reply ≠ proof; GetResults is authoritative (§5.4) ----
        return impl->finalize(candidateId, *device);
    } catch (const sdbus::Error& e) {
        return tl::unexpected(
            fwupd::mapError(std::string{e.getName()}, std::string{e.getMessage()}));
    } catch (const std::exception& e) {
        return core::makeError(core::Error::Code::Io, std::string{"install failed: "} + e.what());
    }
}

}  // namespace devmgr::platform_linux
