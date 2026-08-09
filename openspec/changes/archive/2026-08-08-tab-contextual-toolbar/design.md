## Context

`MainWindow` builds fourteen `QAction`s into one `QToolBar` in its constructor
(`gui/src/main_window.cpp:89-288`) and then re-runs `updateActionEnablement()`
on every tab switch, selection change, model reset, backend-availability change
and operation completion. That function only ever calls `setEnabled()`. Nothing
is ever hidden, so all fourteen verbs stand on every tab and disabled-ness
carries two unrelated meanings at once:

- *wrong tab* — `Install Update` greyed while standing on Devices;
- *applies here, cannot run now* — `Disable` greyed because a guard refused it,
  or because `devmgrd` is unreachable and `gateOnDaemon()` attached the shared
  unavailability sentence as a tooltip.

The second meaning is the one `calm-backend-unavailability` invested in. It is
diluted by the first.

The TUI has no equivalent problem: each view composes its own legend
(`tui/src/views/*_view.cpp` → `views::fitLegend`), so only that view's keys are
advertised. One gap remains there — `d=dismiss` is listed on Updates even when
no dismissible request exists, which is a key that cannot do anything rather
than a key that is refusing.

`docs/DESIGN.md` §5.3 (hide only what cannot apply; explain everything else),
§5.3 command ordering with separators, §4.4 icons, and §10.1 (verb shortcuts
gated to their tab) already specify the target state. This change implements it.

## Goals / Non-Goals

**Goals:**

- The toolbar shows only the active tab's verbs; a verb belonging to another tab
  is *absent*, not greyed.
- One function owns the complete presentation of every action — visibility,
  enablement, dynamic text, tooltip — and is the only place that decides them.
- Separators group the visible verbs by frequency/consequence per §5.3, with no
  leading, trailing, or doubled separator once a group is hidden.
- Theme icons where a standard name exists, text always retained.
- Verb shortcuts are inert off their tab.
- The TUI legend stops advertising a key that has no target at all, while keys
  that a guard would refuse stay listed and explain themselves on the status
  line.

**Non-Goals:**

- No change to any verb's enablement predicate, wording, confirmation text, or
  shortcut binding. This change decides *which verbs are present*, never
  *whether a present verb is enabled*.
- No new toolbar, no per-tab `QToolBar` instances, no `QStackedWidget` of
  toolbars — the toolbar region stays geometrically stable.
- No orientation/empty-state rework in either frontend, and no TUI compact
  list/detail mode. Both were considered and deliberately parked: the first is a
  four-view state-rendering audit, the second would replace the shipped 44/34
  split at 80 columns and force the parked `DESIGN.md` §3.2 reconciliation.
- No VM, core, or daemon change. Presentation only.

## Decisions

### D1 — Visibility rides on `QAction::setData()`, not a side table

Each action is tagged at construction with the index of the tab that owns it
(`action->setData(tabIndex)`). The presentation pass then reads
`toolbar_->actions()` in order and sets `setVisible(a->data().toInt() == tab)`.

*Alternative considered:* a `std::vector<std::pair<QAction*, int>>` member, or a
`switch` per tab listing its actions. Both restate the ownership that the
construction site already knows, and both drift the moment an action is added.
`setData()` keeps the fact adjacent to the `addAction()` call, and the pass over
`toolbar_->actions()` cannot miss an action that was added but not registered.

### D2 — Construction order *is* the §5.3 order

Only one tab's actions are visible at a time, so the toolbar's single linear
order can satisfy every tab's order simultaneously: actions are added grouped by
owning tab, and within a tab in §5.3 order (refresh → enable/disable → additive
→ destructive → advanced). Two existing pairs are transposed to achieve this
(`Bind` before `Unbind`; `Refresh Updates` before `Install Update`). The
connect-lambdas move with their actions and are otherwise untouched.

*Alternative considered:* reordering actions dynamically per tab with
`insertAction()`. Rejected — it mutates toolbar structure on every tab change
for no user-visible gain, and makes the rendered order unverifiable from the
construction site.

### D3 — Separators are static, their visibility is derived

Separators are added between groups at construction and then shown only when
they sit between two visible actions. One pass over `toolbar_->actions()`:
remember the last separator seen after at least one visible action, and reveal
it when the next visible action arrives. This yields exactly one visible
separator between two visible groups, and none leading or trailing, without the
pass knowing which tab is active.

*Alternative considered:* `QToolBar::clear()` + re-add per tab. Rejected —
`clear()` deletes separator actions it created, the churn is per-tab-switch, and
test accessors hold action pointers across it.

### D4 — `Dismiss Request` is hidden when nothing is dismissible

A dismissible request either exists or does not; when it does not, the verb has
no object, which is exactly the §5.3 case where hiding is correct. It stays
visible-and-disabled-with-a-reason only for the cases that *are* explainable
(daemon-backed refusals on other verbs). This is the one action whose visibility
depends on more than the active tab, and it reads its condition from the VM
(`updatesVm_.requestBanner().empty()`) — no new rule is derived in the GUI.

### D5 — Off-tab shortcuts stay bound and become inert

The four shortcuts (`F5`, `Ctrl+E`, `Ctrl+L`, `Ctrl+N`) remain attached to their
actions. Off-tab those actions are both hidden and disabled, which is what makes
the shortcut inert — Qt does not deliver a shortcut to a disabled or hidden
action. Nothing is unbound and rebound on tab change, so the binding cannot be
lost by a mis-sequenced update.

### D6 — Icons only where a freedesktop name exists

`QIcon::fromTheme` with a `QStyle::standardIcon` fallback, applied to the five
commands with an unambiguous standard name (`view-refresh` ×2,
`system-software-update`, `edit-delete`, `window-close`). Everything else stays
text-only per §4.4. The toolbar uses `Qt::ToolButtonTextBesideIcon`, so text is
never replaced by an icon — destructive and advanced commands keep their labels,
and every action's accessible name remains its visible text.

### D7 — TUI: gate the legend entry on the same VM fact the view already has

`renderUpdatesView` already receives `requestBanner`. The `d=dismiss` entry is
appended only when it is non-empty — the same shape as the existing
`i=diagnostics` gate one line above it. `Render()` gains no work and no new
input. The `d` key handler in `tui_app.cpp` is left alone: `dismissRequest()` on
nothing is already a no-op, and rejecting the key would be a new rule.

## Risks / Trade-offs

- **A verb silently disappears from every tab** (a wrong `setData` tag) →
  the per-tab visibility matrix test asserts the *complete* visible set for each
  of the four tabs, so an action that vanishes fails the tab that owns it.
- **`Refresh` no longer reachable on Modules/Snapshots**, where it previously
  refreshed devices from any tab → intended: it acted on a view the user was not
  looking at. Devices keeps it, Updates keeps its own. Accepted per the scoped
  verb list.
- **Existing tests assert only `isEnabled()`** and would keep passing while
  visibility regressed → those assertions are extended to `isVisible()` in the
  same change rather than left as a trailing gap.
- **Toolbar overflow at 800x520** → the toolbar keeps Qt's native extension
  button; nothing is compressed or elided by us. Fewer visible actions per tab
  makes overflow less likely than today, not more.
- **Ambiguous-shortcut warnings** if two visible actions ever share a key →
  cannot occur while each shortcut belongs to exactly one tab's action; no
  shortcut is duplicated by this change.
