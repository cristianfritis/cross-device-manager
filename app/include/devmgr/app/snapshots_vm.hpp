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

    void setRebuildHooks(std::function<void()> before, std::function<void()> after);

   private:
    void queueRebuild();  // SnapshotsRefreshedEvent → coalesced dispatcher post
    void queueRefresh();  // SnapshotsChangedEvent → coalesced facade_.refreshSnapshots()
    // Selected row's meta, nullptr for the placeholder row; and its stable
    // identity for selection restore across rebuilds.
    const core::SnapshotMeta* selectedMeta() const;
    std::optional<std::string> selectedId() const;

    ApplicationFacade& facade_;
    runtime::EventBus& bus_;
    IUiDispatcher& dispatcher_;
    std::vector<std::string> rows_;
    // Row → index into metas_; nullopt == placeholder.
    std::vector<std::optional<std::size_t>> rowRefs_;
    std::vector<core::SnapshotMeta> metas_;
    int selected_ = 0;
    std::atomic<bool> rebuildQueued_{false};
    std::atomic<bool> refreshQueued_{false};
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    // Future custody (ApplicationFacade::refreshSnapshots contract): the worker
    // captures the facade, so every handle must be waited before the facade
    // dies. queueRefresh() waits the previous handle before starting the next
    // (coalescing bounds that to one in flight) and the dtor waits the last.
    std::future<void> lastRefresh_;
    runtime::Subscription subRefreshed_;
    runtime::Subscription subChanged_;
    // Posted closures capture this token by value and no-op once false; the
    // destructor clears it first (ModulesVM i-2 contract: dtor and post()
    // execution serialize on the UI thread).
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace devmgr::app
