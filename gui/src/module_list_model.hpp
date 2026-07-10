#pragma once
#include <QAbstractListModel>

#include "devmgr/app/modules_vm.hpp"

namespace devmgr::gui {

// Qt mirror of ModulesVM's row list (DeviceListModel pattern: the VM's
// rebuild hooks drive begin/endResetModel; rows are preformatted strings and
// delivered unmodified — the T10 byte-frozen row contract).
//
// Lifetime: the VM must outlive this model (the composition root and tests
// guarantee it by declaration order); the destructor unregisters the hooks so
// a later VM rebuild never calls into a destroyed model.
class ModuleListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit ModuleListModel(app::ModulesVM& vm, QObject* parent = nullptr);
    ~ModuleListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

   private:
    app::ModulesVM& vm_;
};

}  // namespace devmgr::gui
