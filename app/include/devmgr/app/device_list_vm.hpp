#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Builds the FTXUI-facing row list (grouped/sorted by bus, filtered) and maps
// the selected row to a DeviceId. Subscribes to model deltas and rebuilds via
// the dispatcher, so all state mutation happens on the UI thread.
class DeviceListVM {
   public:
    DeviceListVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher);

    std::vector<std::string>& rowsRef() { return rows_; }
    const std::vector<std::string>& rowsRef() const { return rows_; }
    int& selectedRef() { return selected_; }
    void setFilter(std::string filter);
    std::optional<core::DeviceId> selectedDeviceId() const;
    // True iff `row` is a group-header or placeholder row (no DeviceId behind
    // it); false for device rows and for out-of-range rows. Lets a frontend
    // render/disable headers without re-deriving the grouping.
    bool isHeader(int row) const;
    // Per-row device status for TUI semantic colouring (read-only; adds no
    // wording and changes no behaviour). Returns the status the row's Device
    // already carries; nullopt for group-header, "(no devices)" placeholder,
    // and out-of-range rows — exactly the rows isHeader()/selectedDeviceId()
    // treat as non-device. The role→colour mapping lives in the TUI, not here.
    std::optional<core::DeviceStatus> statusForRow(int row) const;
    // Frontend hooks invoked at entry/exit of every rebuild() — the single
    // funnel for all row mutation (delta-triggered posts and setFilter alike).
    // Qt uses them for beginResetModel()/endResetModel(); the TUI leaves them
    // unset. Default-constructed hooks are no-ops. UI-thread only: set/clear
    // them on the same thread rebuild() runs on.
    void setRebuildHooks(std::function<void()> before, std::function<void()> after) {
        beforeRebuild_ = std::move(before);
        afterRebuild_ = std::move(after);
    }
    void rebuild();  // UI-thread: refresh snapshot if stale, filter, group, clamp selection

   private:
    void onModelChanged();   // EventBus handler — marshals one coalesced rebuild() via dispatcher
    void refreshSnapshot();  // UI-thread: re-copy the model and rebuild filter haystacks
    // Sorts `group` by name and appends its header + device rows to rows_/rowIds_.
    void appendRows(core::BusType bus, std::vector<const core::Device*>& group);
    // Re-resolves `keep` to its new row index (stable selection), then clamps.
    void restoreSelection(const std::optional<core::DeviceId>& keep);

    ApplicationFacade& facade_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<core::DeviceId>> rowIds_;  // nullopt == group header
    // Aligned 1:1 with rows_/rowIds_; nullopt for header/placeholder rows.
    // Captured from the grouped Device* during rebuild so statusForRow() is a
    // pure lookup (status only changes via a delta, which rebuilds anyway).
    std::vector<std::optional<core::DeviceStatus>> rowStatus_;
    // Model snapshot cached on the UI thread so filter keystrokes rebuild rows
    // without re-copying the whole model out of DeviceService. haystacks_ holds
    // the per-device lowercase filter text, aligned with snapshot_, computed
    // once per snapshot refresh instead of once per device per keystroke.
    std::vector<core::Device> snapshot_;
    std::vector<std::string> haystacks_;
    // false => snapshot_ is stale and the next rebuild() must re-fetch. Written
    // by onModelChanged() on publisher threads, claimed (exchange) on the UI
    // thread; a store(false) racing a rebuild's fetch is followed by that
    // event's own posted rebuild, so no invalidation is ever lost.
    std::atomic<bool> snapshotValid_{false};
    std::atomic<bool> rebuildQueued_{false};  // one dispatcher post per burst of deltas
    int selected_ = 0;
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
