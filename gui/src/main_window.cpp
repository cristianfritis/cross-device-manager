#include "gui/src/main_window.hpp"

#include <algorithm>
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
#include <QHBoxLayout>
#include <QIcon>
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
#include <QStyle>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "devmgr/core/events.hpp"
#include "devmgr/core/models.hpp"

namespace devmgr::gui {
namespace {
// Tab indices, named where the toolbar tags each verb with its owner. The
// QTabWidget order is Devices | Modules | Updates | Snapshots.
constexpr int kDevicesTab = 0;
constexpr int kModulesTab = 1;
constexpr int kUpdatesTab = 2;
constexpr int kSnapshotsTab = 3;

// A desktop theme icon with a QStyle standard-icon fallback (DESIGN.md §4.4).
// If neither resolves, the action stays text-only: this product draws no one-off
// artwork, and the label is the real affordance either way.
void applyThemeIcon(QAction* action, const char* themeName, QStyle::StandardPixmap fallback,
                    const QWidget& owner) {
    QIcon icon = QIcon::fromTheme(QString::fromLatin1(themeName));
    if (icon.isNull()) icon = owner.style()->standardIcon(fallback);
    if (!icon.isNull()) action->setIcon(icon);
}

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

    toolbar_ = addToolBar(QStringLiteral("main"));
    toolbar_->setMovable(false);
    // Text stays beside every icon (§4.4): destructive and advanced commands are
    // never reduced to a glyph, and an action's visible text is the accessible
    // name assistive technology reads (§10.1).
    toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Each verb is tagged with the tab that owns it, so updateActionPresentation()
    // can decide visibility by walking toolbar_->actions() rather than consulting
    // a second table that could drift from this construction site. The toolbar's
    // single linear order is grouped by owning tab and, within a tab, by §5.3
    // frequency and consequence — only one tab's run is ever visible, so one
    // order serves all four. Separators mark the group boundaries; a separator
    // whose neighbours are hidden is suppressed by updateToolbarSeparators().
    const auto verb = [this](int tab, const QString& text) {
        QAction* action = toolbar_->addAction(text);
        action->setData(tab);
        return action;
    };

    // ----- Devices: refresh, persistent enable/disable, bind, unbind ----------
    refreshAction_ = verb(kDevicesTab, QStringLiteral("Refresh"));
    applyThemeIcon(refreshAction_, "view-refresh", QStyle::SP_BrowserReload, *this);
    connect(refreshAction_, &QAction::triggered, this, [this] { actions_.onRefresh(); });

    toolbar_->addSeparator();
    toggleAction_ = verb(kDevicesTab, QStringLiteral("Disable"));
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

    toolbar_->addSeparator();
    bindAction_ = verb(kDevicesTab, QStringLiteral("Bind driver…"));
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

    toolbar_->addSeparator();
    unbindAction_ = verb(kDevicesTab, QStringLiteral("Unbind driver (advanced)"));
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

    // ----- Modules: additive load, then destructive unload --------------------
    loadModuleAction_ = verb(kModulesTab, QStringLiteral("Load Module…"));
    connect(loadModuleAction_, &QAction::triggered, this, [this] {
        const QString name = actions_.textInput
                                 ? actions_.textInput(QStringLiteral("Load module"), QString{})
                                 : QInputDialog::getText(this, QStringLiteral("Load module"),
                                                         QStringLiteral("Module name:"));
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!name.isEmpty() && valid.match(name).hasMatch())
            actions_.onLoadModule(name.toStdString());
    });

    toolbar_->addSeparator();
    unloadModuleAction_ = verb(kModulesTab, QStringLiteral("Unload"));
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

    // ----- Updates: refresh, install, dismiss ---------------------------------
    refreshUpdatesAction_ = verb(kUpdatesTab, QStringLiteral("Refresh Updates"));
    applyThemeIcon(refreshUpdatesAction_, "view-refresh", QStyle::SP_BrowserReload, *this);
    connect(refreshUpdatesAction_, &QAction::triggered, this,
            [this] { pruneAndPushPending(facade_.refreshUpdates()); });

    toolbar_->addSeparator();
    installUpdateAction_ = verb(kUpdatesTab, QStringLiteral("Install Update"));
    applyThemeIcon(installUpdateAction_, "system-software-update", QStyle::SP_ArrowDown, *this);
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

    toolbar_->addSeparator();
    dismissRequestAction_ = verb(kUpdatesTab, QStringLiteral("Dismiss Request"));
    applyThemeIcon(dismissRequestAction_, "window-close", QStyle::SP_DialogCloseButton, *this);
    connect(dismissRequestAction_, &QAction::triggered, this, [this] {
        updatesVm_.dismissRequest();
        updateRequestBannerLabel();
        updateActionPresentation();  // the dismissed request was this verb's object
    });

    // Snapshots verbs (Phase 7) — the TUI s/r/x keys as toolbar actions. Like
    // the Updates commands, they call the facade directly and keep custody in
    // pending_ (this window's own set, drained in the destructor). The ellipsis
    // on Create marks the label prompt (DESIGN.md §5.3).
    // ----- Snapshots: create, restore, inspect, delete -------------------------
    createSnapshotAction_ = verb(kSnapshotsTab, QStringLiteral("Create Snapshot…"));
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
    toolbar_->addSeparator();
    restoreSnapshotAction_ = verb(kSnapshotsTab, QStringLiteral("Restore Snapshot…"));
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

    // Diff and History inspect rows already on screen — the advanced group, kept
    // apart from the mutations above and the deletion below (§5.3, §10).
    toolbar_->addSeparator();
    diffSnapshotAction_ = verb(kSnapshotsTab, QStringLiteral("Diff Snapshot"));
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

    historySnapshotAction_ = verb(kSnapshotsTab, QStringLiteral("History"));
    historySnapshotAction_->setCheckable(true);
    connect(historySnapshotAction_, &QAction::triggered, this,
            [this](bool on) { snapshotsVm_.setHistoryView(on); });

    toolbar_->addSeparator();
    deleteSnapshotAction_ = verb(kSnapshotsTab, QStringLiteral("Delete Snapshot"));
    applyThemeIcon(deleteSnapshotAction_, "edit-delete", QStyle::SP_TrashIcon, *this);
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

    // devmgrd owns every mutation verb on this page and the disabled-state
    // overlay behind its rows, so an unreachable daemon is explained here too
    // (§14 F1). The row collapses to nothing while the daemon is serving.
    const AvailabilityBanner devicesBanner = makeAvailabilityBanner();
    devicesBannerLabel_ = devicesBanner.label;
    devicesDetailsButton_ = devicesBanner.details;
    devicesDiagnosticLabel_ = devicesBanner.diagnostic;
    devicesBannerLabel_->setVisible(false);
    connect(devicesDetailsButton_, &QToolButton::toggled, this,
            [this](bool) { updateDevicesDisclosure(); });

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(devicesBanner.row);
    leftLayout->addWidget(devicesDiagnosticLabel_);
    leftLayout->addWidget(filterEdit_);
    leftLayout->addWidget(listView_);

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(detailTree_);
    splitter->setStretchFactor(1, 1);

    // Modules page: banner + filter + fixed-column list left, detail right —
    // the T11 TUI screen, in widgets.
    const AvailabilityBanner modulesBanner = makeAvailabilityBanner();
    bannerLabel_ = modulesBanner.label;
    modulesDetailsButton_ = modulesBanner.details;
    modulesDiagnosticLabel_ = modulesBanner.diagnostic;
    connect(modulesDetailsButton_, &QToolButton::toggled, this,
            [this](bool) { updateModulesDisclosure(); });
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
    modulesLeftLayout->addWidget(modulesBanner.row);
    modulesLeftLayout->addWidget(modulesDiagnosticLabel_);
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
    // Disclosure for the raw backend detail (backend-availability spec: the
    // diagnostic is preserved but never primary). A checkable button rather than
    // a tooltip: a tooltip is pointer-only, so the detail would be unreachable
    // by keyboard and assistive technology. Both it and the region below appear
    // only while a backend is degraded.
    updatesDetailsButton_ = new QToolButton;
    updatesDetailsButton_->setCheckable(true);
    updatesDetailsButton_->setText(QStringLiteral("Details ▾"));
    updatesDetailsButton_->setAccessibleName(QStringLiteral("Backend diagnostics"));
    updatesDetailsButton_->setAccessibleDescription(
        QStringLiteral("Show the raw diagnostic text for an unavailable backend"));
    updatesDetailsButton_->setFocusPolicy(Qt::StrongFocus);  // in the tab order
    updatesDetailsButton_->setVisible(false);
    updatesDiagnosticLabel_ = new QLabel;
    updatesDiagnosticLabel_->setWordWrap(true);
    // Read-only, but selectable by keyboard as well as mouse so the text can be
    // read and copied without a pointer.
    updatesDiagnosticLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                     Qt::TextSelectableByKeyboard);
    updatesDiagnosticLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    updatesDiagnosticLabel_->setVisible(false);
    connect(updatesDetailsButton_, &QToolButton::toggled, this,
            [this](bool) { updateAvailabilityDisclosure(); });
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
    // Banner row: the sentence, then the disclosure pushed to the trailing edge.
    // The revealed region sits directly under it, so the detail reads as
    // belonging to the note rather than floating in the page.
    auto* updatesBannerRow = new QWidget;
    auto* updatesBannerRowLayout = new QHBoxLayout(updatesBannerRow);
    updatesBannerRowLayout->setContentsMargins(0, 0, 0, 0);
    updatesBannerRowLayout->addWidget(updatesBannerLabel_, 1);
    updatesBannerRowLayout->addWidget(updatesDetailsButton_, 0);
    updatesLeftLayout->addWidget(updatesBannerRow);
    updatesLeftLayout->addWidget(updatesDiagnosticLabel_);
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
    snapshotsBannerLabel_->setVisible(false);  // no counts to show until the tab is entered (B4)
    // The Updates disclosure, verbatim, for the backend this page reads: the
    // snapshot list comes from devmgrd, so an unreachable daemon is explained
    // here and its raw detail is reachable by keyboard here (§13).
    snapshotsDetailsButton_ = new QToolButton;
    snapshotsDetailsButton_->setCheckable(true);
    snapshotsDetailsButton_->setText(QStringLiteral("Details ▾"));
    snapshotsDetailsButton_->setAccessibleName(QStringLiteral("Backend diagnostics"));
    snapshotsDetailsButton_->setAccessibleDescription(
        QStringLiteral("Show the raw diagnostic text for an unavailable backend"));
    snapshotsDetailsButton_->setFocusPolicy(Qt::StrongFocus);  // in the tab order
    snapshotsDetailsButton_->setVisible(false);
    snapshotsDiagnosticLabel_ = new QLabel;
    snapshotsDiagnosticLabel_->setWordWrap(true);
    snapshotsDiagnosticLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                       Qt::TextSelectableByKeyboard);
    snapshotsDiagnosticLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    snapshotsDiagnosticLabel_->setVisible(false);
    connect(snapshotsDetailsButton_, &QToolButton::toggled, this,
            [this](bool) { updateSnapshotsDisclosure(); });
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
    // Banner row shaped exactly like the Updates page's: sentence, then the
    // disclosure at the trailing edge, then the revealed region under it.
    auto* snapshotsBannerRow = new QWidget;
    auto* snapshotsBannerRowLayout = new QHBoxLayout(snapshotsBannerRow);
    snapshotsBannerRowLayout->setContentsMargins(0, 0, 0, 0);
    snapshotsBannerRowLayout->addWidget(snapshotsBannerLabel_, 1);
    snapshotsBannerRowLayout->addWidget(snapshotsDetailsButton_, 0);
    snapshotsLeftLayout->addWidget(snapshotsBannerRow);
    snapshotsLeftLayout->addWidget(snapshotsDiagnosticLabel_);
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
    // their tab in updateActionPresentation(), so a shortcut fired off-tab is
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
        updateActionPresentation();
        // Tab-aware composition (review I-1): entering Updates must fold
        // installProgressText() in; leaving it must fall back to the shared
        // status line — same as the taskExecuted wake path below.
        updateStatusBar();
        if (index == 0) {
            updateDevicesBannerLabel();
        } else if (index == 1) {
            updateModulesBannerLabel();
            modulesVm_.rebuild();
            modulesVm_.fillSignatures();
        } else if (index == 2) {
            updateUpdatesBannerLabel();
            updateRequestBannerLabel();
            updatesVm_.rebuild();
            pruneAndPushPending(facade_.refreshUpdates());
        } else if (index == 3) {
            updateSnapshotsBannerLabel();
            snapshotsVm_.rebuild();
            pruneAndPushPending(facade_.refreshSnapshots());
        }
    });

    // View selection mirrors the VM (headers can't be selected — model flags).
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) listVm_.selectedRef() = current.row();
                updateDetailPane();
                updateActionPresentation();
            });
    // After a rebuild the VM has re-resolved the selection by DeviceId; the
    // reset cleared the view's currentIndex, so re-apply the VM's row and
    // rebuild the detail pane (properties may have changed under the same id).
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        syncSelectionFromVm();
        updateDetailPane();
        updateActionPresentation();
        // A device-side reset is also how a daemon outage (or its recovery)
        // reaches this page, so the note follows the list it explains.
        if (tabs_->currentIndex() == 0) updateDevicesBannerLabel();
    });

    connect(modulesView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) modulesVm_.selectedRef() = current.row();
                updateModuleDetailPane();
                updateActionPresentation();
            });
    connect(moduleModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateModuleDetailPane();
        // Module-side resets must not re-run the Devices-tab criticality probe
        // (reads /proc/self/mounts + sysfs) — Phase 5 review F-1.
        if (tabs_->currentIndex() == 1) {
            updateActionPresentation();
            updateModulesBannerLabel();
        }
    });

    connect(updatesView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) updatesVm_.selectedRef() = current.row();
                updateUpdatesDetailPane();
                updateActionPresentation();
            });
    connect(updateModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateUpdatesDetailPane();
        // Update-side resets must not re-run the Devices/Modules-tab logic —
        // Phase 5 review F-1, extended to the Updates tab.
        if (tabs_->currentIndex() == 2) updateActionPresentation();
    });

    connect(snapshotsView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) snapshotsVm_.selectedRef() = current.row();
                updateSnapshotsDetailPane();
                updateActionPresentation();
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
        updateActionPresentation();  // release the duplicate-submission block
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
            updateSnapshotsBannerLabel();
            updateActionPresentation();
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
        if (tabs_->currentIndex() == 0) {
            updateDevicesBannerLabel();
        } else if (tabs_->currentIndex() == 1) {
            updateModulesBannerLabel();
        } else if (tabs_->currentIndex() == 2) {
            updateUpdatesBannerLabel();
            updateRequestBannerLabel();
            updateActionPresentation();
        } else if (tabs_->currentIndex() == 3) {
            // A create/restore/delete completion wakes here (TaskCompletedEvent →
            // StatusLineVM); the banner reads the rebuilt metas, so refresh it
            // and re-gate the verbs while the Snapshots tab stays current.
            updateSnapshotsBannerLabel();
            updateActionPresentation();
        }
    });

    updateDetailPane();  // "(no device selected)" until something is chosen
    updateModuleDetailPane();
    updateUpdatesDetailPane();
    updateSnapshotsDetailPane();
    updateActionPresentation();
    // Devices is the tab the window opens on, so `currentChanged` never fires
    // for it and its note would stay unpainted until the user left and came
    // back — the daemon can already be unreachable at construction.
    updateDevicesBannerLabel();
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

// The three widgets every availability banner row needs, wired identically:
// a sentence label, a checkable disclosure at the trailing edge, and the
// read-only region it reveals. A checkable button rather than a tooltip because
// a tooltip is pointer-only, so the diagnostic would be unreachable by keyboard
// and assistive technology.
// Same Qt parent-child ownership the constructor documents: every widget here
// is owned by the layout or the window that adopts it, which gsl::owner cannot
// model — see the NOLINT rationale on the constructor above.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
MainWindow::AvailabilityBanner MainWindow::makeAvailabilityBanner() {
    AvailabilityBanner b;
    b.label = new QLabel;
    b.details = new QToolButton;
    b.details->setCheckable(true);
    b.details->setText(QStringLiteral("Details ▾"));
    b.details->setAccessibleName(QStringLiteral("Backend diagnostics"));
    b.details->setAccessibleDescription(
        QStringLiteral("Show the raw diagnostic text for an unavailable backend"));
    b.details->setFocusPolicy(Qt::StrongFocus);  // in the tab order
    b.details->setVisible(false);
    b.diagnostic = new QLabel;
    b.diagnostic->setWordWrap(true);
    // Read-only, but selectable by keyboard as well as mouse so the text can be
    // read and copied without a pointer.
    b.diagnostic->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    b.diagnostic->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    b.diagnostic->setVisible(false);
    b.row = new QWidget;
    auto* rowLayout = new QHBoxLayout(b.row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addWidget(b.label, 1);
    rowLayout->addWidget(b.details, 0);
    return b;
}
// NOLINTEND(cppcoreguidelines-owning-memory)

// Devices and Modules carry the same devmgrd note the Snapshots page does: the
// daemon owns every mutation verb on both, so a user looking at dimmed controls
// is owed the reason on the tab they are standing on (§14 F1/F2). The words are
// the VM's, the role rides on weight and the glyph, never colour.
void MainWindow::updateDevicesBannerLabel() {
    const auto notes = listVm_.availabilityNotes();
    const bool degraded = !notes.empty();
    const bool warn = std::ranges::any_of(
        notes, [](const app::BackendNote& n) { return n.role == app::StatusSeverity::Warning; });
    std::string banner;
    for (const auto& n : notes) {
        if (!banner.empty()) banner += " | ";
        banner += n.text;
    }
    devicesBannerLabel_->setText(
        QString::fromStdString(degraded ? std::string("? ") + banner : banner));
    QFont font = devicesBannerLabel_->font();
    font.setBold(warn);
    devicesBannerLabel_->setFont(font);
    devicesBannerLabel_->setVisible(degraded);  // healthy ⇒ no row at all, as in the TUI
    devicesDetailsButton_->setVisible(degraded);
    if (!degraded) devicesDetailsButton_->setChecked(false);
    updateDevicesDisclosure();
}

void MainWindow::updateDevicesDisclosure() {
    const auto lines = app::diagnosticLines(listVm_.availabilityNotes());
    QString text;
    for (const auto& line : lines) {
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += QString::fromStdString(line);
    }
    devicesDiagnosticLabel_->setText(text);
    const bool open = devicesDetailsButton_->isChecked() && !lines.empty();
    devicesDiagnosticLabel_->setVisible(open);
    devicesDetailsButton_->setText(open ? QStringLiteral("Details ▴")
                                        : QStringLiteral("Details ▾"));
}

// ModulesVM::bannerLine() carries text AND severity from one read — the seam
// task 7.1 built. The GUI used to take the plain banner() string, so the role
// never arrived and the glyph, the weight and the disclosure were all missing.
void MainWindow::updateModulesBannerLabel() {
    const app::BannerLine line = modulesVm_.bannerLine();
    const bool degraded = !modulesVm_.availabilityNotes().empty();
    bannerLabel_->setText(
        QString::fromStdString(degraded ? std::string("? ") + line.text : line.text));
    QFont font = bannerLabel_->font();
    font.setBold(line.severity == app::StatusSeverity::Warning);
    bannerLabel_->setFont(font);
    bannerLabel_->setVisible(!line.text.empty());
    modulesDetailsButton_->setVisible(degraded);
    if (!degraded) modulesDetailsButton_->setChecked(false);
    updateModulesDisclosure();
}

void MainWindow::updateModulesDisclosure() {
    const auto lines = app::diagnosticLines(modulesVm_.availabilityNotes());
    QString text;
    for (const auto& line : lines) {
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += QString::fromStdString(line);
    }
    modulesDiagnosticLabel_->setText(text);
    const bool open = modulesDetailsButton_->isChecked() && !lines.empty();
    modulesDiagnosticLabel_->setVisible(open);
    modulesDetailsButton_->setText(open ? QStringLiteral("Details ▴")
                                        : QStringLiteral("Details ▾"));
}

// The counts banner is empty exactly when the store is empty, and the list's
// "(no snapshots)" placeholder is then the single empty indicator (pass-2 bug
// B4) — hide the label rather than reserve a blank row for it, matching how the
// TUI drops the banner row and how the durable request banner above behaves.
void MainWindow::updateSnapshotsBannerLabel() {
    const auto notes = snapshotsVm_.availabilityNotes();
    const bool degraded = !notes.empty();
    const bool warn = std::ranges::any_of(
        notes, [](const app::BackendNote& n) { return n.role == app::StatusSeverity::Warning; });
    const std::string banner = snapshotsVm_.banner();
    // Same treatment as the Updates banner: weight and the "unavailable" glyph
    // carry the role, never colour (docs/DESIGN.md §9 GUI colour exception).
    snapshotsBannerLabel_->setText(
        QString::fromStdString(degraded ? std::string("? ") + banner : banner));
    QFont font = snapshotsBannerLabel_->font();
    font.setBold(warn);
    snapshotsBannerLabel_->setFont(font);
    snapshotsBannerLabel_->setVisible(!banner.empty());
    snapshotsDetailsButton_->setVisible(degraded);
    if (!degraded) snapshotsDetailsButton_->setChecked(false);  // no stale region left open
    updateSnapshotsDisclosure();
}

void MainWindow::updateSnapshotsDisclosure() {
    const auto lines = app::diagnosticLines(snapshotsVm_.availabilityNotes());
    QString text;
    for (const auto& line : lines) {
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += QString::fromStdString(line);
    }
    snapshotsDiagnosticLabel_->setText(text);
    const bool open = snapshotsDetailsButton_->isChecked() && !lines.empty();
    snapshotsDiagnosticLabel_->setVisible(open);
    snapshotsDetailsButton_->setText(open ? QStringLiteral("Details ▴")
                                          : QStringLiteral("Details ▾"));
}

// The Updates banner and its disclosure.
//
// The words are UpdatesVM's — the GUI adds no wording of its own. Role is
// carried by WEIGHT and the documented "unavailable" glyph, never by colour:
// docs/DESIGN.md §9's GUI colour exception stands, so a degraded backend has to
// be identifiable with the palette untouched.
void MainWindow::updateUpdatesBannerLabel() {
    const auto notes = updatesVm_.availabilityNotes();
    const bool degraded = !notes.empty();
    const bool warn = std::ranges::any_of(
        notes, [](const app::BackendNote& n) { return n.role == app::StatusSeverity::Warning; });
    const std::string banner = updatesVm_.banner();
    updatesBannerLabel_->setText(
        QString::fromStdString(degraded ? std::string("? ") + banner : banner));
    QFont font = updatesBannerLabel_->font();
    font.setBold(warn);
    updatesBannerLabel_->setFont(font);
    updatesDetailsButton_->setVisible(degraded);
    // A backend that recovered must not leave a stale region open behind it.
    if (!degraded) updatesDetailsButton_->setChecked(false);
    updateAvailabilityDisclosure();
}

void MainWindow::updateAvailabilityDisclosure() {
    const auto lines = app::diagnosticLines(updatesVm_.availabilityNotes());
    QString text;
    for (const auto& line : lines) {
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += QString::fromStdString(line);
    }
    updatesDiagnosticLabel_->setText(text);
    const bool open = updatesDetailsButton_->isChecked() && !lines.empty();
    updatesDiagnosticLabel_->setVisible(open);
    updatesDetailsButton_->setText(open ? QStringLiteral("Details ▴")
                                        : QStringLiteral("Details ▾"));
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

// The single owner of every toolbar action's presentation. It reads only state
// the ViewModels and BackendStatusVM have already resolved for this frame — no
// sysfs, libkmod, D-Bus or filesystem work happens here, and no wording is
// authored that a VM or the availability banner already owns.
void MainWindow::updateActionPresentation() {
    const int tab = tabs_->currentIndex();
    // First: which verbs are even on offer. A verb from another tab is absent,
    // which is what lets `disabled` below keep its single meaning.
    applyTabVisibility(tab);
    // Every verb below reaches the system through devmgrd. While it cannot be
    // reached they stay VISIBLE and become disabled with the reason attached
    // (docs/DESIGN.md §5.3: hide only what cannot apply to the object at all;
    // explain everything else). The reason is the SHARED sentence, escalated by
    // noteFor(..., blocksAttemptedVerb) — a control never authors its own
    // wording for a state the banner already names.
    const auto blocked =
        facade_.backendStatus().noteFor(core::BackendId::Devmgrd, /*blocksAttemptedVerb=*/true);
    const QString blockedReason = blocked ? QString::fromStdString(blocked->text) : QString{};
    const bool daemonUp = !blocked.has_value();

    // Refresh re-reads the device list, so it belongs to Devices and to no other
    // tab — it used to fire from all four and refresh a view the user was not
    // looking at. A read stays usable while the daemon is degraded (§6), so it is
    // deliberately not gated on daemonUp.
    refreshAction_->setEnabled(tab == kDevicesTab);

    const bool onModules = tab == kModulesTab;
    gateOnDaemon(loadModuleAction_, onModules, daemonUp, blockedReason);
    gateOnDaemon(unloadModuleAction_, onModules && modulesVm_.selectedModule().has_value(),
                 daemonUp, blockedReason);

    const bool onUpdates = tab == kUpdatesTab;
    installUpdateAction_->setEnabled(onUpdates && updatesVm_.selectedInstall().has_value());
    refreshUpdatesAction_->setEnabled(onUpdates);
    dismissRequestAction_->setEnabled(onUpdates && !updatesVm_.requestBanner().empty());

    // Snapshot verbs are gated to the Snapshots tab; the per-selection refusal
    // (placeholder/corrupt/unsupported) is enforced on click and explained on
    // the status line — the TUI parity model, so safety refusals stay visible
    // (DESIGN.md §5.3) rather than being silently greyed out.
    const bool onSnapshots = tab == kSnapshotsTab;
    gateOnDaemon(createSnapshotAction_, onSnapshots, daemonUp, blockedReason);
    gateOnDaemon(restoreSnapshotAction_, onSnapshots, daemonUp, blockedReason);
    gateOnDaemon(deleteSnapshotAction_, onSnapshots, daemonUp, blockedReason);
    // The diff is an IPC read, also the daemon's.
    gateOnDaemon(diffSnapshotAction_, onSnapshots, daemonUp, blockedReason);
    // History is a local view toggle over rows already on screen — it needs no
    // daemon, so a degraded daemon must not disable it.
    historySnapshotAction_->setEnabled(onSnapshots);

    updateDeviceVerbEnablement(daemonUp, blockedReason);
    // Last: the group rules only make sense once every visibility above is
    // settled, or a separator would be revealed next to a verb about to vanish.
    updateToolbarSeparators();
}

// A verb owned by another tab is HIDDEN, not greyed (docs/DESIGN.md §5.3): with
// fourteen verbs standing on every tab, `disabled` meant both "wrong tab" and
// "applies here, cannot run right now", and the second — the one carrying the
// shared unavailability sentence and the guard reasons — was the one that lost.
// The owning tab was recorded on each action at construction, so this pass needs
// no table of its own and cannot miss a verb that was added later.
void MainWindow::applyTabVisibility(int tab) {
    for (QAction* action : toolbar_->actions()) {
        if (action->isSeparator()) continue;
        action->setVisible(action->data().toInt() == tab);
    }
    // The one verb whose object can be missing while its own tab is active: with
    // no dismissible request there is nothing to dismiss, which is exactly the
    // §5.3 case where hiding is right rather than explaining. The condition is
    // the VM's own — the GUI derives no rule here.
    if (tab == kUpdatesTab) dismissRequestAction_->setVisible(!updatesVm_.requestBanner().empty());
}

// A separator earns its pixels only between two visible actions. Revealing one
// when the NEXT visible action arrives handles the leading, trailing and doubled
// cases in a single pass, without this function knowing which tab is active or
// which groups it shows.
void MainWindow::updateToolbarSeparators() {
    QAction* pending = nullptr;  // last separator seen after a visible action
    bool sawVisible = false;
    for (QAction* action : toolbar_->actions()) {
        if (action->isSeparator()) {
            action->setVisible(false);
            if (sawVisible) pending = action;
            continue;
        }
        if (!action->isVisible()) continue;
        if (pending != nullptr) {
            pending->setVisible(true);
            pending = nullptr;
        }
        sawVisible = true;
    }
}

// The Devices-tab half: toggle/unbind/bind depend on the SELECTED device, so
// they need the findById()/canDisable() probes the other tabs must not pay for
// (T1 F-1 gating). Split out of updateActionPresentation so each half stays
// readable — and under the analyzer's complexity threshold — on its own.
void MainWindow::updateDeviceVerbEnablement(bool daemonUp, const QString& blockedReason) {
    const auto id = listVm_.selectedDeviceId();
    const auto device =
        (tabs_->currentIndex() == kDevicesTab && id) ? facade_.findById(*id) : std::nullopt;
    if (!device) {  // not on the Devices tab, or no device selected
        toggleAction_->setEnabled(false);
        toggleAction_->setText(QStringLiteral("Disable"));
        toggleAction_->setToolTip({});
        unbindAction_->setEnabled(false);
        bindAction_->setEnabled(false);
        return;
    }
    gateOnDaemon(unbindAction_, true, daemonUp, blockedReason);
    gateOnDaemon(bindAction_, true, daemonUp, blockedReason);
    const bool enable = device->status == core::DeviceStatus::Disabled;
    toggleAction_->setText(enable ? QStringLiteral("Enable") : QStringLiteral("Disable"));
    if (!enable) {
        // Advisory only — devmgrd re-checks authoritatively on every request.
        // A guard refusal outranks the availability note: it is the more
        // specific reason, and it is the one that survives the daemon coming
        // back.
        const auto verdict = facade_.canDisable(*id);
        if (!verdict.allowed) {
            toggleAction_->setEnabled(false);
            toggleAction_->setToolTip(QString::fromStdString("cannot disable: " + verdict.reason));
            return;
        }
        gateOnDaemon(toggleAction_, true, daemonUp, blockedReason);
        if (daemonUp) toggleAction_->setToolTip({});
        return;
    }
    gateOnDaemon(toggleAction_, true, daemonUp, blockedReason);
    if (daemonUp) toggleAction_->setToolTip({});
}

// A daemon-backed verb stays VISIBLE and becomes disabled with the SHARED
// sentence attached (docs/DESIGN.md §5.3: hide only what cannot apply to the
// object at all; explain everything else). A control never authors its own
// wording for a state the banner already names.
void MainWindow::gateOnDaemon(QAction* action, bool enabled, bool daemonUp,
                              const QString& blockedReason) {
    action->setEnabled(enabled && daemonUp);
    if (!daemonUp) action->setToolTip(blockedReason);
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
