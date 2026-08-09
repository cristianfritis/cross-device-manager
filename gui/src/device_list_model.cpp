#include "gui/src/device_list_model.hpp"

#include <cstddef>

#include "devmgr/core/criticality.hpp"

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
    QString text = QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
    // R6 parity: the TUI marks a load-bearing device with a warning-coloured
    // glyph badge; the GUI has no colour this cycle (DESIGN §9 exception), so it
    // spells the same shared fact out in words. Both read the SAME
    // criticalityForRow() — neither surface re-derives it.
    if (const auto level = vm_.criticalityForRow(index.row());
        level && *level != core::Criticality::Ordinary) {
        text += QString("  [%1]").arg(QString::fromStdString(core::displayCriticality(*level)));
    }
    return text;
}

Qt::ItemFlags DeviceListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (vm_.isHeader(index.row())) return Qt::NoItemFlags;  // headers: unselectable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

}  // namespace devmgr::gui
