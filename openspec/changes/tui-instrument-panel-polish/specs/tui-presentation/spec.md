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
