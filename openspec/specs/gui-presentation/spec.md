# gui-presentation Specification

## Purpose

Presentation contract for the desktop frontend: the toolbar presents only the
verbs that belong to the active tab, a single presentation function owns every
action's visibility, enablement, text and tooltip, and hiding is reserved for a
verb that cannot apply at all. A disabled action therefore always means *this
verb applies here and cannot run right now*, with the shared ViewModel or
`BackendStatusVM` wording as its reason. Separator groups follow
docs/DESIGN.md §5.3 and never leave orphans; icons pair with retained text and
never replace it.

## Requirements

### Requirement: Toolbar composition follows the active tab
The GUI toolbar SHALL present only the verbs belonging to the active tab. A verb owned by another tab SHALL be hidden (`QAction::setVisible(false)`), not merely disabled, so that a disabled toolbar action always means *this verb applies here and cannot run right now*. The toolbar region itself SHALL remain a single stable toolbar; per-tab toolbars, stacked toolbars, and rebuilding the toolbar on tab change are not permitted.

The per-tab verb sets SHALL be:

| Tab | Verbs, in presentation order |
| --- | --- |
| Devices | `Refresh`, `Enable`/`Disable`, `Bind driver…`, `Unbind driver (advanced)` |
| Modules | `Load Module…`, `Unload` |
| Updates | `Refresh Updates`, `Install Update`, `Dismiss Request` |
| Snapshots | `Create Snapshot…`, `Restore Snapshot…`, `Diff Snapshot`, `History`, `Delete Snapshot` |

#### Scenario: Standing on Devices
- **WHEN** the Devices tab is active
- **THEN** the visible toolbar actions are exactly `Refresh`, the enable/disable verb, `Bind driver…` and `Unbind driver (advanced)`, in that order, and no Modules, Updates or Snapshots verb is visible

#### Scenario: Standing on Snapshots
- **WHEN** the Snapshots tab is active
- **THEN** the visible toolbar actions are exactly `Create Snapshot…`, `Restore Snapshot…`, `Diff Snapshot`, `History` and `Delete Snapshot`, in that order, and `Refresh` is not visible

#### Scenario: Switching tabs re-composes the toolbar
- **WHEN** the user switches from Devices to Modules
- **THEN** the Devices verbs become invisible and the Modules verbs become visible in the same toolbar, without the toolbar being rebuilt or replaced

### Requirement: One function owns action presentation
A single presentation function SHALL own visibility, enablement, dynamic text and tooltip for every toolbar action, and SHALL be the only place that sets them. It SHALL run on every input that can change them: tab change, selection change, model reset, backend-availability change, and operation start/completion.

That function SHALL NOT perform sysfs, libkmod, D-Bus, or filesystem work. It SHALL read only ViewModel and `BackendStatusVM` state already resolved for the current frame, and SHALL NOT author wording for a state the ViewModels or the availability banner already name.

#### Scenario: Presentation is recomputed on every relevant input
- **WHEN** the active tab, the current selection, the model contents, backend availability, or an in-flight operation changes
- **THEN** the presentation function runs and every toolbar action's visibility, enablement, text and tooltip reflect the new state

#### Scenario: Enablement predicates are unchanged
- **WHEN** a verb is visible on its own tab
- **THEN** whether it is enabled is decided by the same predicate as before this change, and its refusal wording is still the shared ViewModel/`BackendStatusVM` text

### Requirement: Hiding is reserved for inapplicability
A verb SHALL be hidden only when it cannot apply at all — it belongs to another tab, or it has no object to act on. A verb that belongs to the active tab but is temporarily blocked — by an unreachable `devmgrd`, by an operation already in flight, or by a safety guard — SHALL remain visible, be disabled, and carry the concrete shared reason as its tooltip. Safety refusals SHALL NOT be hidden.

`Dismiss Request` SHALL be visible on the Updates tab only while a dismissible request exists, that condition being read from the ViewModel rather than derived in the GUI.

#### Scenario: Daemon unreachable on the active tab
- **WHEN** `devmgrd` is unreachable and the Devices tab is active
- **THEN** the enable/disable verb, `Bind driver…` and `Unbind driver (advanced)` are all still visible, all disabled, and each carries the shared unavailability sentence as its tooltip; `Refresh` remains visible and enabled, reads staying usable while the daemon is down

#### Scenario: Guard refusal stays visible
- **WHEN** a selected device cannot be disabled and a guard supplies the reason
- **THEN** the enable/disable verb remains visible and disabled with the guard's reason as its tooltip

#### Scenario: Nothing to dismiss
- **WHEN** the Updates tab is active and no dismissible request exists
- **THEN** `Dismiss Request` is not visible; and **WHEN** a request arrives, it becomes visible on the same tab

### Requirement: Separator groups without orphans
Visible toolbar actions SHALL be separated into the `docs/DESIGN.md` §5.3 groups — refresh, persistent enable/disable, additive commands, destructive commands, advanced commands. When a group is hidden, no leading, trailing, or consecutive visible separator SHALL remain, so a separator is visible only between two visible actions.

#### Scenario: No orphan separators on any tab
- **WHEN** each of the four tabs is active in turn
- **THEN** the visible toolbar entries neither begin nor end with a separator, and contain no two adjacent separators

#### Scenario: Destructive verbs are separated from benign ones
- **WHEN** the Snapshots tab is active
- **THEN** a separator stands between `Delete Snapshot` and the actions preceding it

### Requirement: Verb shortcuts are inert off their tab
Existing verb shortcuts (`F5` refresh, `Ctrl+E` enable/disable, `Ctrl+L` load module, `Ctrl+N` create snapshot) SHALL remain bound to their actions, and SHALL have no effect while their tab is not active. Tab-switching shortcuts (`Ctrl+1`…`Ctrl+4`) remain global.

#### Scenario: Refresh shortcut off-tab
- **WHEN** the Modules tab is active and `F5` is pressed
- **THEN** no device refresh is invoked, and the `F5` binding is still present on the `Refresh` action

#### Scenario: Bindings survive tab switching
- **WHEN** the user switches through all four tabs and returns
- **THEN** each verb still carries its original shortcut

### Requirement: Toolbar icons pair with retained text
Toolbar actions SHALL use desktop theme icons via `QIcon::fromTheme` with a `QStyle` standard-icon fallback where a standard name exists, and SHALL keep their visible text. Destructive, advanced and ambiguous commands SHALL NOT become icon-only. Each action's accessible name SHALL remain its visible text. The toolbar SHALL stay usable at the `800x520` minimum window size, relying on Qt's native toolbar overflow rather than compressing or truncating controls.

#### Scenario: Icons never replace labels
- **WHEN** the toolbar renders with a desktop icon theme present
- **THEN** every visible action shows its text label, with an icon beside it only where a standard icon name resolved

#### Scenario: Minimum window size
- **WHEN** the window is at `800x520` with the widest tab's verbs visible
- **THEN** no control is truncated or compressed; controls that do not fit are reachable through the native toolbar extension
