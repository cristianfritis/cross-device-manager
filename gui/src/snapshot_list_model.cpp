#include "gui/src/snapshot_list_model.hpp"

#include <cstddef>

#include "gui/src/prose_row_delegate.hpp"

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
    // A wholly unsupported tab has no data at all: pushEmptyStateRow() makes the
    // shared sentence the single row, so unsupportedContent() having a value IS
    // the statement that every row here is prose. ProseRowDelegate wraps it;
    // every data row keeps the DESIGN §2.4 elide.
    if (role == kProseRowRole) return vm_.unsupportedContent().has_value();
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

}  // namespace devmgr::gui
