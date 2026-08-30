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

Tab ownership determines which verbs are *candidates*. A candidate whose backing capability has no implementation on the running platform SHALL NOT be a member of its tab's verb set at all, so the per-tab sets below describe a platform that implements every capability. Platform membership SHALL be decided once from the platform capability descriptor, and SHALL NOT be re-derived per frame or inferred from a failed call.

The per-tab verb sets SHALL be:

| Tab | Verbs, in presentation order |
| --- | --- |
| Devices | `Refresh`, `Enable`/`Disable`, `Bind driver…`, `Unbind driver (advanced)` |
| Modules | `Load Module…`, `Unload` |
| Updates | `Refresh Updates`, `Install Update`, `Dismiss Request` |
| Snapshots | `Create Snapshot…`, `Restore Snapshot…`, `Diff Snapshot`, `History`, `Delete Snapshot` |

#### Scenario: Standing on Devices
- **WHEN** the Devices tab is active on a platform implementing every capability
- **THEN** the visible toolbar actions are exactly `Refresh`, the enable/disable verb, `Bind driver…` and `Unbind driver (advanced)`, in that order, and no Modules, Updates or Snapshots verb is visible

#### Scenario: Standing on Snapshots
- **WHEN** the Snapshots tab is active on a platform implementing every capability
- **THEN** the visible toolbar actions are exactly `Create Snapshot…`, `Restore Snapshot…`, `Diff Snapshot`, `History` and `Delete Snapshot`, in that order, and `Refresh` is not visible

#### Scenario: Switching tabs re-composes the toolbar
- **WHEN** the user switches from Devices to Modules
- **THEN** the Devices verbs become invisible and the Modules verbs become visible in the same toolbar, without the toolbar being rebuilt or replaced

#### Scenario: Read-only platform reduces the Devices set
- **WHEN** the Devices tab is active on a platform whose capability descriptor reports device control and driver management as not implemented
- **THEN** the only visible toolbar action is `Refresh`, and the enable/disable, bind and unbind verbs are absent rather than disabled

#### Scenario: A wholly unsupported tab has an empty verb set
- **WHEN** a tab is active on a platform where every one of its verbs' backing capabilities is not implemented
- **THEN** no verb is visible, the toolbar shows no disabled remnants and no orphan separators, and the tab's content region carries the backend's unsupported explanation

### Requirement: One function owns action presentation
A single presentation function SHALL own visibility, enablement, dynamic text and tooltip for every toolbar action, and SHALL be the only place that sets them. It SHALL run on every input that can change them: tab change, selection change, model reset, backend-availability change, and operation start/completion. Platform capability is not among those inputs because it cannot change within a process; it SHALL be applied once when the toolbar is constructed, and the presentation function SHALL NOT re-derive it.

That function SHALL NOT perform sysfs, libkmod, D-Bus, or filesystem work, and SHALL NOT call any platform backend. It SHALL read only ViewModel and `BackendStatusVM` state already resolved for the current frame, and SHALL NOT author wording for a state the ViewModels or the availability banner already name.

#### Scenario: Presentation is recomputed on every relevant input
- **WHEN** the active tab, the current selection, the model contents, backend availability, or an in-flight operation changes
- **THEN** the presentation function runs and every toolbar action's visibility, enablement, text and tooltip reflect the new state

#### Scenario: Enablement predicates are unchanged
- **WHEN** a verb is visible on its own tab
- **THEN** whether it is enabled is decided by the same predicate as before this change, and its refusal wording is still the shared ViewModel/`BackendStatusVM` text

#### Scenario: Platform-excluded verbs stay excluded
- **WHEN** the presentation function runs repeatedly across tab switches, selection changes, and availability transitions on a platform missing a capability
- **THEN** a verb excluded at construction never becomes visible

### Requirement: Hiding is reserved for inapplicability
A verb SHALL be hidden only when it cannot apply at all — it belongs to another tab, it has no object to act on, or its backing capability has no implementation on the running platform. A verb that belongs to the active tab but is temporarily blocked — by an unreachable `devmgrd`, by an operation already in flight, or by a safety guard — SHALL remain visible, be disabled, and carry the concrete shared reason as its tooltip. Safety refusals SHALL NOT be hidden.

Platform inapplicability and runtime blocking SHALL NOT be conflated. A capability that the running platform does not implement can never become available in this process, so presenting it as a disabled control would assert a reachable state that does not exist; a backend that is merely unreachable can recover, so it stays visible and disabled. The two SHALL be distinguished by the platform capability descriptor rather than by observing a call fail.

`Dismiss Request` SHALL be visible on the Updates tab only while a dismissible request exists, that condition being read from the ViewModel rather than derived in the GUI.

#### Scenario: Daemon unreachable on the active tab
- **WHEN** `devmgrd` is unreachable and the Devices tab is active on a platform that implements device control
- **THEN** the enable/disable verb, `Bind driver…` and `Unbind driver (advanced)` are all still visible, all disabled, and each carries the shared unavailability sentence as its tooltip; `Refresh` remains visible and enabled, reads staying usable while the daemon is down

#### Scenario: Guard refusal stays visible
- **WHEN** a selected device cannot be disabled and a guard supplies the reason
- **THEN** the enable/disable verb remains visible and disabled with the guard's reason as its tooltip

#### Scenario: Nothing to dismiss
- **WHEN** the Updates tab is active and no dismissible request exists
- **THEN** `Dismiss Request` is not visible; and **WHEN** a request arrives, it becomes visible on the same tab

#### Scenario: Unsupported capability hides rather than disables
- **WHEN** the capability descriptor reports a verb's backing capability as not implemented and that verb's tab is active
- **THEN** the verb is not visible, and no disabled control bearing an unsupported tooltip appears anywhere on the toolbar

#### Scenario: Unreachable and unimplemented are told apart without calling
- **WHEN** the presentation function decides between hiding and disabling a verb
- **THEN** it reads the platform capability descriptor and the backend availability state, and it invokes no backend in order to reach the decision

### Requirement: Separator groups without orphans
Visible toolbar actions SHALL be separated into the `docs/DESIGN.md` §5.3 groups — refresh, persistent enable/disable, additive commands, destructive commands, advanced commands. When a group is hidden, no leading, trailing, or consecutive visible separator SHALL remain, so a separator is visible only between two visible actions. This SHALL hold when a group is empty because the running platform does not implement its verbs, including when every group on a tab is empty.

#### Scenario: No orphan separators on any tab
- **WHEN** each of the four tabs is active in turn
- **THEN** the visible toolbar entries neither begin nor end with a separator, and contain no two adjacent separators

#### Scenario: Destructive verbs are separated from benign ones
- **WHEN** the Snapshots tab is active
- **THEN** a separator stands between `Delete Snapshot` and the actions preceding it

#### Scenario: No separators survive an emptied toolbar
- **WHEN** a tab whose every verb is excluded by platform capability is active
- **THEN** the toolbar shows no separator at all

### Requirement: Verb shortcuts are inert off their tab
Existing verb shortcuts (`F5` refresh, `Ctrl+E` enable/disable, `Ctrl+L` load module, `Ctrl+N` create snapshot) SHALL remain bound to their actions, and SHALL have no effect while their tab is not active. Tab-switching shortcuts (`Ctrl+1`…`Ctrl+4`) remain global.

#### Scenario: Refresh shortcut off-tab
- **WHEN** the Modules tab is active and `F5` is pressed
- **THEN** no device refresh is invoked, and the `F5` binding is still present on the `Refresh` action

#### Scenario: Bindings survive tab switching
- **WHEN** the user switches through all four tabs and returns
- **THEN** each verb still carries its original shortcut

A shortcut whose action is excluded because the running platform does not implement its capability SHALL NOT be bound at all, so that pressing it does nothing and it is not advertised anywhere.

#### Scenario: Excluded verbs have no live shortcut
- **WHEN** a verb is excluded by platform capability and its shortcut is pressed on its own tab
- **THEN** nothing happens, no backend is called, and no menu or tooltip advertises that shortcut

### Requirement: Toolbar icons pair with retained text
Toolbar actions SHALL use desktop theme icons via `QIcon::fromTheme` with a `QStyle` standard-icon fallback where a standard name exists, and SHALL keep their visible text. Destructive, advanced and ambiguous commands SHALL NOT become icon-only. Each action's accessible name SHALL remain its visible text. The toolbar SHALL stay usable at the `800x520` minimum window size, relying on Qt's native toolbar overflow rather than compressing or truncating controls.

#### Scenario: Icons never replace labels
- **WHEN** the toolbar renders with a desktop icon theme present
- **THEN** every visible action shows its text label, with an icon beside it only where a standard icon name resolved

#### Scenario: Minimum window size
- **WHEN** the window is at `800x520` with the widest tab's verbs visible
- **THEN** no control is truncated or compressed; controls that do not fit are reachable through the native toolbar extension
