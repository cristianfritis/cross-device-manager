#include "gui/src/main_window.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <QAction>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace devmgr::gui {

// The constructor below is standard Qt widget-tree construction: raw `new`
// under Qt parent-child ownership (the window, layouts, and model parent
// delete the children), members assigned in the body interleaved with their
// setup, and the whole build in one function. gsl::owner and the
// member-initializer rule do not model Qt ownership, and splitting the build
// would only scatter a linear widget assembly.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
// NOLINTBEGIN(readability-function-size)
MainWindow::MainWindow(app::DeviceListVM& listVm, app::DeviceDetailVM& detailVm,
                       app::StatusLineVM& statusVm, QtUiDispatcher& dispatcher,
                       std::function<void()> onRefresh, QWidget* parent)
    : QMainWindow(parent),
      listVm_(listVm),
      detailVm_(detailVm),
      statusVm_(statusVm),
      onRefresh_(std::move(onRefresh)) {
    setWindowTitle(QStringLiteral("Device Manager"));

    auto* toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    auto* refreshAction = toolbar->addAction(QStringLiteral("Refresh"));
    connect(refreshAction, &QAction::triggered, this, [this] { onRefresh_(); });

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText(QStringLiteral("filter devices…"));
    connect(filterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { listVm_.setFilter(text.toStdString()); });

    model_ = new DeviceListModel(listVm_, this);
    listView_ = new QListView;
    listView_->setModel(model_);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    detailTree_ = new QTreeWidget;
    detailTree_->setColumnCount(2);
    detailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    detailTree_->setRootIsDecorated(false);
    detailTree_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(filterEdit_);
    leftLayout->addWidget(listView_);

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(detailTree_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // View selection mirrors the VM (headers can't be selected — model flags).
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) listVm_.selectedRef() = current.row();
                updateDetailPane();
            });
    // After a rebuild the VM has re-resolved the selection by DeviceId; the
    // reset cleared the view's currentIndex, so re-apply the VM's row and
    // rebuild the detail pane (properties may have changed under the same id).
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        syncSelectionFromVm();
        updateDetailPane();
    });
    // The Qt analogue of the TUI re-rendering on Event::Custom: StatusLineVM
    // posts a wake closure on every message set/clear; re-read text() then.
    connect(&dispatcher, &QtUiDispatcher::taskExecuted, this, [this] { updateStatusBar(); });

    updateDetailPane();  // "(no device selected)" until something is chosen
}
// NOLINTEND(readability-function-size)
// NOLINTEND(cppcoreguidelines-prefer-member-initializer)
// NOLINTEND(cppcoreguidelines-owning-memory)

void MainWindow::syncSelectionFromVm() {
    const int row = listVm_.selectedRef();
    if (row >= 0 && row < model_->rowCount() && !listVm_.isHeader(row))
        listView_->setCurrentIndex(model_->index(row, 0));
}

void MainWindow::updateDetailPane() {
    detailTree_->clear();
    for (const std::string& line : detailVm_.lines(listVm_.selectedDeviceId())) {
        // Parented to detailTree_ — the tree deletes its items.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* item = new QTreeWidgetItem(detailTree_);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            item->setText(0, QString::fromStdString(line));
        } else {
            item->setText(0, QString::fromStdString(line.substr(0, colon)).trimmed());
            item->setText(1, QString::fromStdString(line.substr(colon + 1)).trimmed());
        }
    }
    detailTree_->resizeColumnToContents(0);
}

void MainWindow::updateStatusBar() {
    statusBar()->showMessage(QString::fromStdString(statusVm_.text()));
}

}  // namespace devmgr::gui
