#include "gui/src/main_window.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <QAction>
#include <QFontDatabase>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "devmgr/core/models.hpp"

namespace devmgr::gui {

// The constructor below is standard Qt widget-tree construction: raw `new`
// under Qt parent-child ownership (the window, layouts, and model parent
// delete the children), members assigned in the body interleaved with their
// setup, and the whole build in one function. gsl::owner and the
// member-initializer rule do not model Qt ownership, and splitting the build
// would only scatter a linear widget assembly. The cognitive-complexity
// suppression is the same judgment: the "complexity" is the branch count
// summed across the independent connect-lambdas (each trivial on its own),
// not one tangled control flow.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer)
// NOLINTBEGIN(readability-function-size)
// NOLINTBEGIN(readability-function-cognitive-complexity)
MainWindow::MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm,
                       app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm,
                       app::ModulesVM& modulesVm, QtUiDispatcher& dispatcher, Actions actions,
                       QWidget* parent)
    : QMainWindow(parent),
      facade_(facade),
      listVm_(listVm),
      detailVm_(detailVm),
      statusVm_(statusVm),
      modulesVm_(modulesVm),
      actions_(std::move(actions)) {
    setWindowTitle(QStringLiteral("Device Manager"));

    auto* toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    auto* refreshAction = toolbar->addAction(QStringLiteral("Refresh"));
    connect(refreshAction, &QAction::triggered, this, [this] { actions_.onRefresh(); });

    toggleAction_ = toolbar->addAction(QStringLiteral("Disable"));
    toggleAction_->setEnabled(false);
    connect(toggleAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        const bool enable = device->status == core::DeviceStatus::Disabled;
        const QString prompt = QStringLiteral("%1 %2?").arg(
            enable ? QStringLiteral("Enable") : QStringLiteral("Disable"),
            QString::fromStdString(device->name));
        if (askConfirm(prompt)) actions_.onSetEnabled(*id, enable);
    });

    loadModuleAction_ = toolbar->addAction(QStringLiteral("Load Module…"));
    connect(loadModuleAction_, &QAction::triggered, this, [this] {
        const QString name = actions_.textInput
                                 ? actions_.textInput(QStringLiteral("Load module"), QString{})
                                 : QInputDialog::getText(this, QStringLiteral("Load module"),
                                                         QStringLiteral("Module name:"));
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!name.isEmpty() && valid.match(name).hasMatch())
            actions_.onLoadModule(name.toStdString());
    });

    unloadModuleAction_ = toolbar->addAction(QStringLiteral("Unload"));
    connect(unloadModuleAction_, &QAction::triggered, this, [this] {
        const auto name = modulesVm_.selectedModule();
        if (!name) return;
        const auto verdict = facade_.canUnloadModule(*name);
        if (!verdict.allowed) {
            statusBar()->showMessage(QString::fromStdString("cannot unload: " + verdict.reason));
            return;
        }
        const QString prompt =
            QStringLiteral("Unload module %1?").arg(QString::fromStdString(*name));
        if (askConfirm(prompt)) actions_.onUnloadModule(*name);
    });

    unbindAction_ = toolbar->addAction(QStringLiteral("Unbind driver (advanced)"));
    connect(unbindAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        const auto verdict = facade_.canDisable(*id);
        if (!verdict.allowed) {
            statusBar()->showMessage(QString::fromStdString("cannot unbind: " + verdict.reason));
            return;
        }
        if (askConfirm(QStringLiteral("Unbind driver from %1? (not persistent)")
                           .arg(QString::fromStdString(device->name))))
            actions_.onUnbindDriver(*id);
    });

    bindAction_ = toolbar->addAction(QStringLiteral("Bind driver…"));
    connect(bindAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        QString prefill = QString::fromStdString(device->boundDriver.value_or(""));
        if (prefill.isEmpty()) {
            const auto candidates = facade_.driverInfo(*id);  // modalias dropdown data
            if (!candidates.empty()) prefill = QString::fromStdString(candidates.front().name);
        }
        const QString driver =
            actions_.textInput
                ? actions_.textInput(QStringLiteral("Bind driver"), prefill)
                : QInputDialog::getText(this, QStringLiteral("Bind driver"),
                                        QStringLiteral("Driver name:"), QLineEdit::Normal, prefill);
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!driver.isEmpty() && valid.match(driver).hasMatch())
            actions_.onBindDriver(*id, driver.toStdString());
    });

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText(QStringLiteral("filter devices…"));
    connect(filterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { listVm_.setFilter(text.toStdString()); });

    model_ = new DeviceListModel(listVm_, this);
    listView_ = new QListView;
    listView_->setModel(model_);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Same action doubles as the list's context menu.
    listView_->addAction(toggleAction_);
    listView_->setContextMenuPolicy(Qt::ActionsContextMenu);

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

    // Modules page: banner + filter + fixed-column list left, detail right —
    // the T11 TUI screen, in widgets.
    bannerLabel_ = new QLabel;
    moduleFilterEdit_ = new QLineEdit;
    moduleFilterEdit_->setPlaceholderText(QStringLiteral("filter modules…"));
    connect(moduleFilterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { modulesVm_.setFilter(text.toStdString()); });
    moduleModel_ = new ModuleListModel(modulesVm_, this);
    modulesView_ = new QListView;
    modulesView_->setModel(moduleModel_);
    modulesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modulesView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    moduleDetailTree_ = new QTreeWidget;
    moduleDetailTree_->setColumnCount(2);
    moduleDetailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    moduleDetailTree_->setRootIsDecorated(false);
    moduleDetailTree_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* modulesLeft = new QWidget;
    auto* modulesLeftLayout = new QVBoxLayout(modulesLeft);
    modulesLeftLayout->setContentsMargins(0, 0, 0, 0);
    modulesLeftLayout->addWidget(bannerLabel_);
    modulesLeftLayout->addWidget(moduleFilterEdit_);
    modulesLeftLayout->addWidget(modulesView_);
    auto* modulesSplitter = new QSplitter;
    modulesSplitter->addWidget(modulesLeft);
    modulesSplitter->addWidget(moduleDetailTree_);
    modulesSplitter->setStretchFactor(1, 1);

    tabs_ = new QTabWidget;
    tabs_->addTab(splitter, QStringLiteral("Devices"));
    tabs_->addTab(modulesSplitter, QStringLiteral("Modules"));
    setCentralWidget(tabs_);

    // Tab entry mirrors the TUI's 'm' key: banner recomputed (it reads sysfs —
    // never per frame), fresh snapshot, async signature fill (cached names are
    // skipped; overlapping calls coalesce onto the in-flight worker).
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        updateActionEnablement();
        if (index == 1) {
            bannerLabel_->setText(QString::fromStdString(modulesVm_.banner()));
            modulesVm_.rebuild();
            modulesVm_.fillSignatures();
        }
    });

    // View selection mirrors the VM (headers can't be selected — model flags).
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) listVm_.selectedRef() = current.row();
                updateDetailPane();
                updateActionEnablement();
            });
    // After a rebuild the VM has re-resolved the selection by DeviceId; the
    // reset cleared the view's currentIndex, so re-apply the VM's row and
    // rebuild the detail pane (properties may have changed under the same id).
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        syncSelectionFromVm();
        updateDetailPane();
        updateActionEnablement();
    });

    connect(modulesView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) modulesVm_.selectedRef() = current.row();
                updateModuleDetailPane();
                updateActionEnablement();
            });
    connect(moduleModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateModuleDetailPane();
        updateActionEnablement();
    });

    // The Qt analogue of the TUI re-rendering on Event::Custom: StatusLineVM
    // posts a wake closure on every message set/clear; re-read text() then.
    connect(&dispatcher, &QtUiDispatcher::taskExecuted, this, [this] { updateStatusBar(); });

    updateDetailPane();  // "(no device selected)" until something is chosen
    updateModuleDetailPane();
    updateActionEnablement();
}
// NOLINTEND(readability-function-cognitive-complexity)
// NOLINTEND(readability-function-size)
// NOLINTEND(cppcoreguidelines-prefer-member-initializer)
// NOLINTEND(cppcoreguidelines-owning-memory)

bool MainWindow::askConfirm(const QString& prompt) {
    return actions_.confirm
               ? actions_.confirm(prompt)
               : QMessageBox::question(this, QStringLiteral("Confirm"), prompt) == QMessageBox::Yes;
}

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

void MainWindow::updateModuleDetailPane() {
    moduleDetailTree_->clear();
    for (const std::string& line : modulesVm_.detailLines()) {
        // Parented to moduleDetailTree_ — the tree deletes its items.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* item = new QTreeWidgetItem(moduleDetailTree_);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            item->setText(0, QString::fromStdString(line));
        } else {
            item->setText(0, QString::fromStdString(line.substr(0, colon)).trimmed());
            item->setText(1, QString::fromStdString(line.substr(colon + 1)).trimmed());
        }
    }
    moduleDetailTree_->resizeColumnToContents(0);
}

void MainWindow::updateStatusBar() {
    statusBar()->showMessage(QString::fromStdString(statusVm_.text()));
}

void MainWindow::updateActionEnablement() {
    const bool onModules = tabs_->currentIndex() == 1;
    loadModuleAction_->setEnabled(onModules);
    unloadModuleAction_->setEnabled(onModules && modulesVm_.selectedModule().has_value());

    const auto id = listVm_.selectedDeviceId();
    const auto device = (!onModules && id) ? facade_.findById(*id) : std::nullopt;
    if (!device) {  // Modules tab, or no device selected
        toggleAction_->setEnabled(false);
        toggleAction_->setText(QStringLiteral("Disable"));
        toggleAction_->setToolTip({});
        unbindAction_->setEnabled(false);
        bindAction_->setEnabled(false);
        return;
    }
    unbindAction_->setEnabled(true);
    bindAction_->setEnabled(true);
    const bool enable = device->status == core::DeviceStatus::Disabled;
    toggleAction_->setText(enable ? QStringLiteral("Enable") : QStringLiteral("Disable"));
    if (!enable) {
        // Advisory only — devmgrd re-checks authoritatively on every request.
        const auto verdict = facade_.canDisable(*id);
        toggleAction_->setEnabled(verdict.allowed);
        toggleAction_->setToolTip(
            verdict.allowed ? QString{}
                            : QString::fromStdString("cannot disable: " + verdict.reason));
        return;
    }
    toggleAction_->setEnabled(true);
    toggleAction_->setToolTip({});
}

}  // namespace devmgr::gui
