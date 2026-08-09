## MODIFIED Requirements

### Requirement: Semantic 16-color theme
The TUI SHALL render all status color through a single theme layer mapping docs/DESIGN.md §4.1 roles to 16-color ANSI via FTXUI decorators only: accent→cyan (with inverted video for selection/focus), nominal→green with the dim attribute, success→green, warning→yellow, danger→red, information→blue, muted→dim. The TUI SHALL NOT emit hand-written ANSI escape sequences, true-color, or 256-color output, and SHALL NOT color decoratively (no per-metric color, colored header blocks, gauges, or graphs).

#### Scenario: Roles map to ANSI colors
- **WHEN** a view renders a nominal, success, warning, danger, information, or accent signal in full color mode
- **THEN** the emitted element uses the corresponding 16-color ANSI decorator from the theme, not a hard-coded color or escape string

#### Scenario: Nominal is green plus dim
- **WHEN** a view renders a nominal signal in full color mode
- **THEN** the emitted element carries both the green foreground and the dim attribute, distinguishing it from success (green without dim) and from muted (dim without a hue)

#### Scenario: No decorative color
- **WHEN** any TUI element carries color
- **THEN** the color expresses a §4.1 semantic role for that element's state, and neutral values render in default terminal foreground

### Requirement: Per-view semantic coloring
Views SHALL color state semantically: Devices — enabled→nominal, disabled→muted, transitioning→warning, error→danger, unknown→muted; Modules — signed→nominal, unsigned→danger, undetermined→muted, blacklisted→warning; Updates — available→information, up-to-date→nominal, a candidate whose own install ran and failed→danger; Snapshots — healthy→nominal, corrupt→danger, unsupported→warning, with HEAD and last-good markers in accent taking precedence over nominal; status line — success/warning/danger/information by task outcome. The success role SHALL be reserved for transient task outcomes and SHALL NOT be used for the resting state of a list row. The Modules security banner SHALL render as information in steady state and escalate to warning only when it explains a blocked or likely-to-fail operation.

A backend that is unavailable is a state of the source, not a failed operation, and SHALL NOT be colored danger on any view. Availability notes SHALL follow the same calm rule as the security banner: information by default, warning when the backend is present but unreachable or refusing, or when the note explains a verb the user attempted that the unavailability blocks. Availability notes SHALL be bounded to their own region and SHALL NOT render as a full-bleed bar over otherwise readable content.

#### Scenario: Device states colored semantically
- **WHEN** the Devices list renders enabled, disabled, transitioning, error, and unknown devices in full color mode
- **THEN** each row's state signal uses nominal, muted, warning, danger, and muted respectively, each paired with its glyph and state text

#### Scenario: A list of normal rows is quiet
- **WHEN** every row in Devices, Modules, Updates, or Snapshots is in its normal state in full color mode
- **THEN** no row carries success, warning, or danger, and the view's only paint is the nominal role

#### Scenario: Danger is reserved for faults
- **WHEN** the Devices list renders a disabled device and an errored device on the same screen
- **THEN** the errored row is the only one colored danger, and the disabled row is colored muted

#### Scenario: Success does not describe resting state
- **WHEN** any collection view renders with no operation in flight and no operation recently completed
- **THEN** no row carries the success role

#### Scenario: Snapshot markers outrank nominal
- **WHEN** the Snapshots list renders a healthy row that is also HEAD or last-good
- **THEN** that row takes accent rather than nominal, and healthy rows without a marker take nominal

#### Scenario: Steady-state security is calm
- **WHEN** Secure Boot is on and no operation is blocked
- **THEN** the security banner renders as information, not warning or danger

#### Scenario: Degraded daemon is not danger
- **WHEN** `devmgrd` is unreachable and a view renders with its content still readable
- **THEN** the availability note renders in the warning role within its own bounded region, not in danger and not as a full-bleed bar
