#include "gui/src/snapshot_list_model.hpp"

#include <cstddef>

namespace devmgr::gui {

SnapshotListModel::SnapshotListModel(app::SnapshotsVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
    vm_.rebuild();
}

SnapshotListModel::~SnapshotListModel() {
    vm_.setRebuildHooks({}, {});
}

int SnapshotListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(vm_.rowsRef().size());
}

QVariant SnapshotListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount({})) return {};
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

}  // namespace devmgr::gui
