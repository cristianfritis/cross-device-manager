# tui-presentation Specification

## Purpose

Presentation contract for the terminal frontend: a semantic 16-color theme with
capability degradation, ASCII-first status glyphs, pure per-view render
functions over ViewModel-owned state, canonical device naming, criticality
marking, and fixed-screen render test coverage. Color is additive only — every
state it expresses is also carried by a glyph and a word, which is what keeps
the GUI at fact and wording parity while the docs/DESIGN.md §9 color exception
stands.

## Requirements

### Requirement: Semantic 16-color theme
The TUI SHALL render all status color through a single theme layer mapping docs/DESIGN.md §4.1 roles to 16-color ANSI via FTXUI decorators only: accent→cyan (with inverted video for selection/focus), success→green, warning→yellow, danger→red, information→blue, muted→dim. The TUI SHALL NOT emit hand-written ANSI escape sequences, true-color, or 256-color output, and SHALL NOT color decoratively (no per-metric color, colored header blocks, gauges, or graphs).

#### Scenario: Roles map to ANSI colors
- **WHEN** a view renders a success, warning, danger, information, or accent signal in full color mode
- **THEN** the emitted element uses the corresponding 16-color ANSI decorator from the theme, not a hard-coded color or escape string

#### Scenario: No decorative color
- **WHEN** any TUI element carries color
- **THEN** the color expresses a §4.1 semantic role for that element's state, and neutral values render in default terminal foreground

### Requirement: Color capability degradation
The TUI SHALL resolve a color capability mode at startup — full, mono, or plain — from `NO_COLOR` (env), `--no-color`/`--ascii` flags, and `TERM=dumb`. In mono and plain modes, color decorators SHALL become identity functions, and every state signal SHALL remain distinguishable through its paired glyph and text. In plain mode, borders SHALL degrade to ASCII characters.

#### Scenario: NO_COLOR honored
- **WHEN** the TUI starts with `NO_COLOR` set
- **THEN** no color decorators are applied and every state remains identifiable by glyph and text alone

#### Scenario: TERM=dumb forces plain
- **WHEN** the TUI starts with `TERM=dumb`
- **THEN** plain mode is active: no color and ASCII-only borders and glyphs

### Requirement: Status glyph policy
Status glyphs SHALL default to ASCII (`+` enabled, `-` disabled, `?` unavailable, `!` unsigned). Unicode glyphs (● ○ ◉) SHALL be available only as an opt-in behind the capability flag, and mono/plain modes SHALL always use ASCII. Every glyph SHALL accompany existing text; no state is conveyed by glyph or color alone.

#### Scenario: ASCII default
- **WHEN** the TUI renders device or module state with default settings
- **THEN** state glyphs are the ASCII set and each glyph appears alongside the textual state word

#### Scenario: Unicode is opt-in only
- **WHEN** the user has not opted into Unicode glyphs
- **THEN** no Unicode status glyph is emitted in any mode

### Requirement: Pure per-view render decomposition
Each TUI view (tab bar, status bar, Devices, Modules, Updates, Snapshots) SHALL be a pure render function of the form `render(const XxxVM&, const Theme&, Size) -> ftxui::Element` performing no sysfs, libkmod, D-Bus, or filesystem work. The application shell SHALL compose these functions; view rendering SHALL NOT depend on shell-local mutable state.

#### Scenario: Render is side-effect free
- **WHEN** a view render function is invoked with a fixed ViewModel snapshot, theme, and size
- **THEN** it returns an element without performing I/O, and repeated invocation with the same inputs yields the same output

### Requirement: VM-owned per-row state seam
The per-row and per-outcome state the TUI colors from SHALL originate in the ViewModels through read-only accessors, not from parsing rendered row strings in the frontend. Each list ViewModel SHALL expose the state it already computes for a given row (device status, module signed state, update state, snapshot health/markers), and the status ViewModel SHALL expose the severity of its current message; accessors SHALL return an empty/absent value for header, placeholder, and out-of-range rows. The state→color-role mapping SHALL live in the TUI presentation layer, keeping the ViewModels free of toolkit color semantics.

#### Scenario: Header rows carry no state
- **WHEN** the TUI queries the per-row state accessor for a group-header or placeholder row
- **THEN** the accessor returns an absent value and the row renders without a state color or glyph

#### Scenario: State comes from the VM, not the string
- **WHEN** a device row's displayed text changes wording but its underlying status is unchanged
- **THEN** the row's color is unaffected because it is derived from the ViewModel state accessor, not the row text

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

### Requirement: GUI color parity — temporary DESIGN §9 exception
The semantic color introduced here applies to the TUI only; the GUI does not yet color from these roles. This is a temporary docs/DESIGN.md §9 cross-surface parity exception, bounded by §10: because no state is conveyed by color alone, the GUI SHALL continue to convey every state the TUI colors through text and iconography, keeping facts, choices, consequences, and wording at parity — only the additive color differs between surfaces. The per-row state accessors this change adds to the ViewModels SHALL remain GUI-consumable so a later change can adopt the same roles for the GUI and lift this exception without reworking the seam.

#### Scenario: GUI keeps word and fact parity without color
- **WHEN** a device, module, update, or snapshot state that the TUI colors is presented in the GUI
- **THEN** the GUI conveys that state through text and iconography sourced from the same ViewModel state the TUI reads, with no meaning depending on color and no wording divergence

### Requirement: Region border discipline
Borders SHALL appear only on major interactive regions (collection, detail, status); sub-regions SHALL use separators and muted group headers. The TUI SHALL keep exactly one shortcut legend line and one status line, and SHALL preserve the master-detail structure at every size it renders — both panes render at 80 columns and above, and below the DESIGN §3.2 minimum the app shows the minimum-size notice instead; there is no width-driven list/detail switching layout in the build.

#### Scenario: No border proliferation
- **WHEN** any view renders at 120x32
- **THEN** borders enclose only the major regions and no individual value or sub-group is boxed

### Requirement: Master-detail split keeps both panes legible
The width of the collection pane SHALL be derived from the terminal width by a pure function, not fixed, so that the detail pane retains enough columns to render its lines at every width the app renders at; the collection pane SHALL take its preferred width whenever the terminal affords both, and SHALL yield width to the detail pane rather than starve it when the terminal is narrow. Where a narrow terminal forces a collection row to clip, the columns it drops SHALL remain available in the detail pane for the selected row.

#### Scenario: Detail pane is readable at the minimum size
- **WHEN** a view whose collection rows carry long identifiers renders at 80x24
- **THEN** the detail pane renders its lines as readable text rather than eliding every line to an ellipsis, and the collection pane keeps the leading columns of its rows

#### Scenario: Preferred width when the terminal affords it
- **WHEN** the terminal is wide enough for both the collection pane's preferred width and the detail pane's minimum
- **THEN** the collection pane renders at its preferred width and the detail pane takes the remainder

### Requirement: Fixed-screen render test coverage
The TUI SHALL have automated render tests in `tui/tests/`, wired into ctest, covering: theme role→color mapping and capability downgrade; each view rendered to fixed `ftxui::Screen` sizes 120x32, 100x28, and 80x24 with no row exceeding screen width and no out-of-bounds writes; glyph and text presence in mono mode; selection/focus markers; and the states matrix (empty, loading, prompt, confirmation, refusal, failure). Format and clang-tidy gates SHALL cover `tui/src` and `tui/tests`.

#### Scenario: Width safety at all tested sizes
- **WHEN** each view's render tests run at 120x32, 100x28, and 80x24
- **THEN** no rendered row exceeds the screen width and no content is written outside screen bounds

#### Scenario: Mono proves color independence
- **WHEN** a view is rendered in mono mode in tests
- **THEN** assertions find the state glyph and state text for every represented state

### Requirement: Canonical device names
The system SHALL display a canonical vendor+product device name, produced by `core::displayDeviceName(...)` from fields the daemon already provides (no bundled hardware database, no frontend parsing of identifiers), as the primary device label in both the TUI and the GUI; the raw bus address and VID:PID SHALL be shown as secondary muted text; the device detail SHALL present `Name:` (canonical), `Address:`, `VID:PID:`, and `Id:` rows; where no canonical name resolves, the system SHALL fall back to the raw identifier without error.

#### Scenario: Device with a canonical name
- **WHEN** a device resolves to a vendor+product name
- **THEN** the list primary label and the detail `Name:` show that name and the raw id appears only as secondary muted text

#### Scenario: Device without a canonical name
- **WHEN** no canonical name resolves
- **THEN** the raw identifier is shown as the label and no error is raised

#### Scenario: GUI and TUI detail parity
- **WHEN** the same device is selected in the GUI and the TUI
- **THEN** both surfaces show the identical canonical name from the same VM/core field

#### Scenario: Long canonical name
- **WHEN** the canonical name overflows the list region
- **THEN** it feeds the bounded reveal rather than wrapping or overflowing

### Requirement: Active tab distinguishable without color
The tab bar SHALL make the currently active tab unmistakable without color: in FULL mode via accent color + bold, and in MONO/PLAIN via bold + an ASCII width-safe marker that is distinct from the focus marker and the criticality marker; the `(m: next tab)` legend SHALL be retained; the active tab SHALL NOT use the warning (yellow) role.

#### Scenario: Each tab active in MONO and FULL
- **WHEN** each of the four tabs is the active tab
- **THEN** in MONO/PLAIN the ASCII marker is present and in FULL the accent color is present

#### Scenario: Active tab never yellow
- **WHEN** any tab is active
- **THEN** its highlight is not rendered with the warning/yellow role

#### Scenario: Markers are distinct
- **WHEN** focus, an essential row, and the active tab are all visible
- **THEN** the focus marker (`>`), the criticality marker (`#`), and the active-tab marker (bold + bracket change) are three different glyphs

### Requirement: Bounded reveal of overflowing selected names
On selection or focus of a row whose name overflows its region, the system SHALL reveal the elided portion of that name by a bounded, finite horizontal reveal that comes to rest; the system SHALL NOT run a perpetual idle animation for this; non-selected rows SHALL elide right; the reveal SHALL be bounded within the list region and SHALL work at 80 columns. The reveal helper SHALL be a pure function of an explicit offset so it is deterministically renderable off-screen.

#### Scenario: Long selected name reveals then rests
- **WHEN** a row with an overflowing name is selected at 80/100/120 columns
- **THEN** the elided tail becomes visible over a finite sequence of offsets and then rests, with element width never exceeding the region at any offset

#### Scenario: Short name is static
- **WHEN** the selected name fits the region
- **THEN** no reveal runs and the row is static

#### Scenario: At rest, no out-of-bounds write
- **WHEN** the reveal is at rest (offset 0 and offset max)
- **THEN** the rendered element width is within the region and no content writes outside screen bounds

#### Scenario: Mono and plain parity
- **WHEN** the reveal runs in MONO or PLAIN
- **THEN** behaviour is identical to FULL (the reveal is text/offset only)

### Requirement: Per-row criticality marker
For components whose unbind or unload risks system usability, the Modules view SHALL render a warning-colored, bold marker element adjacent to the name, carrying a non-color marker glyph, plus a detail line naming the concrete risk; the marker element SHALL own its own color and SHALL NOT recolor the name, the enable/disable state glyph, or the signed glyph, and SHALL NOT use the danger role; the classification SHALL come from a curated core classifier exposed via `ModulesVM::criticalityForRow`; the Devices view MAY expose the same accessor and render it (phase-2).

#### Scenario: Essential module in MONO and PLAIN
- **WHEN** an essential module is selected in MONO or PLAIN
- **THEN** the marker glyph is present on its list row AND the criticality word is present in the detail pane on the same screen, with no color anywhere

#### Scenario: Criticality and signed state coexist
- **WHEN** a module is both essential and unsigned/blacklisted
- **THEN** both signals are visible (warning on the criticality marker element and on the signed glyph, in different columns) without being treated as a collision

#### Scenario: Ordinary module has no marker
- **WHEN** an ordinary module is rendered
- **THEN** no criticality marker appears

#### Scenario: State glyph keeps its own color
- **WHEN** any module is rendered
- **THEN** the +/- state glyph retains its own semantic color regardless of criticality

### Requirement: Muted column headers
The Modules and Updates lists SHALL show exactly one muted, non-selectable header row naming the columns (Modules: `Name Signed Ref Size Used-by`; Updates: `Source Device Version -> New`); the values SHALL NOT be individually bordered; the header SHALL render in every layout the build produces — there is no collapsed list-only view: at 80 columns and above both panes render, and below the DESIGN §3.2 minimum the app shows the minimum-size notice instead — and the list SHALL still satisfy the 80x24 row budget.

#### Scenario: Header present and non-selectable
- **WHEN** the Modules or Updates list is rendered
- **THEN** the header row is present, muted, and cannot receive selection

#### Scenario: 80x24 row budget holds
- **WHEN** the list is rendered at 80x24 with the header
- **THEN** the header, legend, tab line, status line, and at least one data row all fit

#### Scenario: Header at the 80-column minimum
- **WHEN** the Modules or Updates list is rendered at exactly 80x24
- **THEN** the header renders in the list pane alongside the detail pane

#### Scenario: Mono and plain text
- **WHEN** the header is rendered in MONO or PLAIN
- **THEN** it is plain muted text with no color dependence

### Requirement: Canonical name and criticality text parity
Canonical device names and criticality marker text SHALL be shared facts rendered on both surfaces from the same shared VM/core fields; only their color is TUI-only under the DESIGN §9 temporary parity exception.

#### Scenario: GUI shows the same canonical name
- **WHEN** a device is shown in the GUI list and detail
- **THEN** the canonical name matches the TUI for the same device

#### Scenario: GUI shows criticality text without color
- **WHEN** an essential component is shown in the GUI
- **THEN** the criticality text is present; no color is required

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
