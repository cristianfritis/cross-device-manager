#pragma once
#include <QAbstractListModel>

#include "devmgr/app/device_list_vm.hpp"

namespace devmgr::gui {

// Flat Qt adapter over the shared DeviceListVM (Phase 3 spec, Approach A).
// The VM's rows stay the single source of truth — this model stores nothing.
// Every VM rebuild (hotplug delta or filter keystroke) is bracketed with
// beginResetModel()/endResetModel() via the VM's rebuild hooks, so views
// re-query exactly once per rebuild. Group-header rows are disabled and
// non-selectable; MainWindow re-applies the VM's DeviceId-preserved selection
// after each reset.
//
// Lifetime: the VM must outlive this model (the composition root and tests
// guarantee it by declaration order); the destructor unregisters the hooks so
// a later VM rebuild never calls into a destroyed model.
class DeviceListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit DeviceListModel(app::DeviceListVM& vm, QObject* parent = nullptr);
    ~DeviceListModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

   private:
    app::DeviceListVM& vm_;
};

}  // namespace devmgr::gui
