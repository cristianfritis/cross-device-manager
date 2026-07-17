#pragma once
#include <QAbstractListModel>

#include "devmgr/app/snapshots_vm.hpp"

namespace devmgr::gui {

// Qt mirror of SnapshotsVM's row list (UpdateListModel pattern, reused verbatim:
// the VM's rebuild hooks drive begin/endResetModel; rows are preformatted
// strings and delivered unmodified — the byte-frozen row contract shared with
// the TUI, snapshot-ui spec §"Byte-identical rows across UIs").
//
// Lifetime: the VM must outlive this model (the composition root and tests
// guarantee it by declaration order); the destructor unregisters the hooks so
// a later VM rebuild never calls into a destroyed model.
class SnapshotListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit SnapshotListModel(app::SnapshotsVM& vm, QObject* parent = nullptr);
    ~SnapshotListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

   private:
    app::SnapshotsVM& vm_;
};

}  // namespace devmgr::gui
