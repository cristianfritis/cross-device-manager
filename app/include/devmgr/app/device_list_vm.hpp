#pragma once
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
    void rebuild();  // UI-thread: re-read model, filter, group, clamp selection

   private:
    void onModelChanged();  // EventBus handler — marshals rebuild() via dispatcher

    ApplicationFacade& facade_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<core::DeviceId>> rowIds_;  // nullopt == group header
    int selected_ = 0;
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
