#pragma once
#include <functional>

#include <QMainWindow>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

class QAction;
class QLineEdit;
class QListView;
class QTreeWidget;

namespace devmgr::gui {

// Strict-parity main window (Phase 3 spec): filter box + grouped device list
// on the left, two-column key/value detail pane on the right, toolbar Refresh,
// transient status bar. The shared ViewModels remain the source of truth;
// widgets mirror them. onRefresh is injected so the composition root owns the
// pending-refresh-futures lifetime (the drain-before-teardown contract in
// tui/src/tui_app.cpp applies identically to the GUI root).
class MainWindow final : public QMainWindow {
    Q_OBJECT
   public:
    MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm,
               app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm,
               QtUiDispatcher& dispatcher, std::function<void()> onRefresh,
               std::function<void(const core::DeviceId&, bool)> onSetEnabled,
               std::function<bool(const QString&)> confirm = {}, QWidget* parent = nullptr);

    // Test accessors (offscreen tests drive/inspect the real widgets).
    QListView* listView() const { return listView_; }
    QTreeWidget* detailTree() const { return detailTree_; }
    QLineEdit* filterEdit() const { return filterEdit_; }
    QAction* toggleAction() const { return toggleAction_; }

   private:
    void syncSelectionFromVm();  // after modelReset: VM re-resolved by DeviceId
    void updateDetailPane();
    void updateStatusBar();
    void updateToggleAction();

    app::ApplicationFacade& facade_;
    app::DeviceListVM& listVm_;
    app::DeviceDetailVM& detailVm_;
    app::StatusLineVM& statusVm_;
    std::function<void()> onRefresh_;
    std::function<void(const core::DeviceId&, bool)> onSetEnabled_;
    std::function<bool(const QString&)> confirm_;
    DeviceListModel* model_ = nullptr;  // Qt-parented to this window
    QListView* listView_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QTreeWidget* detailTree_ = nullptr;
    QAction* toggleAction_ = nullptr;
};

}  // namespace devmgr::gui
