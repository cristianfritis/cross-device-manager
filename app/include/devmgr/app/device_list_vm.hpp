#pragma once
#include <atomic>
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
    void rebuild();  // UI-thread: refresh snapshot if stale, filter, group, clamp selection

   private:
    void onModelChanged();   // EventBus handler — marshals one coalesced rebuild() via dispatcher
    void refreshSnapshot();  // UI-thread: re-copy the model and rebuild filter haystacks
    // Sorts `group` by name and appends its header + device rows to rows_/rowIds_.
    void appendRows(core::BusType bus, std::vector<const core::Device*>& group);

    ApplicationFacade& facade_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<core::DeviceId>> rowIds_;  // nullopt == group header
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
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
