#include "devmgr/app/snapshots_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <utility>

#include "devmgr/core/snapshot_history.hpp"
#include "devmgr/core/snapshot_presentation.hpp"

namespace devmgr::app {
namespace {

constexpr const char* kPlaceholderRow = "(no snapshots)";
// Depth indent for the chain view, capped so a long chain cannot push the
// fixed row columns off a narrow terminal (docs/DESIGN.md §3.2).
constexpr std::size_t kIndentPerLevel = 2;
constexpr std::size_t kMaxIndentLevels = 4;

std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

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

// Filter haystack (snapshot-ui spec): id, trigger and reason, lowercased.
std::string filterHaystack(const core::SnapshotMeta& m) {
    return toLower(m.id + " " + to_string(m.trigger) + " " + reasonCell(m));
}

// Indent level per row for the chain view. core::buildSnapshotChain measures
// depth from the chain START (oldest = 0), but rows are rendered newest-first,
// so indenting by that directly would push the NEWEST rows deepest and — past
// the cap — flatten every recent row to the same indent, killing the
// distinction exactly where the eye lands first. Indent from the chain TIP
// instead: the newest sits at the left margin and each ancestor steps right,
// which also matches the top-down reading order ("this, and what it came
// from"). The cap then only degrades the oldest ancestors.
// A chainStart row ends its group: the next row begins a new chain, back at
// the margin, which is how a pruned ancestor reads as an absent chain start.
std::vector<std::size_t> chainIndents(const std::vector<core::SnapshotChainRow>& chain) {
    std::vector<std::size_t> indents(chain.size(), 0);
    std::size_t tipDepth = 0;
    bool groupOpen = false;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (!groupOpen) {
            tipDepth = chain[i].depth;
            groupOpen = true;
        }
        // Guard the subtraction: depth is only monotonic within a group, and a
        // corrupt store could in principle order rows otherwise.
        indents[i] = tipDepth > chain[i].depth ? tipDepth - chain[i].depth : 0;
        if (chain[i].chainStart) groupOpen = false;
    }
    return indents;
}

// HEAD and last-good snapshot ids from the chain (snapshot-history spec), each
// "" when the chain has no such row. Derived from the same core::buildSnapshotChain
// the history markers and preview use, so the list, markers and preview can
// never disagree about which row is which. `chain` is index-aligned with `metas`.
struct HeadAndLastGood {
    std::string head;
    std::string lastGood;
};
HeadAndLastGood headAndLastGood(const std::vector<core::SnapshotChainRow>& chain,
                                const std::vector<core::SnapshotMeta>& metas) {
    HeadAndLastGood ids;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (chain[i].head) ids.head = metas[i].id;
        if (chain[i].lastGood) ids.lastGood = metas[i].id;
    }
    return ids;
}

// Which snapshot is selected, which is current HEAD, which is last good
// (snapshot-ui spec). HEAD and last-good come from the same chain builder the
// history view uses, so the preview and the list cannot disagree about which
// row is which.
std::vector<std::string> previewContextLines(const std::vector<core::SnapshotMeta>& metas,
                                             const std::string& previewId) {
    const auto chain = core::buildSnapshotChain(metas);
    std::string headId;
    std::string goodId;
    std::string createdCell;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        if (chain[i].head) headId = metas[i].id;
        if (chain[i].lastGood) goodId = metas[i].id;
        if (metas[i].id == previewId) createdCell = localDateTime(metas[i].createdAtUtc);
    }
    const auto mark = [&previewId](const std::string& id) {
        return !id.empty() && id == previewId ? " — this snapshot" : "";
    };
    std::vector<std::string> out;
    out.push_back("Selected:     " + core::snapshotShortId(previewId) +
                  (createdCell.empty() ? "" : " (created " + createdCell + ")"));
    out.push_back("Current HEAD: " + (headId.empty() ? "(none)" : core::snapshotShortId(headId)) +
                  mark(headId));
    out.push_back(
        "Last good:    " +
        (goodId.empty() ? "(none — no healthy snapshot)" : core::snapshotShortId(goodId)) +
        mark(goodId));
    return out;
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
    // The diff landed: drop the loading state and let the open surface redraw.
    // Not coalesced — one preview is in flight at a time — but marshalled to
    // the UI thread like every other model mutation.
    subDiff_ = bus_.subscribe<core::SnapshotDiffRefreshedEvent>(
        [this](const core::SnapshotDiffRefreshedEvent&) {
            auto alive = alive_;
            dispatcher_.post([this, alive] {
                if (!alive->load()) return;
                previewPending_ = false;
                if (diffReady_) diffReady_();
            });
        });
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
    if (lastDiff_.valid()) lastDiff_.wait();
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

void SnapshotsVM::setDiffReadyHook(std::function<void()> hook) {
    diffReady_ = std::move(hook);
}

void SnapshotsVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    const auto keep = selectedId();
    metas_ = facade_.snapshots();
    rows_.clear();
    rowRefs_.clear();
    // Chain over the FULL list, so HEAD/last-good stay true regardless of what
    // the filter hides (see the header note). Built unconditionally now — the
    // row-marker predicates need HEAD/last-good even with the history view off —
    // and reused for the indent path below when the view is on.
    const auto chain = core::buildSnapshotChain(metas_);
    const auto ids = headAndLastGood(chain, metas_);
    headId_ = ids.head;
    lastGoodId_ = ids.lastGood;
    const auto indents = historyView_ ? chainIndents(chain) : std::vector<std::size_t>{};
    const std::string needle = toLower(filter_);
    for (std::size_t i = 0; i < metas_.size(); ++i) {
        if (!needle.empty() && filterHaystack(metas_[i]).find(needle) == std::string::npos)
            continue;
        std::string row;
        if (historyView_) {
            const auto& chainRow = chain[i];
            row.append(std::min(indents[i], kMaxIndentLevels) * kIndentPerLevel, ' ');
            row += formatRow(metas_[i]);
            row += core::chainMarkers(chainRow);
        } else {
            row = formatRow(metas_[i]);
        }
        rows_.push_back(std::move(row));
        rowRefs_.emplace_back(i);
    }
    if (rows_.empty()) {
        // Name the filter that hid everything and keep the clear-filter path
        // discoverable (docs/DESIGN.md §5.1); an unfiltered empty store is a
        // different state and says so.
        rows_.push_back(needle.empty() ? kPlaceholderRow
                                       : "No snapshots match \"" + filter_ + "\"");
        rowRefs_.emplace_back(std::nullopt);
    }
    selected_ = restoreSelection(rowRefs_, metas_, keep, selected_, static_cast<int>(rows_.size()));
    if (afterRebuild_) afterRebuild_();
}

void SnapshotsVM::setFilter(std::string filter) {
    filter_ = std::move(filter);
    rebuild();
}

void SnapshotsVM::setHistoryView(bool on) {
    historyView_ = on;
    rebuild();
}

const core::SnapshotMeta* SnapshotsVM::metaForRow(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowRefs_.size())) return nullptr;
    const auto& ref = rowRefs_[static_cast<std::size_t>(row)];
    if (!ref) return nullptr;
    return &metas_[*ref];
}

const core::SnapshotMeta* SnapshotsVM::selectedMeta() const {
    return metaForRow(selected_);
}

std::optional<std::string> SnapshotsVM::selectedId() const {
    const auto* m = selectedMeta();
    if (m == nullptr) return std::nullopt;
    return m->id;
}

std::optional<core::SnapshotHealth> SnapshotsVM::healthForRow(int row) const {
    const auto* m = metaForRow(row);
    if (m == nullptr) return std::nullopt;
    return m->health;
}

bool SnapshotsVM::isHeadRow(int row) const {
    const auto* m = metaForRow(row);
    return m != nullptr && !headId_.empty() && m->id == headId_;
}

bool SnapshotsVM::isLastGoodRow(int row) const {
    const auto* m = metaForRow(row);
    return m != nullptr && !lastGoodId_.empty() && m->id == lastGoodId_;
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

std::shared_future<void> SnapshotsVM::requestPreview(std::string id) {
    previewId_ = id;
    previewPending_ = true;
    // Custody, same rule as queueRefresh(): the worker captures the facade, so
    // the previous handle is waited before its future is dropped. Bounded —
    // one preview is in flight at a time because the surface is modal.
    if (lastDiff_.valid()) lastDiff_.wait();
    // Empty target = live system state: what the machine looks like right now
    // (design decision 1).
    lastDiff_ = facade_.refreshSnapshotDiff(std::move(id), "").share();
    return lastDiff_;
}

std::vector<std::string> SnapshotsVM::diffLines() const {
    if (!previewId_) return {"(no diff requested)"};
    if (previewPending_) return {"Computing differences..."};
    const auto diff = facade_.snapshotDiff();
    // Cleared cache after a completed fetch means the fetch failed; the reason
    // reached the user through ErrorEvent, so this states the consequence
    // rather than repeating a raw error (docs/DESIGN.md §6).
    if (!diff) return {"Differences are unavailable for this snapshot."};
    return core::diffLines(*diff);
}

std::vector<std::string> SnapshotsVM::previewLines() const {
    if (!previewId_) return {"Select a snapshot to preview its restore."};
    std::vector<std::string> out;
    out.push_back("Restore snapshot " + core::snapshotShortId(*previewId_) + "?");
    for (auto& line : previewContextLines(metas_, *previewId_)) out.push_back(std::move(line));
    out.emplace_back("");
    if (previewPending_) {
        out.emplace_back("Computing what will change...");
        return out;
    }
    for (auto& line : core::restorePreviewChangeLines(facade_.snapshotDiff()))
        out.push_back(std::move(line));
    out.emplace_back("");
    out.emplace_back(core::restorePreviewConvergenceNote());
    return out;
}

std::vector<std::string> SnapshotsVM::restoreGuidanceLines() const {
    const auto outcome = facade_.lastRestoreOutcome();
    if (!outcome) return {};
    return core::restoreGuidanceLines(*outcome);
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
