## MODIFIED Requirements

### Requirement: Per-view semantic coloring
Views SHALL color state semantically: Devices — enabled→success, disabled→danger, unavailable→warning, unknown→muted; Modules — signed→success, unsigned→danger, undetermined→muted, blacklisted→warning; Updates — available→information, up-to-date→muted success, a candidate whose own install ran and failed→danger; Snapshots — healthy→default, corrupt→danger, unsupported→warning, with HEAD and last-good markers in accent; status line — success/warning/danger/information by task outcome. The Modules security banner SHALL render as information in steady state and escalate to warning only when it explains a blocked or likely-to-fail operation.

A backend that is unavailable is a state of the source, not a failed operation, and SHALL NOT be colored danger on any view. Availability notes SHALL follow the same calm rule as the security banner: information by default, warning when the backend is present but unreachable or refusing, or when the note explains a verb the user attempted that the unavailability blocks. Availability notes SHALL be bounded to their own region and SHALL NOT render as a full-bleed bar over otherwise readable content.

#### Scenario: Device states colored semantically
- **WHEN** the Devices list renders enabled, disabled, unavailable, and unknown devices in full color mode
- **THEN** each row's state signal uses success, danger, warning, and muted respectively, each paired with its glyph and state text

#### Scenario: Steady-state security is calm
- **WHEN** Secure Boot is on and no operation is blocked
- **THEN** the security banner renders as information, not warning or danger

#### Scenario: Degraded daemon is not danger
- **WHEN** `devmgrd` is unreachable and a view renders with its content still readable
- **THEN** the availability note renders in the warning role within its own bounded region, not in danger and not as a full-bleed bar

#### Scenario: Absent optional provider is information
- **WHEN** the Updates view renders with the `dkms` provider absent
- **THEN** its availability note renders in the information role

## ADDED Requirements

### Requirement: Backend diagnostics region
The TUI SHALL expose raw backend diagnostic text behind a documented key rather than in the default render. The key SHALL be `i`; `d` is not available, being already bound per-view. Pressing it SHALL toggle a diagnostics region listing the raw detail for each currently degraded backend, and pressing it again or `Escape` SHALL close it. The region SHALL be introduced by a muted section header and a rule rather than a border: borders belong to major regions, and a subordinate reveal under the banner it explains is not one. The key SHALL appear in the shortcut legend while a degraded backend is being reported, and SHALL be inert and unlisted otherwise, keeping the single-legend and single-status-line rules intact. With the region closed, no rendered cell SHALL contain raw exception names, D-Bus names, errno values, or filesystem paths.

#### Scenario: Diagnostics stay closed by default
- **WHEN** a view renders with a degraded backend and the diagnostics region has not been opened
- **THEN** the rendered screen contains the translated sentence and no raw backend detail

#### Scenario: Diagnostics toggle open and closed
- **WHEN** the user presses `i` while a backend is degraded, then presses `i` again
- **THEN** the diagnostics region appears under its muted header with the raw detail for each degraded backend, then disappears, and the collection and detail panes keep their structure throughout

#### Scenario: Diagnostics region fits the minimum terminal
- **WHEN** the diagnostics region is open at 80x24 with a long raw diagnostic
- **THEN** no row overflows the terminal width, the raw text elides rather than wrapping the layout, and the legend and status line each remain a single line

### Requirement: Availability notes are identifiable without color
A degraded backend SHALL be identifiable in MONO and PLAIN modes by the ASCII unavailable glyph `?` together with the translated sentence on the same screen, with the semantic role additive only. No availability state SHALL depend on color to be recognized.

#### Scenario: Degraded backend in MONO
- **WHEN** a view renders with a degraded backend in MONO mode at 120x32, 100x28, and 80x24
- **THEN** the `?` glyph and the translated sentence are both present, and the state is recognizable with no color applied

#### Scenario: PLAIN mode keeps the same words
- **WHEN** the same degraded backend renders in PLAIN mode
- **THEN** the sentence is byte-identical to the FULL-mode sentence and only the glyph set and color differ

### Requirement: Banner and note roles come from the ViewModel
The role applied to a banner or availability note SHALL be supplied by the ViewModel alongside the text, and SHALL NOT be derived by matching substrings of the rendered string. Changing the wording of a banner SHALL NOT change its color.

#### Scenario: Rewording does not recolor
- **WHEN** a banner or availability note's wording changes while the underlying state is unchanged
- **THEN** the rendered role is unchanged, because it is read from the ViewModel rather than parsed from the text

#### Scenario: No substring matching in the render path
- **WHEN** the render path resolves the role for a banner or availability note this change touches
- **THEN** it reads a ViewModel-supplied role value and performs no search of the display string
