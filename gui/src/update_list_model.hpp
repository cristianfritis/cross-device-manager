#pragma once
#include <QAbstractListModel>

#include "devmgr/app/updates_vm.hpp"

namespace devmgr::gui {

// Qt mirror of UpdatesVM's row list (ModuleListModel pattern, reused verbatim:
// the VM's rebuild hooks drive begin/endResetModel; rows are preformatted
// strings and delivered unmodified — the T10 byte-frozen row contract, shared
// with the TUI, spec §8.3/V3).
//
// Lifetime: the VM must outlive this model (the composition root and tests
// guarantee it by declaration order); the destructor unregisters the hooks so
// a later VM rebuild never calls into a destroyed model.
class UpdateListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit UpdateListModel(app::UpdatesVM& vm, QObject* parent = nullptr);
    ~UpdateListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

   private:
    app::UpdatesVM& vm_;
};

}  // namespace devmgr::gui
