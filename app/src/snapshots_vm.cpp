#include "devmgr/app/snapshots_vm.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <utility>

namespace devmgr::app {
namespace {

constexpr const char* kPlaceholderRow = "(no snapshots)";

// Local date-time cell (snapshot-ui spec row format). Fixed 16-char shape so
// the byte-frozen row columns stay aligned.
std::string localDateTime(std::int64_t utcSeconds) {
    const auto t = static_cast<std::time_t>(utcSeconds);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) return "?";
    static constexpr std::size_t kTimeBufferSize = 20;  // "YYYY-mm-dd HH:MM" + NUL
    std::array<char, kTimeBufferSize> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M", &local) == 0) return "?";
    return {buffer.data()};
}

// Auto snapshots read "<verb> <subject>"; manual ones read their label.
std::string reasonCell(const core::SnapshotMeta& m) {
    if (m.reason.verb.empty()) return m.reason.subject;
    if (m.reason.subject.empty()) return m.reason.verb;
    return m.reason.verb + " " + m.reason.subject;
}

// Non-Ok health renders marked (snapshot-ui spec); Ok renders empty.
std::string markerFor(const core::SnapshotMeta& m) {
    if (m.health == core::SnapshotHealth::Ok) return "";
    return to_string(m.health);
}

// Fixed-column snapshot row (byte-frozen, shared by TUI/GUI — V3: the emitted
// format IS the parity contract, single-sourced here).
std::string formatRow(const core::SnapshotMeta& m) {
    static constexpr std::size_t kRowBufferSize = 128;
    std::array<char, kRowBufferSize> row{};
    // snprintf reproduces the fixed printf-style column alignment shared
    // byte-for-byte with both UIs (ModulesVM D-6 idiom).
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(row.data(), row.size(), "%-12.12s %-16.16s %-6.6s %-30.30s %s",
                  core::snapshotShortId(m.id).c_str(), localDateTime(m.createdAtUtc).c_str(),
                  to_string(m.trigger), reasonCell(m).c_str(), markerFor(m).c_str());
    return {row.data()};
}

// Restores the highlighted snapshot by id after a rebuild, then clamps.
int restoreSelection(const std::vector<std::optional<std::size_t>>& refs,
                     const std::vector<core::SnapshotMeta>& metas,
                     const std::optional<std::string>& keep, int current, int rowCount) {
    if (keep) {
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const auto& ref = refs[i];
            if (!ref) continue;
            if (metas[ref.value()].id == *keep) {
                current = static_cast<int>(i);
                break;
            }
        }
    }
    return std::clamp(current, 0, rowCount - 1);
}

}  // namespace

SnapshotsVM::SnapshotsVM(ApplicationFacade& facade, runtime::EventBus& bus,
                         IUiDispatcher& dispatcher)
    : facade_(facade), bus_(bus), dispatcher_(dispatcher) {
    subRefreshed_ = bus_.subscribe<core::SnapshotsRefreshedEvent>(
        [this](const core::SnapshotsRefreshedEvent&) { queueRebuild(); });
    subChanged_ = bus_.subscribe<core::SnapshotsChangedEvent>(
        [this](const core::SnapshotsChangedEvent&) { queueRefresh(); });
}

SnapshotsVM::~SnapshotsVM() {
    // Clear the alive token FIRST (ModulesVM i-2 contract): closures already
    // sitting in a queuing dispatcher check it and no-op instead of touching a
    // dead VM. Safe without a mutex: the destructor and IUiDispatcher::post()'s
    // execution both happen on the UI thread.
    alive_->store(false);
    // Future custody (ApplicationFacade::refreshSnapshots contract): the last
    // outstanding handle must be waited before the composition root tears the
    // facade down — that wait is here.
    if (lastRefresh_.valid()) lastRefresh_.wait();
}

void SnapshotsVM::queueRebuild() {
    if (rebuildQueued_.exchange(true)) return;  // coalesce bursts
    auto alive = alive_;
    dispatcher_.post([this, alive] {
        if (!alive->load()) return;  // VM died before this ran
        rebuildQueued_.store(false);
        rebuild();
    });
}

void SnapshotsVM::queueRefresh() {
    if (refreshQueued_.exchange(true)) return;  // coalesce mutation bursts
    auto alive = alive_;
    dispatcher_.post([this, alive] {
        if (!alive->load()) return;  // VM died before this ran
        refreshQueued_.store(false);
        // Custody: wait out the previous worker before dropping its handle —
        // coalescing bounds this to at most one in flight, and the read-side
        // list pass is short, so the UI-thread wait is bounded.
        if (lastRefresh_.valid()) lastRefresh_.wait();
        lastRefresh_ = facade_.refreshSnapshots();
    });
}

void SnapshotsVM::setRebuildHooks(std::function<void()> before, std::function<void()> after) {
    beforeRebuild_ = std::move(before);
    afterRebuild_ = std::move(after);
}

void SnapshotsVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    const auto keep = selectedId();
    metas_ = facade_.snapshots();
    rows_.clear();
    rowRefs_.clear();
    for (std::size_t i = 0; i < metas_.size(); ++i) {
        rows_.push_back(formatRow(metas_[i]));
        rowRefs_.emplace_back(i);
    }
    if (rows_.empty()) {
        rows_.emplace_back(kPlaceholderRow);
        rowRefs_.emplace_back(std::nullopt);
    }
    selected_ = restoreSelection(rowRefs_, metas_, keep, selected_, static_cast<int>(rows_.size()));
    if (afterRebuild_) afterRebuild_();
}

const core::SnapshotMeta* SnapshotsVM::selectedMeta() const {
    if (selected_ < 0 || std::cmp_greater_equal(selected_, rowRefs_.size())) return nullptr;
    const auto& ref = rowRefs_[static_cast<std::size_t>(selected_)];
    if (!ref) return nullptr;
    return &metas_[*ref];
}

std::optional<std::string> SnapshotsVM::selectedId() const {
    const auto* m = selectedMeta();
    if (m == nullptr) return std::nullopt;
    return m->id;
}

std::string SnapshotsVM::banner() const {
    if (metas_.empty()) return "no snapshots";
    std::size_t autoCount = 0;
    std::size_t manualCount = 0;
    std::size_t unhealthy = 0;
    for (const auto& m : metas_) {
        if (m.trigger == core::SnapshotTrigger::Auto)
            ++autoCount;
        else
            ++manualCount;
        if (m.health != core::SnapshotHealth::Ok) ++unhealthy;
    }
    std::string b = std::to_string(metas_.size()) + " snapshots · " + std::to_string(autoCount) +
                    " auto · " + std::to_string(manualCount) + " manual";
    if (unhealthy > 0) b += " · " + std::to_string(unhealthy) + " unhealthy";
    return b;
}

std::vector<std::string> SnapshotsVM::detailLines() const {
    const auto* m = selectedMeta();
    if (m == nullptr) return {"(no snapshot selected)"};
    std::vector<std::string> out;
    out.push_back("Id:      " + m->id);
    out.push_back("Parent:  " + m->parent.value_or("(none)"));
    out.push_back("Created: " + localDateTime(m->createdAtUtc));
    out.push_back(std::string("Trigger: ") + to_string(m->trigger));
    out.push_back("Reason:  " + reasonCell(*m));
    out.push_back("Payload: " + std::to_string(m->entryCount) + " entries, " +
                  std::to_string(m->modprobeFileCount) + " modprobe files");
    if (m->health != core::SnapshotHealth::Ok)
        out.push_back(std::string("Health:  ") + to_string(m->health) +
                      " — restore disabled for this snapshot");
    return out;
}

std::optional<SnapshotsVM::RestoreArgs> SnapshotsVM::selectedRestore() const {
    const auto* m = selectedMeta();
    if (m == nullptr) return std::nullopt;
    // Corrupt/unsupported refuse restore locally (snapshot-ui spec); the store
    // re-refuses authoritatively.
    if (m->health != core::SnapshotHealth::Ok) return std::nullopt;
    return RestoreArgs{
        .id = m->id,
        .confirmText = "Restore " + core::snapshotShortId(m->id) +
                       "? Current state is saved as a safety snapshot first; devices and "
                       "modules re-converge and the critical-device guard may refuse "
                       "items (reported per item)."};
}

std::optional<SnapshotsVM::DeleteArgs> SnapshotsVM::selectedDelete() const {
    const auto* m = selectedMeta();
    if (m == nullptr) return std::nullopt;
    // Unsupported (newer formatVersion) refuses delete locally too — the store
    // refuses to touch a file it cannot fully parse.
    if (m->health == core::SnapshotHealth::Unsupported) return std::nullopt;
    return DeleteArgs{.id = m->id,
                      .confirmText = "Delete snapshot " + core::snapshotShortId(m->id) +
                                     "? This cannot be undone."};
}

}  // namespace devmgr::app
