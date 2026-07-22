#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Toolkit-agnostic Snapshots view model (snapshot-ui spec): row/detail/banner
// formatting single-sourced here (byte-frozen parity, V3 house pattern),
// coalesced refresh/rebuild through the dispatcher, UpdatesVM alive-token
// teardown contract verbatim. Task outcomes (create/restore/delete) surface
// through StatusLineVM via TaskCompletedEvent — no completion text here.
class SnapshotsVM {
   public:
    SnapshotsVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher);
    // UI-thread destroy; alive-token handshake (ModulesVM contract verbatim).
    ~SnapshotsVM();

    // Holds references + Subscriptions: neither copyable nor movable.
    SnapshotsVM(const SnapshotsVM&) = delete;
    SnapshotsVM& operator=(const SnapshotsVM&) = delete;
    SnapshotsVM(SnapshotsVM&&) = delete;
    SnapshotsVM& operator=(SnapshotsVM&&) = delete;

    std::vector<std::string>& rowsRef() { return rows_; }
    int& selectedRef() { return selected_; }
    void rebuild();                                // UI thread: facade snapshot list → rows
    std::string banner() const;                    // counts summary ("no snapshots" when empty)
    std::vector<std::string> detailLines() const;  // full id, parent, payload counts

    // Case-insensitive substring over id, trigger and reason (snapshot-ui
    // spec), same interaction contract as the Devices/Modules filters. A
    // filter that matches nothing yields the named no-matches placeholder
    // row, never an empty list.
    void setFilter(std::string filter);
    const std::string& filter() const { return filter_; }

    // Parent-chain presentation (snapshot-history spec): rows gain depth
    // indentation plus HEAD / last-good / chain-start markers. Off by default;
    // both UIs toggle it. Filter and history compose, but the chain is built
    // from the FULL list and then filtered — deriving markers from the visible
    // subset would label a non-HEAD row as HEAD whenever the real HEAD is
    // filtered out, which is exactly the kind of confident falsehood
    // docs/DESIGN.md §2.1 forbids.
    void setHistoryView(bool on);
    bool historyView() const { return historyView_; }

    // ---- Restore preview (snapshot-ui spec) ----
    // Starts the diff fetch for a restore preview of `id` against live state
    // and returns a waitable handle. Async by construction: the diff is an IPC
    // round trip and docs/DESIGN.md forbids blocking the UI thread on one.
    // Callers open the preview surface immediately — previewLines() reports
    // the loading state until SnapshotDiffRefreshedEvent lands. Shared, not
    // unique, so returning the handle to a caller (tests) does not strip the
    // destructor of the one it must wait on (ModulesVM::fillSignatures idiom).
    std::shared_future<void> requestPreview(std::string id);
    // The preview surface for the snapshot named by the last requestPreview():
    // what will change, which snapshot is selected / current HEAD / last good,
    // and the partial-convergence note. Rendered verbatim by both UIs.
    // Covers the loading and unavailable states too, so neither UI invents
    // wording for them.
    std::vector<std::string> previewLines() const;
    // Diff of the pending preview alone, without the surrounding preview
    // framing — what a plain "diff selected against current state" view shows.
    std::vector<std::string> diffLines() const;

    // Recovery guidance for the most recent restore (snapshot-ui spec): the
    // items that did not converge with their reasons, the safety snapshot id,
    // and the CLI command to fall back to. Empty when the last restore fully
    // converged or none has run — callers show nothing in that case rather
    // than an empty box.
    std::vector<std::string> restoreGuidanceLines() const;

    // Verb args for the confirm modals (snapshot-ui spec). nullopt ⇔ verb
    // disabled: placeholder row, corrupt/unsupported for restore (refused
    // locally), unsupported for delete (store refuses newer formatVersion).
    struct RestoreArgs {
        std::string id;
        std::string confirmText;  // states the restore outcome semantics
    };
    struct DeleteArgs {
        std::string id;
        std::string confirmText;
    };
    std::optional<RestoreArgs> selectedRestore() const;
    std::optional<DeleteArgs> selectedDelete() const;
    // Selected snapshot's id, nullopt on a placeholder row. Unlike
    // selectedRestore() this carries no health gate: diffing a corrupt
    // snapshot is a read, and the daemon refuses it authoritatively.
    std::optional<std::string> selectedSnapshotId() const { return selectedId(); }

    // Per-row snapshot health for TUI colouring (read-only; no wording change).
    // nullopt for the placeholder / out-of-range rows.
    std::optional<core::SnapshotHealth> healthForRow(int row) const;
    // Row-marker predicates for the chain view (snapshot-history spec): whether
    // the row is the current HEAD or the last-good snapshot. Both derive from
    // the same core::buildSnapshotChain the history markers use (computed once
    // per rebuild), so a coloured marker can never disagree with the list.
    bool isHeadRow(int row) const;
    bool isLastGoodRow(int row) const;

    void setRebuildHooks(std::function<void()> before, std::function<void()> after);
    // Fired on the UI thread when a requested diff lands. Deliberately NOT the
    // rebuild hooks: those bracket a Qt begin/endResetModel pair, and a diff
    // changes no rows — signalling it through them would fire endResetModel
    // without its begin. Frontends use this to repaint a diff pane or to open
    // a preview dialog once its content exists.
    void setDiffReadyHook(std::function<void()> hook);

   private:
    void queueRebuild();  // SnapshotsRefreshedEvent → coalesced dispatcher post
    void queueRefresh();  // SnapshotsChangedEvent → coalesced facade_.refreshSnapshots()
    // Row's meta, nullptr for the placeholder / out-of-range row. selectedMeta()
    // is metaForRow(selected_); both feed the per-row accessors above.
    const core::SnapshotMeta* metaForRow(int row) const;
    const core::SnapshotMeta* selectedMeta() const;
    std::optional<std::string> selectedId() const;

    ApplicationFacade& facade_;
    runtime::EventBus& bus_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    bool historyView_ = false;
    // Snapshot the preview was requested for, and whether its diff has landed.
    // `previewPending_` is what separates "still fetching" from "fetched, no
    // differences" — both look like an absent diff otherwise.
    std::optional<std::string> previewId_;
    bool previewPending_ = false;
    std::vector<std::string> rows_;
    // Row → index into metas_; nullopt == placeholder.
    std::vector<std::optional<std::size_t>> rowRefs_;
    std::vector<core::SnapshotMeta> metas_;
    // Current chain HEAD and last-good snapshot ids ("" when none). Recomputed
    // each rebuild() from core::buildSnapshotChain so the row-marker predicates
    // are O(1) id compares rather than rebuilding the chain per query.
    std::string headId_;
    std::string lastGoodId_;
    int selected_ = 0;
    std::atomic<bool> rebuildQueued_{false};
    std::atomic<bool> refreshQueued_{false};
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    std::function<void()> diffReady_;
    // Future custody (ApplicationFacade::refreshSnapshots contract): the worker
    // captures the facade, so every handle must be waited before the facade
    // dies. queueRefresh() waits the previous handle before starting the next
    // (coalescing bounds that to one in flight) and the dtor waits the last.
    std::future<void> lastRefresh_;
    // Same custody rule as lastRefresh_: the diff worker captures the facade.
    std::shared_future<void> lastDiff_;
    runtime::Subscription subRefreshed_;
    runtime::Subscription subChanged_;
    runtime::Subscription subDiff_;
    // Posted closures capture this token by value and no-op once false; the
    // destructor clears it first (ModulesVM i-2 contract: dtor and post()
    // execution serialize on the UI thread).
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace devmgr::app
