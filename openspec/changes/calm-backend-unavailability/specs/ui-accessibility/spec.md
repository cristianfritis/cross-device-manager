## MODIFIED Requirements

### Requirement: Defined loading, empty, and error states
Every list and detail view in both UIs SHALL define all three states — loading, empty, and error — with the same wording across GUI and TUI per docs/DESIGN.md. Blank panels with no explanation are not permitted.

Raw backend detail SHALL NOT be presented as the explanation. Exception names, D-Bus names, errno values, and filesystem paths SHALL NOT appear in any primary widget or pane of either UI; the explanation SHALL be the shared translated sentence for that backend, with the raw detail preserved in the log and reachable through an expandable diagnostic affordance.

The empty state and the source-unreachable state SHALL be distinguishable and SHALL NOT co-occur. A view's empty-state string asserts a query that completed and returned nothing, so it SHALL NOT render while a source feeding that view is unreachable.

#### Scenario: Daemon down on any view
- **WHEN** any view loads while the backend it reads from is unreachable
- **THEN** both UIs show the same explanatory error state sourced from shared application state, not an empty list and not a blank panel

#### Scenario: Updates tab with fwupd unreachable
- **WHEN** the Updates view loads on a system where the `fwupd` service is not responding
- **THEN** both UIs show the shared translated sentence for `fwupd`, no primary widget or pane text matches `org\.freedesktop`, `DBus\.Error`, `ServiceUnknown`, or `errno`, and `(no updates available)` is not shown

#### Scenario: Empty is not confused with unreachable
- **WHEN** a view renders while one of its sources is unreachable
- **THEN** that view's empty-state string is absent from the render, and the degraded explanation is present instead

## ADDED Requirements

### Requirement: Backend diagnostics are keyboard-reachable
The expandable diagnostic that carries raw backend detail SHALL be operable without a pointer on both surfaces, and SHALL carry an accessible name in the GUI. A hover-only affordance such as a bare tooltip is not sufficient, because it leaves the diagnostic unreachable for keyboard and assistive-technology users. The affordance SHALL be discoverable: labelled in the GUI, and present in the TUI's shortcut legend while a degraded backend is being reported.

#### Scenario: Diagnostic opens without a pointer
- **WHEN** a user operating either UI with the keyboard alone encounters a degraded backend note
- **THEN** they can reach and open the diagnostic affordance, read the raw detail, and close it again without a mouse

#### Scenario: Diagnostic affordance announces itself
- **WHEN** the GUI renders the diagnostic disclosure control
- **THEN** the control carries an accessible name describing what it reveals, and is reachable in the tab order

#### Scenario: TUI advertises the diagnostics key
- **WHEN** the TUI is reporting a degraded backend
- **THEN** the shortcut legend documents the diagnostics key, and the key toggles the diagnostics region open and closed
