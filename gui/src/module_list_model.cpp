#include "gui/src/module_list_model.hpp"

#include <cstddef>

#include "gui/src/prose_row_delegate.hpp"

#include "devmgr/core/criticality.hpp"

namespace devmgr::gui {

ModuleListModel::ModuleListModel(app::ModulesVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
    vm_.rebuild();
}

ModuleListModel::~ModuleListModel() {
    vm_.setRebuildHooks({}, {});
}

int ModuleListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(vm_.rowsRef().size());
}

QVariant ModuleListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount({})) return {};
    // A wholly unsupported tab has no data at all: pushEmptyStateRow() makes the
    // shared sentence the single row, so unsupportedContent() having a value IS
    // the statement that every row here is prose. ProseRowDelegate wraps it;
    // every data row keeps the DESIGN §2.4 elide.
    if (role == kProseRowRole) return vm_.unsupportedContent().has_value();
    if (role != Qt::DisplayRole) return {};
    QString text = QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
    // R6 parity: the TUI marks a load-bearing module with a warning-coloured
    // glyph badge; the GUI has no colour this cycle (DESIGN §9 exception), so it
    // spells the same shared fact out in words. Both read the SAME
    // criticalityForRow() — neither surface re-derives it.
    if (const auto level = vm_.criticalityForRow(index.row());
        level && *level != core::Criticality::Ordinary) {
        text += QString("  [%1]").arg(QString::fromStdString(core::displayCriticality(*level)));
    }
    return text;
}

}  // namespace devmgr::gui
