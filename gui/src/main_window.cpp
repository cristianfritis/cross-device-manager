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
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPalette>
#include <QRegularExpression>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
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
    // Amber-on-dark / ochre-on-light warning foregrounds (contrast pair, §10).
    static const QColor kWarnOnDark(0xE8, 0xB3, 0x5C);
    static const QColor kWarnOnLight(0x8A, 0x5B, 0x00);
    palette.setColor(QPalette::WindowText, dark ? kWarnOnDark : kWarnOnLight);
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
                       app::SnapshotsVM& snapshotsVm, QtUiDispatcher& dispatcher,
                       runtime::EventBus& bus, Actions actions, QWidget* parent)
    : QMainWindow(parent),
      facade_(facade),
      listVm_(listVm),
      detailVm_(detailVm),
      statusVm_(statusVm),
      modulesVm_(modulesVm),
      updatesVm_(updatesVm),
      snapshotsVm_(snapshotsVm),
      bus_(bus),
      actions_(std::move(actions)) {
    setWindowTitle(QStringLiteral("Device Manager"));

    auto* toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    refreshAction_ = toolbar->addAction(QStringLiteral("Refresh"));
    connect(refreshAction_, &QAction::triggered, this, [this] { actions_.onRefresh(); });

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

    // Snapshots verbs (Phase 7) — the TUI s/r/x keys as toolbar actions. Like
    // the Updates commands, they call the facade directly and keep custody in
    // pending_ (this window's own set, drained in the destructor). The ellipsis
    // on Create marks the label prompt (DESIGN.md §5.3).
    createSnapshotAction_ = toolbar->addAction(QStringLiteral("Create Snapshot…"));
    connect(createSnapshotAction_, &QAction::triggered, this, [this] {
        // Label is optional (an unlabeled manual snapshot is valid, matching the
        // TUI). The real QInputDialog reports Cancel through `ok` so an empty
        // accept still creates; the injected seam always creates (tests drive
        // the label), the same contract loadModule uses.
        bool ok = true;
        const QString label = actions_.textInput
                                  ? actions_.textInput(QStringLiteral("Create snapshot"), QString{})
                                  : QInputDialog::getText(this, QStringLiteral("Create snapshot"),
                                                          QStringLiteral("Label (optional):"),
                                                          QLineEdit::Normal, QString{}, &ok);
        if (ok) pruneAndPushPending(facade_.createSnapshot(label.toStdString()));
    });

    // The ellipsis marks the preview step (DESIGN.md §5.3): restore now opens a
    // preview and only runs on explicit confirmation from it.
    restoreSnapshotAction_ = toolbar->addAction(QStringLiteral("Restore Snapshot…"));
    connect(restoreSnapshotAction_, &QAction::triggered, this, [this] {
        const auto args = snapshotsVm_.selectedRestore();
        if (!args) {
            // Refused locally: placeholder row or corrupt/unsupported snapshot.
            // StatusLineVM owns the status line (TTL + no wipe-by-wake); the
            // wording is the shared VM-level string the TUI publishes verbatim
            // (tui/src/tui_app.cpp) — safety refusals stay visible (DESIGN.md
            // §5.3).
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard",
                .ok = false,
                .message = "cannot restore: no healthy snapshot selected"});
            return;
        }
        // Async by construction: the preview's diff is an IPC round trip, so
        // the dialog is opened by the diff-ready hook rather than here. Opening
        // it now would either block the GUI thread or show a modal whose
        // content rewrites itself under the user.
        pendingPreviewRestoreId_ = args->id;
        snapshotsVm_.requestPreview(args->id);
        // In-progress affordance (DESIGN.md §6): block a duplicate submission
        // while the diff is in flight. Deliberately NOT a TaskCompletedEvent —
        // nothing has completed, and StatusLineVM's completion path is not a
        // progress channel.
        restoreSnapshotAction_->setEnabled(false);
    });

    diffSnapshotAction_ = toolbar->addAction(QStringLiteral("Diff Snapshot"));
    connect(diffSnapshotAction_, &QAction::triggered, this, [this] {
        if (snapshotDiffPaneRequested_) {  // toggle back to the detail pane
            snapshotDiffPaneRequested_ = false;
            updateSnapshotsDetailPane();
            return;
        }
        const auto id = snapshotsVm_.selectedSnapshotId();
        if (!id) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard", .ok = false, .message = "cannot diff: no snapshot selected"});
            return;
        }
        // Remember which snapshot the pane describes: the selection can move
        // while it is open, and a silently re-labelled diff would be a lie.
        snapshotDiffForId_ = *id;
        snapshotDiffPaneRequested_ = true;
        snapshotsVm_.requestPreview(*id);
        updateSnapshotsDetailPane();
    });

    historySnapshotAction_ = toolbar->addAction(QStringLiteral("History"));
    historySnapshotAction_->setCheckable(true);
    connect(historySnapshotAction_, &QAction::triggered, this,
            [this](bool on) { snapshotsVm_.setHistoryView(on); });

    deleteSnapshotAction_ = toolbar->addAction(QStringLiteral("Delete Snapshot"));
    connect(deleteSnapshotAction_, &QAction::triggered, this, [this] {
        const auto args = snapshotsVm_.selectedDelete();
        if (!args) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = "guard",
                .ok = false,
                .message = "cannot delete: no deletable snapshot selected"});
            return;
        }
        if (askConfirm(QString::fromStdString(args->confirmText)))
            pruneAndPushPending(facade_.deleteSnapshot(args->id));
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

    // Snapshots page: counts banner + fixed-column snapshot list left, snapshot
    // detail pane right — the Phase 7 TUI Snapshots screen, in widgets. beta-06
    // task 3.3 adds the filter field, the Diff/History views and the durable
    // recovery-guidance surface, keeping parity with the TUI tab.
    snapshotsBannerLabel_ = new QLabel;
    snapshotModel_ = new SnapshotListModel(snapshotsVm_, this);
    snapshotsView_ = new QListView;
    snapshotsView_->setModel(snapshotModel_);
    snapshotsView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    snapshotsView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    snapshotFilterEdit_ = new QLineEdit;
    snapshotFilterEdit_->setPlaceholderText(QStringLiteral("filter snapshots…"));
    snapshotFilterEdit_->setAccessibleName(QStringLiteral("Filter snapshots"));
    connect(snapshotFilterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { snapshotsVm_.setFilter(text.toStdString()); });

    // Restore/Delete/Diff double as the list's context menu (the Updates page idiom).
    snapshotsView_->addAction(restoreSnapshotAction_);
    snapshotsView_->addAction(deleteSnapshotAction_);
    snapshotsView_->addAction(diffSnapshotAction_);
    snapshotsView_->setContextMenuPolicy(Qt::ActionsContextMenu);

    snapshotsDetailTree_ = new QTreeWidget;
    snapshotsDetailTree_->setColumnCount(2);
    snapshotsDetailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    snapshotsDetailTree_->setRootIsDecorated(false);
    snapshotsDetailTree_->setSelectionMode(QAbstractItemView::NoSelection);

    // Read-only diff view, fixed font so the shared fixed-column diff rows line
    // up exactly as they do in the terminal.
    snapshotDiffView_ = new QTextEdit;
    snapshotDiffView_->setReadOnly(true);
    snapshotDiffView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    snapshotDiffView_->setAccessibleName(QStringLiteral("Snapshot differences"));

    snapshotsDetailStack_ = new QStackedWidget;
    snapshotsDetailStack_->addWidget(snapshotsDetailTree_);
    snapshotsDetailStack_->addWidget(snapshotDiffView_);

    auto* snapshotsLeft = new QWidget;
    auto* snapshotsLeftLayout = new QVBoxLayout(snapshotsLeft);
    snapshotsLeftLayout->setContentsMargins(0, 0, 0, 0);
    snapshotsLeftLayout->addWidget(snapshotsBannerLabel_);
    snapshotsLeftLayout->addWidget(snapshotFilterEdit_);
    snapshotsLeftLayout->addWidget(snapshotsView_);

    // Guidance sits under both panes: it belongs to the last restore, not to
    // the current selection. Hidden until there is something to recover from.
    snapshotGuidanceLabel_ = new QLabel;
    snapshotGuidanceLabel_->setWordWrap(true);
    snapshotGuidanceLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    snapshotGuidanceLabel_->setAccessibleName(QStringLiteral("Restore recovery guidance"));
    snapshotGuidanceLabel_->hide();

    auto* snapshotsSplitter = new QSplitter;
    snapshotsSplitter->addWidget(snapshotsLeft);
    snapshotsSplitter->addWidget(snapshotsDetailStack_);
    snapshotsSplitter->setStretchFactor(1, 1);

    auto* snapshotsPage = new QWidget;
    auto* snapshotsPageLayout = new QVBoxLayout(snapshotsPage);
    snapshotsPageLayout->setContentsMargins(0, 0, 0, 0);
    snapshotsPageLayout->addWidget(snapshotsSplitter);
    snapshotsPageLayout->addWidget(snapshotGuidanceLabel_);

    tabs_ = new QTabWidget;
    tabs_->addTab(splitter, QStringLiteral("Devices"));
    tabs_->addTab(modulesSplitter, QStringLiteral("Modules"));
    tabs_->addTab(updatesSplitter, tr("Updates"));
    tabs_->addTab(snapshotsPage, tr("Snapshots"));
    setCentralWidget(tabs_);

    // Accessibility pass (beta-06 task 3.5, DESIGN.md §10 + design decision 11).
    // Everything below is presentation-only: names for assistive tech, keyboard
    // shortcuts, a coherent tab order, a minimum window size, and explicit row
    // elision. No behavior or wording changes — the VMs stay the source of truth.

    // Minimum window size: DESIGN.md §3.1 requires the GUI to remain usable at
    // 800x520 (labels may wrap, secondary metadata may elide, but selection,
    // primary actions, and status stay reachable). Qt refuses to shrink below.
    constexpr int kMinWindowWidth = 800;
    constexpr int kMinWindowHeight = 520;
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);

    // Accessible names for the focusable, otherwise-unlabelled controls so
    // assistive technology announces a meaningful name (DESIGN.md §10; the
    // toolbar QActions already carry their visible text as their name). The
    // snapshot filter/diff/guidance names were set at task 3.3 and are kept.
    tabs_->setAccessibleName(QStringLiteral("Views"));
    filterEdit_->setAccessibleName(QStringLiteral("Filter devices"));
    listView_->setAccessibleName(QStringLiteral("Devices"));
    detailTree_->setAccessibleName(QStringLiteral("Device details"));
    moduleFilterEdit_->setAccessibleName(QStringLiteral("Filter modules"));
    modulesView_->setAccessibleName(QStringLiteral("Modules"));
    moduleDetailTree_->setAccessibleName(QStringLiteral("Module details"));
    updatesView_->setAccessibleName(QStringLiteral("Updates"));
    updatesDetailTree_->setAccessibleName(QStringLiteral("Update details"));
    snapshotsView_->setAccessibleName(QStringLiteral("Snapshots"));
    snapshotsDetailTree_->setAccessibleName(QStringLiteral("Snapshot details"));

    // Rows elide long values so a wide name/reason/path never pushes the layout
    // (DESIGN.md §2.4); the full value stays reachable in the detail pane, which
    // renders the VM's complete lines. Explicit here so intent is testable even
    // though ElideRight is the item-view default.
    for (QListView* view : {listView_, modulesView_, updatesView_, snapshotsView_}) {
        view->setTextElideMode(Qt::ElideRight);
        view->setWordWrap(false);
    }

    // Keyboard shortcuts (DESIGN.md §10 keyboard-complete operation): tab
    // switching plus the per-view primary verb. The verb actions are gated to
    // their tab in updateActionEnablement(), so a shortcut fired off-tab is
    // inert rather than acting on the wrong view.
    refreshAction_->setShortcut(QKeySequence::Refresh);              // F5
    toggleAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));  // enable/disable
    loadModuleAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    createSnapshotAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    for (int i = 0; i < tabs_->count(); ++i) {
        // Ctrl+1..Ctrl+4 jump straight to a tab. Parented to the window so Qt
        // owns them; the functor drives the same currentChanged path as a click.
        auto* jump =
            new QShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)), this);
        connect(jump, &QShortcut::activated, this, [this, i] { tabs_->setCurrentIndex(i); });
    }

    // Explicit tab order per page follows the visual reading order — filter,
    // list, detail (DESIGN.md §10). Qt scopes focus traversal to the current
    // page's subtree, so each page's chain is set independently.
    setTabOrder(filterEdit_, listView_);
    setTabOrder(listView_, detailTree_);
    setTabOrder(moduleFilterEdit_, modulesView_);
    setTabOrder(modulesView_, moduleDetailTree_);
    setTabOrder(updatesView_, updatesDetailTree_);
    setTabOrder(snapshotFilterEdit_, snapshotsView_);
    setTabOrder(snapshotsView_, snapshotsDetailStack_);

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
        } else if (index == 3) {
            snapshotsBannerLabel_->setText(QString::fromStdString(snapshotsVm_.banner()));
            snapshotsVm_.rebuild();
            pruneAndPushPending(facade_.refreshSnapshots());
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

    connect(snapshotsView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) snapshotsVm_.selectedRef() = current.row();
                updateSnapshotsDetailPane();
                updateActionEnablement();
            });
    // A requested diff landed. Two consumers: the diff pane repaints, and a
    // pending restore preview finally has content to show — which is when its
    // dialog opens, never before (a modal whose text rewrites itself under the
    // user is worse than a brief wait).
    snapshotsVm_.setDiffReadyHook([this] {
        updateSnapshotsDetailPane();
        if (!pendingPreviewRestoreId_) return;
        const std::string id = *pendingPreviewRestoreId_;
        pendingPreviewRestoreId_.reset();
        updateActionEnablement();  // release the duplicate-submission block
        QString prompt;
        for (const std::string& line : snapshotsVm_.previewLines())
            prompt += QString::fromStdString(line) + QLatin1Char('\n');
        if (askConfirm(prompt.trimmed())) pruneAndPushPending(facade_.restoreSnapshot(id));
    });

    connect(snapshotModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateSnapshotsDetailPane();
        // A snapshot-side reset also lands on the cross-frontend refresh path
        // (a CLI/other-UI mutation → SnapshotsRefreshedEvent → rebuild): keep the
        // counts banner in step with the list, and re-gate the verbs — but only
        // while the Snapshots tab is current (F-1 gating, extended here).
        if (tabs_->currentIndex() == 3) {
            snapshotsBannerLabel_->setText(QString::fromStdString(snapshotsVm_.banner()));
            updateActionEnablement();
        }
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
        } else if (tabs_->currentIndex() == 3) {
            // A create/restore/delete completion wakes here (TaskCompletedEvent →
            // StatusLineVM); the banner reads the rebuilt metas, so refresh it
            // and re-gate the verbs while the Snapshots tab stays current.
            snapshotsBannerLabel_->setText(QString::fromStdString(snapshotsVm_.banner()));
            updateActionEnablement();
        }
    });

    updateDetailPane();  // "(no device selected)" until something is chosen
    updateModuleDetailPane();
    updateUpdatesDetailPane();
    updateSnapshotsDetailPane();
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

void MainWindow::updateSnapshotsDetailPane() {
    // Diff pane wins while it is open; it names the snapshot it belongs to so
    // moving the selection cannot silently re-label it.
    snapshotsDetailStack_->setCurrentWidget(snapshotDiffPaneRequested_
                                                ? static_cast<QWidget*>(snapshotDiffView_)
                                                : static_cast<QWidget*>(snapshotsDetailTree_));
    if (snapshotDiffPaneRequested_) {
        QString text = QStringLiteral("Differences: %1 -> current state\n\n")
                           .arg(QString::fromStdString(core::snapshotShortId(snapshotDiffForId_)));
        for (const std::string& line : snapshotsVm_.diffLines())
            text += QString::fromStdString(line) + QLatin1Char('\n');
        snapshotDiffView_->setPlainText(text);
    }

    // Durable recovery guidance for the last unconverged restore (snapshot-ui
    // spec). Empty ⇒ hidden, so a converged restore leaves no empty box.
    const auto guidance = snapshotsVm_.restoreGuidanceLines();
    QString guidanceText;
    for (const std::string& line : guidance)
        guidanceText += QString::fromStdString(line) + QLatin1Char('\n');
    snapshotGuidanceLabel_->setText(guidanceText.trimmed());
    snapshotGuidanceLabel_->setVisible(!guidance.empty());

    snapshotsDetailTree_->clear();
    for (const std::string& line : snapshotsVm_.detailLines()) {
        // Parented to snapshotsDetailTree_ — the tree deletes its items.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        auto* item = new QTreeWidgetItem(snapshotsDetailTree_);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            item->setText(0, QString::fromStdString(line));
        } else {
            item->setText(0, QString::fromStdString(line.substr(0, colon)).trimmed());
            item->setText(1, QString::fromStdString(line.substr(colon + 1)).trimmed());
        }
    }
    snapshotsDetailTree_->resizeColumnToContents(0);
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

    // Snapshot verbs are gated to the Snapshots tab; the per-selection refusal
    // (placeholder/corrupt/unsupported) is enforced on click and explained on
    // the status line — the TUI parity model, so safety refusals stay visible
    // (DESIGN.md §5.3) rather than being silently greyed out.
    const bool onSnapshots = tab == 3;
    createSnapshotAction_->setEnabled(onSnapshots);
    restoreSnapshotAction_->setEnabled(onSnapshots);
    deleteSnapshotAction_->setEnabled(onSnapshots);
    diffSnapshotAction_->setEnabled(onSnapshots);
    historySnapshotAction_->setEnabled(onSnapshots);

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
