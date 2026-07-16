#include "gui/src/main_window.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <QAction>
#include <QCloseEvent>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPalette>
#include <QRegularExpression>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "devmgr/core/events.hpp"
#include "devmgr/core/models.hpp"

namespace devmgr::gui {
namespace {
// Warning role (DESIGN.md §4.1) for the durable request banner: bold weight
// pairs with the color so the signal never rides on color alone (§10), and
// the color itself is picked from the palette's own light/dark reference
// values rather than a hard-coded theme (§7 — QPalette, not a QSS theme).
void styleAsWarning(QLabel& label) {
    QFont font = label.font();
    font.setBold(true);
    label.setFont(font);
    QPalette palette = label.palette();
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    palette.setColor(QPalette::WindowText,
                     dark ? QColor(0xE8, 0xB3, 0x5C) : QColor(0x8A, 0x5B, 0x00));
    label.setPalette(palette);
}
}  // namespace

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
                       app::ModulesVM& modulesVm, app::UpdatesVM& updatesVm,
                       QtUiDispatcher& dispatcher, runtime::EventBus& bus, Actions actions,
                       QWidget* parent)
    : QMainWindow(parent),
      facade_(facade),
      listVm_(listVm),
      detailVm_(detailVm),
      statusVm_(statusVm),
      modulesVm_(modulesVm),
      updatesVm_(updatesVm),
      bus_(bus),
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
            // StatusLineVM owns the status line (TTL + no wipe-by-wake) — see
            // the identical TUI pattern (tui/src/tui_app.cpp) (Phase 5 review F-1).
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard", .ok = false, .message = "cannot unload: " + verdict.reason});
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
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard", .ok = false, .message = "cannot unbind: " + verdict.reason});
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

    installUpdateAction_ = toolbar->addAction(QStringLiteral("Install Update"));
    connect(installUpdateAction_, &QAction::triggered, this, [this] {
        const auto args = updatesVm_.selectedInstall();
        if (!args) {
            // StatusLineVM owns the status line (TTL + no wipe-by-wake) — the
            // same T1 F-1 pattern as the module/unbind guard refusals above,
            // and the identical wording the TUI publishes (tui/src/tui_app.cpp).
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard",
                .ok = false,
                .message = "not installable from here (status-only or external "
                           "download — run `fwupdmgr update`)"});
            return;
        }
        if (askConfirm(QString::fromStdString(args->confirmText)))
            pruneAndPushPending(
                facade_.installUpdate(args->providerId, args->candidateId, args->release));
    });

    refreshUpdatesAction_ = toolbar->addAction(QStringLiteral("Refresh Updates"));
    connect(refreshUpdatesAction_, &QAction::triggered, this,
            [this] { pruneAndPushPending(facade_.refreshUpdates()); });

    dismissRequestAction_ = toolbar->addAction(QStringLiteral("Dismiss Request"));
    connect(dismissRequestAction_, &QAction::triggered, this, [this] {
        updatesVm_.dismissRequest();
        updateRequestBannerLabel();
        updateActionEnablement();
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

    // Updates page: availability/reboot/Secure Boot banner + durable request
    // banner + fixed-column update list left, update detail pane right — the
    // T11 TUI Updates screen, in widgets. No filter input: UpdatesVM exposes
    // none (mirrors the TUI shape).
    updatesBannerLabel_ = new QLabel;
    requestBannerLabel_ = new QLabel;
    requestBannerLabel_->setVisible(false);  // shown only while requestBanner() is non-empty
    styleAsWarning(*requestBannerLabel_);
    updateModel_ = new UpdateListModel(updatesVm_, this);
    updatesView_ = new QListView;
    updatesView_->setModel(updateModel_);
    updatesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    updatesView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // Same action doubles as the list's context menu.
    updatesView_->addAction(installUpdateAction_);
    updatesView_->setContextMenuPolicy(Qt::ActionsContextMenu);

    updatesDetailTree_ = new QTreeWidget;
    updatesDetailTree_->setColumnCount(2);
    updatesDetailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    updatesDetailTree_->setRootIsDecorated(false);
    updatesDetailTree_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* updatesLeft = new QWidget;
    auto* updatesLeftLayout = new QVBoxLayout(updatesLeft);
    updatesLeftLayout->setContentsMargins(0, 0, 0, 0);
    updatesLeftLayout->addWidget(updatesBannerLabel_);
    updatesLeftLayout->addWidget(requestBannerLabel_);
    updatesLeftLayout->addWidget(updatesView_);
    auto* updatesSplitter = new QSplitter;
    updatesSplitter->addWidget(updatesLeft);
    updatesSplitter->addWidget(updatesDetailTree_);
    updatesSplitter->setStretchFactor(1, 1);

    tabs_ = new QTabWidget;
    tabs_->addTab(splitter, QStringLiteral("Devices"));
    tabs_->addTab(modulesSplitter, QStringLiteral("Modules"));
    tabs_->addTab(updatesSplitter, tr("Updates"));
    setCentralWidget(tabs_);

    // Tab entry mirrors the TUI's 'm'/'u' cycle: banner(s) recomputed (they
    // read sysfs/the PAL — never per frame), fresh snapshot, async work
    // (signature fill / update refresh) fired so entering the tab never shows
    // stale data.
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        updateActionEnablement();
        // Tab-aware composition (review I-1): entering Updates must fold
        // installProgressText() in; leaving it must fall back to the shared
        // status line — same as the taskExecuted wake path below.
        updateStatusBar();
        if (index == 1) {
            bannerLabel_->setText(QString::fromStdString(modulesVm_.banner()));
            modulesVm_.rebuild();
            modulesVm_.fillSignatures();
        } else if (index == 2) {
            updatesBannerLabel_->setText(QString::fromStdString(updatesVm_.banner()));
            updateRequestBannerLabel();
            updatesVm_.rebuild();
            pruneAndPushPending(facade_.refreshUpdates());
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
        // Module-side resets must not re-run the Devices-tab criticality probe
        // (reads /proc/self/mounts + sysfs) — Phase 5 review F-1.
        if (tabs_->currentIndex() == 1) updateActionEnablement();
    });

    connect(updatesView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) updatesVm_.selectedRef() = current.row();
                updateUpdatesDetailPane();
                updateActionEnablement();
            });
    connect(updateModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateUpdatesDetailPane();
        // Update-side resets must not re-run the Devices/Modules-tab logic —
        // Phase 5 review F-1, extended to the Updates tab.
        if (tabs_->currentIndex() == 2) updateActionEnablement();
    });

    // The Qt analogue of the TUI re-rendering on Event::Custom: StatusLineVM
    // posts a wake closure on every message set/clear; re-read text() then.
    // Review lesson (T11): a banner/request-banner/action-enablement computed
    // only on tab entry goes stale after an async refresh or install completes
    // while the tab stays current — refresh those here too, gated to the
    // Updates tab like the reset guard above.
    connect(&dispatcher, &QtUiDispatcher::taskExecuted, this, [this] {
        updateStatusBar();
        if (tabs_->currentIndex() == 2) {
            updatesBannerLabel_->setText(QString::fromStdString(updatesVm_.banner()));
            updateRequestBannerLabel();
            updateActionEnablement();
        }
    });

    updateDetailPane();  // "(no device selected)" until something is chosen
    updateModuleDetailPane();
    updateUpdatesDetailPane();
    updateActionEnablement();
}
// NOLINTEND(readability-function-cognitive-complexity)
// NOLINTEND(readability-function-size)
// NOLINTEND(cppcoreguidelines-prefer-member-initializer)
// NOLINTEND(cppcoreguidelines-owning-memory)

MainWindow::~MainWindow() {
    // This window's own future custody (Updates actions call facade_ directly
    // rather than through an injected Actions callback — see the pending_
    // member comment in main_window.hpp): wait every outstanding handle before
    // the composition root tears the facade down, exactly like gui_app.cpp's
    // drainPending() and tui_app.cpp's identical contract.
    for (auto& f : pending_) {
        if (f.valid()) f.wait();
    }
}

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

void MainWindow::updateUpdatesDetailPane() {
    updatesDetailTree_->clear();
    for (const std::string& line : updatesVm_.detailLines()) {
        // Parented to updatesDetailTree_ — the tree deletes its items.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* item = new QTreeWidgetItem(updatesDetailTree_);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            item->setText(0, QString::fromStdString(line));
        } else {
            item->setText(0, QString::fromStdString(line.substr(0, colon)).trimmed());
            item->setText(1, QString::fromStdString(line.substr(colon + 1)).trimmed());
        }
    }
    updatesDetailTree_->resizeColumnToContents(0);
}

void MainWindow::updateRequestBannerLabel() {
    const std::string banner = updatesVm_.requestBanner();
    requestBannerLabel_->setText(QString::fromStdString(banner));
    requestBannerLabel_->setVisible(!banner.empty());  // durable until dismiss (spec §9)
}

void MainWindow::pruneAndPushPending(std::future<void> f) {
    // Drop already-completed handles so pending_ stays bounded over a long
    // session, then keep the new one — same discipline as gui_app.cpp's
    // pruneAndPush, scoped to this window's own Updates-action futures.
    std::erase_if(pending_, [](const std::future<void>& g) {
        return g.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
    pending_.push_back(std::move(f));
}

void MainWindow::updateStatusBar() {
    // Review finding I-1 (DESIGN.md §9 Task feedback: status bar == persistent
    // bottom status line): mirror tui_app.cpp's updatesStatusLine() precedence
    // exactly — while on the Updates tab, the durable install-progress text
    // (spec §5.5) wins over the shared status line whenever it is non-empty,
    // so a firmware flash stays visible for its whole (potentially minutes-
    // long) duration instead of only at completion. Both surfaces read the
    // identical VM-owned string — no GUI-side wording.
    if (tabs_->currentIndex() == 2) {
        const auto progress = updatesVm_.installProgressText();
        if (!progress.empty()) {
            statusBar()->showMessage(QString::fromStdString(progress));
            return;
        }
    }
    statusBar()->showMessage(QString::fromStdString(statusVm_.text()));
}

void MainWindow::updateActionEnablement() {
    const int tab = tabs_->currentIndex();
    const bool onModules = tab == 1;
    loadModuleAction_->setEnabled(onModules);
    unloadModuleAction_->setEnabled(onModules && modulesVm_.selectedModule().has_value());

    installUpdateAction_->setEnabled(tab == 2 && updatesVm_.selectedInstall().has_value());
    refreshUpdatesAction_->setEnabled(tab == 2);
    dismissRequestAction_->setEnabled(tab == 2 && !updatesVm_.requestBanner().empty());

    // Devices probe only when tab == 0 (T1 F-1 gating, extended to the
    // Updates tab): findById()/canDisable() below are Devices-tab-only work.
    const auto id = listVm_.selectedDeviceId();
    const auto device = (tab == 0 && id) ? facade_.findById(*id) : std::nullopt;
    if (!device) {  // not on the Devices tab, or no device selected
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

// Quit guard (spec §5.5): a firmware flash left running in the fwupd daemon
// is NOT cancelled by closing the window, so ask before doing that silently.
void MainWindow::closeEvent(QCloseEvent* event) {
    if (facade_.installActive()) {
        const auto prompt =
            tr("Firmware flash continues in the fwupd daemon; closing "
               "does NOT cancel it. Quit anyway?");
        const bool quit = actions_.confirmQuit ? actions_.confirmQuit(prompt)
                                               : QMessageBox::question(this, tr("Confirm"),
                                                                       prompt) == QMessageBox::Yes;
        if (!quit) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

}  // namespace devmgr::gui
