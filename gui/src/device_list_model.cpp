#include "gui/src/device_list_model.hpp"

#include <cstddef>

namespace devmgr::gui {

DeviceListModel::DeviceListModel(app::DeviceListVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
}

DeviceListModel::~DeviceListModel() {
    vm_.setRebuildHooks({}, {});
}

int DeviceListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;  // flat list
    return static_cast<int>(vm_.rowsRef().size());
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(vm_.rowsRef().size()))
        return {};
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

Qt::ItemFlags DeviceListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (vm_.isHeader(index.row())) return Qt::NoItemFlags;  // headers: unselectable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

}  // namespace devmgr::gui
