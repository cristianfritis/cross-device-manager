#include "devmgr/app/updates_vm.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace devmgr::app {
namespace {

constexpr std::string_view kInstallTaskPrefix = "install-update:";
constexpr const char* kPlaceholderRow = "(no updates available)";
constexpr const char* kRemoteOnlyGuidance = "external download required — run `fwupdmgr update`";

// Fixed-column update row (byte-frozen, shared with T11/T12 — V3: the emitted
// format IS the parity contract, single-sourced here).
std::string formatRow(const std::string& provider, const std::string& name,
                      const std::string& current, const std::string& candidate,
                      const std::string& marker) {
    static constexpr std::size_t kRowBufferSize = 128;
    std::array<char, kRowBufferSize> row{};
    // snprintf reproduces the fixed printf-style column alignment shared
    // byte-for-byte with both UIs (ModulesVM D-6 idiom).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(row.data(), row.size(), "%-6s %-30.30s %-12.12s -> %-12.12s %s", provider.c_str(),
                  name.c_str(), current.c_str(), candidate.c_str(), marker.c_str());
    return {row.data()};
}

bool hasLocalRelease(const core::UpdateCandidate& c) {
    return std::ranges::any_of(c.releases, &core::ReleaseInfo::localCab);
}

bool remoteOnly(const core::UpdateCandidate& c) {
    return !c.releases.empty() && !hasLocalRelease(c);
}

// Marker precedence: "reboot required" (durable pending state for THIS device,
// M1 — never derived from the candidate itself, V4) > "external download"
// (remote-only releases ⇒ verb disabled, V1) > "".
std::string markerFor(const core::UpdateCandidate& c, const std::string& providerId,
                      const std::vector<core::PendingAction>& pending) {
    const bool rebootPending = std::ranges::any_of(pending, [&](const core::PendingAction& p) {
        return p.providerId == providerId && p.deviceId == c.id &&
               p.disposition == core::InstallDisposition::NeedsReboot;
    });
    if (rebootPending) return "reboot required";
    if (remoteOnly(c)) return "external download";
    return "";
}

// Confirm-modal text (spec §9): version delta + needs-reboot warn + duration.
std::string confirmTextFor(const core::UpdateCandidate& c, const core::ReleaseInfo& release) {
    std::string t = "Install " + c.displayName + ": " + c.currentVersion + " → " + release.version;
    if (c.facts.needsRebootAfterUpdate) t += " — reboot required after install";
    if (release.installDurationSec) t += " (~" + std::to_string(*release.installDurationSec) + "s)";
    return t;
}

// Secure Boot line — ModulesVM's exact wording (Phase 5 reuse per spec §8.3).
std::string secureBootLine(const std::optional<pal::ISystemInfo::Info>& info) {
    if (!info) return "Secure Boot: unknown";
    std::string b = std::string("Secure Boot: ") + (info->secureBoot ? "ON" : "off") +
                    " · Lockdown: " + info->lockdownMode;
    if (info->secureBoot || info->lockdownMode != "none")
        b += " — unsigned modules will be rejected";
    return b;
}

// "<version>" when available, "unavailable[: reason]" otherwise.
std::string availabilityCell(const core::UpdateProviderState& s) {
    if (s.availability.available) return s.availability.version.value_or("available");
    std::string cell = "unavailable";
    if (s.availability.error) cell += ": " + s.availability.error->message;
    return cell;
}

void appendReleaseLines(std::vector<std::string>& out, const core::UpdateCandidate& c) {
    for (const auto& r : c.releases) {
        std::string line = "Release " + r.version + (r.localCab ? " [local]" : " [remote]");
        if (!r.summary.empty()) line += " — " + r.summary;
        out.push_back(std::move(line));
    }
    if (remoteOnly(c)) out.emplace_back(kRemoteOnlyGuidance);
}

// Restores the highlighted candidate by (providerId, id) after a rebuild, then clamps.
int restoreSelection(const std::vector<std::optional<std::pair<std::size_t, std::size_t>>>& refs,
                     const std::vector<core::UpdateProviderState>& snapshot,
                     const std::optional<std::pair<std::string, std::string>>& keep, int current,
                     int rowCount) {
    if (keep) {
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const auto& ref = refs[i];
            if (!ref) continue;
            const auto& state = snapshot[ref->first];
            if (state.providerId == keep->first &&
                state.candidates[ref->second].id == keep->second) {
                current = static_cast<int>(i);
                break;
            }
        }
    }
    return std::clamp(current, 0, rowCount - 1);
}

}  // namespace

UpdatesVM::UpdatesVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher)
    : facade_(facade), bus_(bus), dispatcher_(dispatcher) {
    subRefreshed_ = bus_.subscribe<core::UpdatesRefreshedEvent>(
        [this](const core::UpdatesRefreshedEvent&) { queueRebuild(); });
    subChanged_ = bus_.subscribe<core::UpdatesChangedEvent>(
        [this](const core::UpdatesChangedEvent&) { queueRefresh(); });
    subRequest_ = bus_.subscribe<core::UpdateRequestEvent>(
        [this](const core::UpdateRequestEvent& e) { onRequest(e); });
    subProgress_ = bus_.subscribe<core::TaskProgressEvent>(
        [this](const core::TaskProgressEvent& e) { onProgress(e); });
    subCompleted_ = bus_.subscribe<core::TaskCompletedEvent>(
        [this](const core::TaskCompletedEvent& e) { onCompleted(e); });
}

UpdatesVM::~UpdatesVM() {
    // Clear the alive token FIRST (ModulesVM i-2 contract): closures already
    // sitting in a queuing dispatcher (FTXUI/Qt, T11/T12) check it and no-op
    // instead of touching a dead VM. Safe without a mutex: the destructor and
    // IUiDispatcher::post()'s execution both happen on the UI thread.
    alive_->store(false);
    // Future custody (ApplicationFacade::refreshUpdates contract): the worker
    // captures the facade, so the last outstanding handle must be waited
    // before the composition root tears the facade down — that wait is here.
    if (lastRefresh_.valid()) lastRefresh_.wait();
}

void UpdatesVM::queueRebuild() {
    if (rebuildQueued_.exchange(true)) return;  // coalesce bursts
    auto alive = alive_;
    dispatcher_.post([this, alive] {
        if (!alive->load()) return;  // VM died before this ran
        rebuildQueued_.store(false);
        rebuild();
    });
}

void UpdatesVM::queueRefresh() {
    if (refreshQueued_.exchange(true)) return;  // coalesce provider-signal bursts
    auto alive = alive_;
    dispatcher_.post([this, alive] {
        if (!alive->load()) return;  // VM died before this ran
        refreshQueued_.store(false);
        // Custody: wait out the previous worker before dropping its handle —
        // coalescing bounds this to at most one in flight, and the read-side
        // enumerate pass is short, so the UI-thread wait is bounded.
        if (lastRefresh_.valid()) lastRefresh_.wait();
        lastRefresh_ = facade_.refreshUpdates();
    });
}

void UpdatesVM::postWake() {
    // Empty post: its only job is to wake the UI loop so banners/progress
    // strings (read every frame) repaint. No VM state touched — no token needed.
    dispatcher_.post([] {});
}

void UpdatesVM::onRequest(const core::UpdateRequestEvent& e) {
    {
        std::scoped_lock lock(textMutex_);
        requestBanner_ = "device request (" + e.kind + ") " + e.deviceId + ": " + e.message;
    }
    postWake();
}

void UpdatesVM::onProgress(const core::TaskProgressEvent& e) {
    if (!e.taskId.starts_with(kInstallTaskPrefix)) return;
    std::string text = "installing " + e.taskId.substr(kInstallTaskPrefix.size()) + ": ";
    if (e.percent >= 0) text += std::to_string(e.percent) + "% ";
    {
        std::scoped_lock lock(textMutex_);
        // Percent-only frames carry no named stage (decoded as "unknown");
        // retain the last named stage instead of flashing "unknown" — see
        // lastNamedStage_ in the header.
        std::string stage = e.stage;
        if (stage.empty() || stage == "unknown") {
            if (!lastNamedStage_.empty()) stage = lastNamedStage_;
        } else {
            lastNamedStage_ = stage;
        }
        progressText_ = text + stage;
    }
    postWake();
}

void UpdatesVM::onCompleted(const core::TaskCompletedEvent& e) {
    if (!e.taskId.starts_with(kInstallTaskPrefix)) return;
    {
        std::scoped_lock lock(textMutex_);
        progressText_.clear();
        lastNamedStage_.clear();  // next install must not inherit a stale stage
        // requestBanner_ deliberately untouched: a "post"-kind DeviceRequest
        // must outlive the operation, so clearing is EXPLICIT-DISMISS ONLY —
        // sanctioned narrowing of spec §9 "dismiss | resolution" (T13 ledger).
    }
    postWake();
}

void UpdatesVM::setRebuildHooks(std::function<void()> before, std::function<void()> after) {
    beforeRebuild_ = std::move(before);
    afterRebuild_ = std::move(after);
}

void UpdatesVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    const auto keep = selectedKey();
    snapshot_ = facade_.updatesSnapshot();
    pending_ = facade_.pendingUpdateActions();
    rows_.clear();
    rowRefs_.clear();
    for (std::size_t pi = 0; pi < snapshot_.size(); ++pi) {
        const auto& state = snapshot_[pi];
        for (std::size_t ci = 0; ci < state.candidates.size(); ++ci) {
            const auto& c = state.candidates[ci];
            rows_.push_back(formatRow(state.providerId, c.displayName, c.currentVersion,
                                      c.candidateVersion.value_or("-"),
                                      markerFor(c, state.providerId, pending_)));
            rowRefs_.emplace_back(std::pair{pi, ci});
        }
    }
    if (rows_.empty()) {
        rows_.emplace_back(kPlaceholderRow);
        rowRefs_.emplace_back(std::nullopt);
    }
    selected_ =
        restoreSelection(rowRefs_, snapshot_, keep, selected_, static_cast<int>(rows_.size()));
    if (afterRebuild_) afterRebuild_();
}

const core::UpdateCandidate* UpdatesVM::selectedCandidate() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowRefs_.size())) return nullptr;
    const auto& ref = rowRefs_[static_cast<std::size_t>(selected_)];
    if (!ref) return nullptr;
    return &snapshot_[ref->first].candidates[ref->second];
}

std::optional<UpdateRowState> UpdatesVM::stateForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowRefs_.size())) return std::nullopt;
    const auto& ref = rowRefs_[static_cast<std::size_t>(row)];
    if (!ref) return std::nullopt;  // placeholder row
    const auto& state = snapshot_[ref->first];
    if (state.refreshError || state.availability.error) return UpdateRowState::Error;
    return state.candidates[ref->second].candidateVersion ? UpdateRowState::Available
                                                          : UpdateRowState::UpToDate;
}

std::optional<std::pair<std::string, std::string>> UpdatesVM::selectedKey() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowRefs_.size())) return std::nullopt;
    const auto& ref = rowRefs_[static_cast<std::size_t>(selected_)];
    if (!ref) return std::nullopt;
    return std::pair{snapshot_[ref->first].providerId,
                     snapshot_[ref->first].candidates[ref->second].id};
}

std::string UpdatesVM::banner() const {
    std::string b;
    for (const auto& s : facade_.updatesSnapshot()) {
        if (!b.empty()) b += " | ";
        b += s.providerId + " " + availabilityCell(s);
        // Per-provider notices (spec §8.3, e.g. stale-metadata / failed-history
        // hints) apply whether the provider is available or not — appended
        // unconditionally, in provider order, using secureBootLine's mid-cell
        // separator style.
        for (const auto& notice : s.availability.notices) b += " · " + notice;
    }
    if (facade_.rebootPendingEffective()) {
        if (!b.empty()) b += " | ";
        b += "reboot required";
    }
    const std::string sb = secureBootLine(facade_.systemInfo());
    return b.empty() ? sb : b + " | " + sb;
}

std::string UpdatesVM::requestBanner() const {
    std::scoped_lock lock(textMutex_);
    return requestBanner_;
}

void UpdatesVM::dismissRequest() {
    std::scoped_lock lock(textMutex_);
    requestBanner_.clear();
}

std::string UpdatesVM::installProgressText() const {
    std::scoped_lock lock(textMutex_);
    return progressText_;
}

std::vector<std::string> UpdatesVM::detailLines() const {
    const auto* c = selectedCandidate();
    if (c == nullptr) return {"(no update selected)"};
    std::vector<std::string> out;
    out.push_back("Device:    " + c->displayName);
    out.push_back("Provider:  " + c->providerId);
    out.push_back("Current:   " + c->currentVersion);
    if (c->candidateVersion) out.push_back("Candidate: " + *c->candidateVersion);
    out.push_back(std::string("Updatable: ") + (c->facts.updatable ? "yes" : "no"));
    if (c->facts.needsRebootAfterUpdate) out.emplace_back("Reboot required after update");
    for (const auto& [key, value] : c->details) {
        std::string line = key;
        line += ": ";
        line += value;
        out.push_back(std::move(line));
    }
    appendReleaseLines(out, *c);
    return out;
}

std::optional<UpdatesVM::InstallArgs> UpdatesVM::selectedInstall() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowRefs_.size())) return std::nullopt;
    const auto& ref = rowRefs_[static_cast<std::size_t>(selected_)];
    if (!ref) return std::nullopt;  // placeholder row: never actionable (V1)
    const auto& state = snapshot_[ref->first];
    const auto& c = state.candidates[ref->second];
    // V1 gate: verb enabled only for an updatable candidate with a locally
    // resolvable release. Status-only providers (dkms) publish updatable=false
    // for every row, and the facade re-checks caps∋Install (defense in depth).
    if (!c.facts.updatable) return std::nullopt;
    const auto rel = std::ranges::find_if(c.releases, &core::ReleaseInfo::localCab);
    if (rel == c.releases.end()) return std::nullopt;
    return InstallArgs{.providerId = state.providerId,
                       .candidateId = c.id,
                       .release = rel->ref(),
                       .confirmText = confirmTextFor(c, *rel)};
}

}  // namespace devmgr::app
