#include "gui/src/prose_row_delegate.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QListView>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>

namespace devmgr::gui {
namespace {

bool isProse(const QModelIndex& index) {
    return index.data(kProseRowRole).toBool();
}

QStyle* styleFor(const QStyleOptionViewItem& option) {
    return option.widget != nullptr ? option.widget->style() : QApplication::style();
}

constexpr int kWrapFlags = Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop;

}  // namespace

ProseRowDelegate::ProseRowDelegate(QListView* view) : QStyledItemDelegate(view), view_(view) {}

int ProseRowDelegate::textWidth(const QStyleOptionViewItem& option) const {
    // The viewport, never the option rect: sizeHint() is asked before a row has
    // a geometry, so option.rect is empty there. One pixel of slack keeps a row
    // that wraps to exactly the viewport width from tripping the horizontal
    // scrollbar it exists to avoid.
    const int available = view_->viewport()->width() - 1;
    QStyle* style = styleFor(option);
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, option.widget) * 2;
    return available - margin;
}

QSize ProseRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const {
    if (!isProse(index)) return QStyledItemDelegate::sizeHint(option, index);

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    const int width = textWidth(opt);
    // Before the view has been laid out there is no width to wrap to; fall back
    // rather than compute a wrap against a nonsense number.
    if (width <= 0) return QStyledItemDelegate::sizeHint(option, index);

    const QFontMetrics metrics(opt.font);
    const QRect bounds = metrics.boundingRect(QRect(0, 0, width, 0), kWrapFlags, opt.text);
    const int vertical =
        styleFor(opt)->pixelMetric(QStyle::PM_FocusFrameVMargin, &opt, opt.widget) * 2;
    return {width, bounds.height() + vertical};
}

void ProseRowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                             const QModelIndex& index) const {
    if (!isProse(index)) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    QStyle* style = styleFor(opt);

    // Background, selection and focus ring stay the style's job — only the text
    // is drawn here, so the row keeps the platform look of every other row.
    const QString text = opt.text;
    opt.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    const QPalette::ColorRole role =
        (opt.state & QStyle::State_Selected) != 0 ? QPalette::HighlightedText : QPalette::Text;
    painter->save();
    painter->setFont(opt.font);
    painter->setPen(opt.palette.color(QPalette::Normal, role));
    painter->drawText(textRect, kWrapFlags, text);
    painter->restore();
}

}  // namespace devmgr::gui
