#pragma once
#include <QStyledItemDelegate>

class QListView;

namespace devmgr::gui {

// A row that is prose, not data: the shared unsupported sentence a wholly
// unsupported tab shows in place of its list (backend-availability, "A view with
// no implemented source states why rather than showing nothing"). The three list
// models set it; nothing else does.
inline constexpr int kProseRowRole = Qt::UserRole + 1;

// Wraps the prose row, and ONLY the prose row.
//
// docs/DESIGN.md §2.4 has list rows elide a long value so a wide name never
// pushes the layout, with the full value reachable in the detail pane — which is
// why main_window.cpp turns word wrap off on every view. That rule is right for
// a data row and wrong for this one: the unsupported sentence has no detail-pane
// entry to be reachable in, so eliding it would leave it unreadable and letting
// the view scroll horizontally leaves it readable only by scrolling (task 12.4 —
// the Windows gate found exactly that, where the sentence IS the tab's content).
//
// So the elide rule keeps applying to every data row, and a row the model marks
// prose wraps to the viewport width instead. Rows are never wider than the
// viewport, so the view raises no horizontal scrollbar for them.
class ProseRowDelegate final : public QStyledItemDelegate {
    Q_OBJECT
   public:
    // `view` is the delegate's parent and outlives it; it supplies the wrap
    // width, which sizeHint() cannot get from the option rect.
    explicit ProseRowDelegate(QListView* view);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

   private:
    // Width available to the text, viewport minus the style's own item margins.
    [[nodiscard]] int textWidth(const QStyleOptionViewItem& option) const;

    QListView* view_;
};

}  // namespace devmgr::gui
