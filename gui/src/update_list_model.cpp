#include "gui/src/update_list_model.hpp"

#include <cstddef>

namespace devmgr::gui {

UpdateListModel::UpdateListModel(app::UpdatesVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
    vm_.rebuild();
}

UpdateListModel::~UpdateListModel() {
    vm_.setRebuildHooks({}, {});
}

int UpdateListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(vm_.rowsRef().size());
}

QVariant UpdateListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount({})) return {};
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

}  // namespace devmgr::gui
