# tui-presentation Delta Specification

## ADDED Requirements

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
Views SHALL color state semantically: Devices — enabled→success, disabled→danger, unavailable→warning, unknown→muted; Modules — signed→success, unsigned→danger, undetermined→muted, blacklisted→warning; Updates — available→information, up-to-date→muted success, error→danger; Snapshots — healthy→default, corrupt→danger, unsupported→warning, with HEAD and last-good markers in accent; status line — success/warning/danger/information by task outcome. The Modules security banner SHALL render as information in steady state and escalate to warning only when it explains a blocked or likely-to-fail operation.

#### Scenario: Device states colored semantically
- **WHEN** the Devices list renders enabled, disabled, unavailable, and unknown devices in full color mode
- **THEN** each row's state signal uses success, danger, warning, and muted respectively, each paired with its glyph and state text

#### Scenario: Steady-state security is calm
- **WHEN** Secure Boot is on and no operation is blocked
- **THEN** the security banner renders as information, not warning or danger

### Requirement: GUI color parity — temporary DESIGN §9 exception
The semantic color introduced here applies to the TUI only; the GUI does not yet color from these roles. This is a temporary docs/DESIGN.md §9 cross-surface parity exception, bounded by §10: because no state is conveyed by color alone, the GUI SHALL continue to convey every state the TUI colors through text and iconography, keeping facts, choices, consequences, and wording at parity — only the additive color differs between surfaces. The per-row state accessors this change adds to the ViewModels SHALL remain GUI-consumable so a later change can adopt the same roles for the GUI and lift this exception without reworking the seam.

#### Scenario: GUI keeps word and fact parity without color
- **WHEN** a device, module, update, or snapshot state that the TUI colors is presented in the GUI
- **THEN** the GUI conveys that state through text and iconography sourced from the same ViewModel state the TUI reads, with no meaning depending on color and no wording divergence

### Requirement: Region border discipline
Borders SHALL appear only on major interactive regions (collection, detail, status); sub-regions SHALL use separators and muted group headers. The TUI SHALL keep exactly one shortcut legend line and one status line, and SHALL preserve the master-detail structure including the 80–109 column list/detail switching layout.

#### Scenario: No border proliferation
- **WHEN** any view renders at 120x32
- **THEN** borders enclose only the major regions and no individual value or sub-group is boxed

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
For components whose unbind or unload risks system usability, the Modules view SHALL render a non-color marker together with a warning-colored, bold name marker and a detail line naming the risk; the marker SHALL appear on the name only and SHALL NOT recolor the enable/disable state glyph or the signed glyph and SHALL NOT use the danger role; the classification SHALL come from a curated core classifier exposed via `ModulesVM::criticalityForRow`; the Devices view MAY expose the same accessor and render it (phase-2).

#### Scenario: Essential module in MONO and PLAIN
- **WHEN** an essential module is rendered in MONO or PLAIN
- **THEN** the marker glyph and the word "Essential" are present without color

#### Scenario: Criticality and signed state coexist
- **WHEN** a module is both essential and unsigned/blacklisted
- **THEN** both signals are visible (warning on the name marker and on the signed glyph, in different columns) without being treated as a collision

#### Scenario: Ordinary module has no marker
- **WHEN** an ordinary module is rendered
- **THEN** no criticality marker appears

#### Scenario: State glyph keeps its own color
- **WHEN** any module is rendered
- **THEN** the +/- state glyph retains its own semantic color regardless of criticality

### Requirement: Muted column headers
The Modules and Updates lists SHALL show exactly one muted, non-selectable header row naming the columns (Modules: `Name Signed Ref Size Used-by`; Updates: `Source Device Version -> New`); the values SHALL NOT be individually bordered; the header SHALL also render in the collapsed 80-column list-only view and the list SHALL still satisfy the 80x24 row budget.

#### Scenario: Header present and non-selectable
- **WHEN** the Modules or Updates list is rendered
- **THEN** the header row is present, muted, and cannot receive selection

#### Scenario: 80x24 row budget holds
- **WHEN** the list is rendered at 80x24 with the header
- **THEN** the header, legend, tab line, status line, and at least one data row all fit

#### Scenario: Header in collapsed list view
- **WHEN** the layout is in the 80-column list-only mode
- **THEN** the header renders in the list (not the detail view)

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
