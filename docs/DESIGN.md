# Cross-Device Manager Native UI Design

**Status:** Canonical design direction
**Applies to:** `gui/**`, `tui/**`, and user-visible behavior originating in `app/**`
**Audience:** Human contributors and AI coding agents

This document is the source of truth for the product experience. Read it before
designing, implementing, or reviewing GUI or TUI work. Requirements use the
terms **must**, **should**, and **may** deliberately.

The GUI and TUI share workflows and semantics, not pixels. Qt must feel like a
well-made native desktop utility. FTXUI must feel like a first-class terminal
application. Neither surface should imitate a web dashboard or imitate the
other toolkit.

## 1. Product Register

Cross-Device Manager is an operational Linux system tool for inspecting devices,
drivers, and kernel modules and for performing privileged changes safely.

Users are administrators and technically confident desktop users. They scan
dense information, compare current state, and may work primarily from the
keyboard. A mistaken action can disconnect input hardware, affect the root
filesystem, or alter kernel state. The interface therefore values truth and
consequence awareness above novelty.

The intended character is:

- **Precise:** labels name the real object and operation.
- **Calm:** normal system state does not look urgent.
- **Technical:** useful Linux terminology is retained rather than hidden.
- **Trustworthy:** asynchronous work, refusals, and partial failures are visible.
- **Efficient:** repeated inspection and action require little navigation.
- **Quietly distinctive:** modern native structure with restrained terminal
  lineage, never a theatrical "hacker" theme.

The interface is a **product tool**, not a brand or marketing surface. Do not add
hero layouts, promotional copy, feature cards, onboarding carousels, or large
decorative titles.

## 2. Design Principles

### 2.1 System truth before decoration

Show the actual state, its source, and uncertainty. Distinguish `Disabled`,
`Unbound`, `Unavailable`, `Unknown`, and `Loading`; they are not visual aliases.
Never show success before the authoritative operation completes.

### 2.2 Consequences before confirmation

Guard checks happen before authentication. Confirmation text names the exact
device, driver, or module and explains persistence. Advanced one-shot operations
must be visibly different from persistent enable/disable behavior.

### 2.3 Overview first, detail on selection

Keep the established master-detail structure. Lists support scanning and
filtering; the detail pane explains the selected object. Do not turn every
property group into a card or open routine details in modal dialogs.

### 2.4 Stable geometry

Selection, loading text, status changes, and hover/focus states must not resize
the layout. Long identifiers and paths wrap, elide, or scroll within a bounded
region. They must never push actions off-screen or overlap adjacent content.

### 2.5 Native expression, shared semantics

Use Qt Widgets and the active desktop style in the GUI. Use FTXUI components and
terminal conventions in the TUI. Share terminology, ordering, severity, guard
wording, and task state through the existing ViewModels and events.

### 2.6 Restraint is a feature

One strong focus treatment, one accent family, and semantic status colors are
enough. Borders, icons, color, weight, and motion each need a functional reason.

## 3. Experience Architecture

The primary navigation contains two peer views:

1. **Devices** - grouped device list, filter, selection, device details, and
   device/driver actions.
2. **Modules** - security posture, loaded-module list, filter, selection, module
   details, and load/unload actions.

Each view follows the same reading order:

1. View identity and available commands.
2. Security or scope context when relevant.
3. Filter and collection.
4. Selected-object details.
5. Current task or system status.

Do not introduce a dashboard ahead of these views. Device and module management
are the primary experience, not destinations hidden behind a home screen.

### 3.1 Desktop composition

- Use a tab bar for **Devices** and **Modules**.
- Use a horizontal splitter: collection on the left, details on the right.
- Keep the left pane narrower than the details pane but user-resizable.
- Keep global and contextual actions in one stable toolbar region.
- Keep transient task feedback in the status bar; use a dialog only when the
  user must decide or a critical failure requires acknowledgement.
- Preserve splitter position and the last active tab when application settings
  support is introduced.

Target a comfortable layout at `1024x640`. The GUI should remain usable at
`800x520`: labels may wrap and secondary metadata may elide, but primary
actions, selection, and status must remain reachable.

### 3.2 Terminal composition

- At 110 columns or wider, use the established side-by-side list and detail
  panes.
- From 80 through 109 columns, prefer a compact or list/detail switching layout
  over squeezing unreadable columns.
- Below 80 columns or 24 rows, show a concise minimum-size message while keeping
  quit and resize behavior available.
- Keep the status/prompt line at a stable screen edge and one row high.
- Truncate list cells predictably; preserve complete values in the detail view.

Do not assume a fixed 72-column module pane without a narrower fallback. A
terminal resize must not crash, overlap controls, or place content outside the
screen.

## 4. Visual Language

The visual direction is a modern Linux instrument panel with a restrained
terminal echo: crisp separators, compact data, strong selection, and sparing
signal colors. It is not retro pixel art, cyberpunk, or a fake CRT.

### 4.1 Color roles

Colors are semantic roles, not decoration. The application must remain legible
with the desktop palette or terminal theme chosen by the user.

| Role | Light reference | Dark reference | Use |
| --- | --- | --- | --- |
| Canvas | `#F4F6F4` | `#111518` | Window or terminal background |
| Surface | `#FFFFFF` | `#181E22` | Primary panes |
| Raised surface | `#E9EEEB` | `#222A2F` | Inputs, active tool regions |
| Border | `#BCC7C2` | `#3A464C` | Structural separation |
| Primary text | `#17201D` | `#EDF3F1` | Labels and values |
| Muted text | `#5D6A65` | `#A5B1AD` | Secondary metadata |
| Accent | `#0B6F66` | `#61C7B5` | Focus and current selection |
| Accent surface | `#D6ECE7` | `#214E4A` | Selected row background |
| Success | `#267A46` | `#73C991` | Completed operation, healthy result |
| Warning | `#8A5B00` | `#E8B35C` | Risk, suspended enforcement |
| Danger | `#B4232A` | `#F06A6A` | Failure or destructive consequence |
| Information | `#245FA8` | `#74A7E8` | Neutral security or task information |

These values are reference targets, not permission to replace the native Qt
style with a global stylesheet. Prefer `QPalette`, `QStyle`, theme icons, and
semantic widget properties. Support light, dark, and high-contrast system
palettes. Normal text should meet a `4.5:1` contrast ratio; large text,
meaningful icons, focus rings, and state boundaries should meet `3:1`.

For the TUI:

- Default to the terminal's foreground and background.
- Map accent to cyan/teal, success to green, warning to yellow, danger to red,
  and information to blue when those colors are available.
- Pair every color with text, weight, reverse video, a focus marker, or another
  non-color signal.
- Do not emit hand-written ANSI sequences or require a true-color terminal.
- Respect monochrome and `NO_COLOR` operation when support is added.

### 4.2 Typography

The GUI uses the platform UI font intentionally. A native system utility should
not bundle a web font merely to appear designed.

- Use the default UI font for controls, labels, and prose.
- Use the system fixed-width font for module rows, IDs, paths, addresses, and
  other values where character alignment improves scanning.
- Create hierarchy with weight, spacing, and grouping before increasing size.
- View titles may be one step above body text; pane and card-scale display type
  is not appropriate.
- Never use condensed display faces, all-caps paragraphs, or negative letter
  spacing.

The TUI does not control the user's font. Alignment must work in ordinary
monospace terminals and must not depend on icon fonts, emoji width, or ligatures.

### 4.3 Spacing and shape

Use a `4 px` desktop spacing base with `4`, `8`, `12`, `16`, and `24 px` steps.
Prefer native `QStyle` metrics where they differ. Controls in the same task
group are closer than separate groups.

- Pane boundaries may use a one-pixel separator or the native splitter handle.
- Inputs and buttons should use the platform radius; custom surfaces must not
  exceed `6 px` radius.
- Do not place cards inside cards.
- Do not float whole page sections in decorative containers.
- Avoid shadows except where the platform style uses them for menus/dialogs.

In the TUI, one cell is the spacing unit. Use blank rows sparingly. Borders
separate major interactive regions, not every value or line.

### 4.4 Icons

- Prefer desktop theme icons through `QIcon::fromTheme`, with a `QStyle`
  standard-icon fallback where one exists.
- Pair toolbar icons with short text until the command is universally
  recognizable in this product.
- Keep destructive and advanced commands text-labeled.
- Do not draw one-off SVG icons or place every heading beside a rounded icon
  tile.
- In the TUI, use words and ordinary characters. Do not require Nerd Fonts or
  emoji.

### 4.5 Motion

Motion is optional and never carries meaning alone. Native focus, menu, and
dialog transitions are sufficient. Any custom transition should complete in
`120-180 ms`, avoid bounce/elastic easing, and honor reduced-motion settings.
The TUI should animate only an actual in-progress task indicator, not idle
decoration.

## 5. Components and Interaction

### 5.1 Collections

- Keep bus group headers non-selectable and visually quieter than device rows.
- Preserve selection by stable identity across refresh and filtering.
- Use one unmistakable selected-row treatment that remains visible when focus
  moves to the details pane.
- Give keyboard focus a separate visible treatment from selection.
- Empty collection text belongs inside the collection region, not in a dialog.
- Filtering must be incremental and case-insensitive. Show `No devices match
  "..."` or `No modules match "..."`, with a direct way to clear the filter.

Module columns follow this priority when space is constrained:

1. Name
2. Signed state
3. Reference count
4. Size
5. Used by

Drop or move lower-priority fields to details instead of making every column
unreadable.

### 5.2 Details

Details are read-only key/value information grouped by meaning:

- Identity
- Current state
- Connection and sysfs
- Driver
- Security and signature
- Dependencies and holders
- Advanced properties

Use sentence-case group labels. Keep labels stable in a narrow column and allow
values to wrap or horizontally scroll. Paths, IDs, and module names should be
selectable/copyable in the GUI when practical. The TUI must expose the complete
value without relying on a truncated list cell.

When nothing is selected, explain the next action in one sentence:
`Select a device to inspect its properties.` or
`Select a module to inspect its properties.`

Device presentation cosmetics **must** stay consistent across every surface. Bus
names render through one shared `core::displayBus()` helper — acronyms
upper-cased (`USB`, `PCI`), proper nouns title-cased (`Platform`, `Virtio`,
`Other`) — so a list header, a detail row, and status prose never disagree on
casing. Status prose may lower-case that one token for sentence flow, but it
derives from the same helper rather than a second ad-hoc conversion. Detail rows
use a fixed-width label column so every value starts in the same place and no
value abuts its colon.

### 5.3 Toolbar and commands

Order commands by frequency and consequence:

1. Refresh
2. Persistent device enable/disable
3. Additive load/bind commands
4. Destructive unload/unbind commands
5. Advanced commands

Use separators between these groups. Do not color every toolbar action. Danger
color is reserved for an imminent destructive consequence or a failed result,
not for ordinary navigation.

Disabled actions must explain why through a tooltip in Qt and contextual status
text in FTXUI. Hiding an unavailable action is appropriate only when it cannot
apply to the current object type; safety refusals remain visible and explained.

An ellipsis is used only when a command opens a dialog or prompt requiring more
input: `Load module...`, `Bind driver...`. Immediate commands do not use one.

### 5.4 Confirmation

Confirm operations that remove availability or bindings. The prompt must name
the target and distinguish persistent from one-shot behavior.

Preferred patterns:

- `Disable "{device}"? It will remain disabled after reconnecting.`
- `Enable "{device}"?`
- `Unbind "{driver}" from "{device}"? This is not persistent.`
- `Unload "{module}"? Devices using it may stop working.`

The primary button repeats the verb (`Disable`, `Unbind`, `Unload`), not `OK`.
The safer cancel action receives initial focus for destructive operations.
Do not request authentication for an operation already refused by a guard.

### 5.5 Security posture

Secure Boot and kernel lockdown form a compact informational banner on the
Modules view. Normal enabled security is not an error. Escalate to warning only
when it explains a blocked or likely-to-fail operation.

Use explicit text, for example:

`Secure Boot on | Lockdown: integrity | Unsigned modules may be rejected`

Do not use a large red alert for steady-state security configuration.

### 5.6 Destructive-restore preview

Restoring a snapshot **must** open a preview before it runs, not a bare yes/no
confirm. The preview names the snapshot and shows what a restore would change
against the current live state, the selected/current/last-good markers, and a
partial-convergence note when a restore cannot fully converge. The primary
button repeats the verb (`Restore`); the safer cancel keeps initial focus.

The preview's diff is an authoritative round trip, so it **must** be fetched
asynchronously and shown only once it lands — the modal must never rewrite its
own content under the user, and neither UI thread may block on the fetch. While
the fetch is in flight, block a duplicate submission and show the shared loading
text; a failed fetch shows the shared unavailable text rather than a stale diff.
When a restore does not fully converge, durable recovery guidance (the failed
items, the safety snapshot id, and the exact CLI recovery command) **must**
remain visible after the transient status clears — it is not a status message.

| Concept | Qt expression | FTXUI expression |
| --- | --- | --- |
| Restore preview | Modal dialog opened on diff-ready | Modal pane replacing the list |
| Recovery guidance | Persistent label under the panes | Bordered block below the list |

## 6. State Model

Every new workflow must deliberately handle the following states on both
surfaces. Do not implement only the populated success case.

| State | Required presentation |
| --- | --- |
| Initial loading | Keep layout stable; show what is being loaded |
| Refreshing with prior data | Retain prior data and indicate refresh in progress |
| Empty system result | `No supported devices found` or `No modules loaded` |
| No filter matches | Name the filter and offer a clear-filter action |
| No selection | One-line instruction in the details pane |
| Background enrichment | Show `Checking...` or `Unknown`, never a false unsigned result |
| Operation in progress | Disable duplicate submission; identify the target and verb |
| Success | Update authoritative state, then show a concise transient message |
| Guard refusal | Keep the action visible; show the exact safety reason |
| Validation error | Keep entered text when safe and identify the accepted format |
| Partial failure | Keep valid prior data and identify what could not be refreshed |
| Daemon unavailable | Reads remain usable; mutation controls explain unavailability |
| Stale daemon API | State the required API/restart action without generic failure text |
| Enforcement suspended | Warning state with reason; do not display ordinary Disabled |
| Fatal view failure | Stable error region with retry/refresh, not an empty window |

Status messages use sentence case, a concrete subject, and no celebratory copy:

- `Refreshed 18 devices.`
- `Disabled Logitech USB Receiver.`
- `Cannot unload xhci_hcd: used by the only keyboard.`
- `Could not refresh devices; showing the previous result.`

Do not report raw exceptions, errno values, D-Bus names, or filesystem paths as
the only explanation. Preserve diagnostic detail for logs and expose it in an
expandable details region when useful.

### 6.1 Shared state wording

List and detail state text originates in the ViewModels so both surfaces render
the same words; neither frontend invents its own. When any string changes,
change it once in the VM. The current shared list strings:

| View | Empty system result | No filter match |
| --- | --- | --- |
| Devices | `(no devices)` | `(no devices)` |
| Modules | `(no modules)` | `(no matches)` |
| Updates | `(no updates available)` | — (no filter) |
| Snapshots | `(no snapshots)` | `No snapshots match "<filter>"` |

Other shared state text: an unselected device detail reads `(no device
selected)`; a snapshot whose payload cannot be diffed reports `Differences are
unavailable for this snapshot.`; daemon-unavailable and guard refusals surface
through the shared status line, never as a blank list. Loading is the initial
synchronous populate, so the first frame is never empty.

## 7. GUI Rules: Qt 6 Widgets

- Preserve Qt Widgets and model/view. Do not introduce QML or a web view for
  visual polish.
- Prefer native widgets, `QPalette`, `QStyle` metrics, and semantic properties.
  Avoid a monolithic QSS theme that replaces platform behavior.
- Keep all widget access on the GUI thread and all I/O off it.
- Keep ViewModels as the source of truth; Qt models and selection models mirror
  them.
- Use `QAbstractItemView` behavior for keyboard selection, scrolling, and focus
  rather than custom event handling where native behavior suffices.
- Set accessible names/descriptions for icon-only controls, unusual fields,
  status indicators, and custom views.
- Use logical pixels and layouts; never position controls with fixed screen
  coordinates.
- Verify at 100%, 125%, and 200% scale. Text and icons must not clip.
- Long-running actions disable only conflicting controls; inspection and
  cancellation remain available where the operation supports them.
- Use modal dialogs only for confirmation or required input. Routine success
  belongs in the status bar.

## 8. TUI Rules: FTXUI

- Use FTXUI components and decorators; never build presentation with embedded
  ANSI escape strings.
- Every workflow must be complete from the keyboard. Mouse support supplements
  it.
- Show current shortcuts in one contextual header or footer. Do not repeat the
  same shortcut legend in multiple borders.
- Distinguish focus, selection, disabled state, warning, and error without
  relying on color alone.
- Prompts are modal within the TUI: swallow unrelated input, support Escape,
  validate before submission, and restore focus afterward.
- Keep rendering pure and cheap. No sysfs, libkmod, D-Bus, or filesystem work
  occurs during `Render()`.
- Cache derived detail content and invalidate it on selection/model changes.
- Compute widths from the current terminal size. Fixed widths require a tested
  compact fallback.
- Restore the terminal cleanly after normal exit and exceptions.
- Avoid emoji and ambiguous-width Unicode. Standard box drawing may be used only
  with a plain-character fallback when compatibility support is introduced.

## 9. Cross-Surface Parity

Parity means the user receives the same facts, choices, consequences, and
wording. It does not mean forcing identical controls or geometry.

| Concept | Qt expression | FTXUI expression |
| --- | --- | --- |
| Primary navigation | Tabs | `m` view toggle or tab control |
| Filter | Search line edit | Focusable input |
| Selection | Native selected row | Menu selection plus focus marker |
| Details | Read-only key/value view | Bounded detail pane |
| Task feedback | Status bar | Persistent bottom status line |
| Required input | Native input dialog | Modal inline prompt |
| Confirmation | Named native dialog buttons | Modal verb/cancel prompt |
| Disabled action reason | Disabled action plus tooltip/status | Key remains documented; status explains refusal |
| Background loading | Stable placeholder/progress state | Stable text/spinner state |

The following are invariants:

- Visible nouns and verbs match between surfaces.
- Detail fields have the same ordering unless terminal width requires a subset;
  omitted list data remains available in terminal details.
- Safety and daemon refusal wording originates from shared application state.
- Filtering, grouping, identity-preserving selection, and task completion remain
  ViewModel behavior rather than duplicated toolkit logic.
- A feature is not complete until both surfaces support it or the design spec
  explicitly declares a temporary exception.

## 10. Accessibility and Input

- All text and meaningful controls must meet the contrast targets in section 4.
- Never communicate state by color alone.
- The GUI tab order follows visual reading order and does not trap focus.
- Standard desktop shortcuts and navigation continue to work in native views.
- Focus remains visible at all times for keyboard users.
- Destructive actions are not placed adjacent without spacing to common benign
  actions when an accidental click is plausible.
- Text scales with the desktop or terminal configuration; do not force a tiny
  font to fit more rows.
- Dynamic status changes should be exposed to accessibility APIs where Qt
  support permits, without repeatedly interrupting the user.
- TUI commands use ordinary keys and always provide a discoverable way to quit,
  cancel, return, and clear a filter.

### 10.1 Keyboard, shortcuts, and layout minimums

- The GUI provides keyboard shortcuts for tab switching (`Ctrl+1`…`Ctrl+4`) and
  the per-view primary verbs (`F5` refresh, `Ctrl+E` enable/disable, `Ctrl+L`
  load module, `Ctrl+N` create snapshot). Verb shortcuts are gated to their tab,
  so a shortcut fired off-tab is inert rather than acting on a hidden view.
- Every focusable list, tree, and filter carries an accessible name; toolbar
  actions carry their visible text as their name. Icon-only or ambiguous
  controls are not permitted without an accessible name.
- Tab order per page follows the visual reading order: filter, then list, then
  detail.
- The GUI enforces a minimum window size of `800x520` (§3.1) so primary controls
  can never be squeezed off-screen. The TUI shows a concise minimum-size message
  below `80x24` (§3.2), keeping quit and resize live.
- List rows elide long values (`ElideRight`, no wrap); the full value is always
  reachable in the detail pane, which renders the ViewModel's complete lines.

## 11. Anti-Patterns

Do not introduce:

- Web-dashboard layouts, landing-page composition, or a grid of summary cards.
- Cards nested inside cards or borders around every property.
- Purple/blue gradients, gradient text, glassmorphism, glow, or decorative
  background blobs.
- A one-note teal or blue palette; semantic warning, danger, success, and
  information roles remain distinct.
- Oversized headings inside compact panes.
- Rounded rectangles with text where a native icon/control is clearer.
- Cyberpunk neon, fake scanlines, CRT curvature, glitch effects, pixel fonts,
  or terminal decoration that competes with system data.
- A forced dark theme, hard-coded pure black/white surfaces, or assumptions
  about terminal background color.
- Low-contrast muted text or gray text on colored status surfaces.
- Color-only signed/unsigned, enabled/disabled, or success/failure states.
- Hidden safety refusals, generic `Operation failed` messages, or success before
  authoritative completion.
- Blocking I/O on either UI thread.
- GUI/TUI-specific business rules, guard wording, filtering, or identity logic.
- New font, icon, animation, or styling dependencies without a demonstrated
  product need and build/distribution review.

## 12. Validation

Visual quality is a behavior to test, not a final decoration pass.

### 12.1 Automated checks

GUI changes should add focused offscreen tests where applicable for:

- Action enablement, refusal reasons, and confirmation text.
- Model selection and focus behavior across reset/filter operations.
- Empty, loading, unavailable, and error states.
- Accessible names and keyboard reachability.
- No clipped primary controls at the minimum supported window size.

TUI changes should render components to fixed FTXUI screens and verify:

- `120x32`, `100x28`, and `80x24` layouts.
- No row exceeds screen width and no content writes outside screen bounds.
- Focus/selection markers and non-color status labels.
- Empty, loading, prompt, confirmation, refusal, and failure states.
- Resize and modal cancellation behavior.

Prefer structural assertions and small text/layout snapshots over brittle
full-window pixel comparisons tied to one desktop style.

### 12.2 Manual matrix

Before calling a visual change complete, inspect:

- GUI: light, dark, and a high-contrast palette.
- GUI: 100%, 125%, and 200% display scale.
- GUI: `1024x640` and `800x520` windows.
- TUI: true color, 256 color, and monochrome where available.
- TUI: `120x32`, `100x28`, and `80x24` terminals.
- Both: keyboard-only Devices and Modules workflows.
- Both: long device names, long paths, no selection, no matches, daemon down,
  guard refusal, delayed signature lookup, and operation failure.

The Impeccable browser detector and live mode are not validation for this
project: they inspect web markup and CSS, not C++ Qt Widgets or FTXUI output.

## 13. AI Agent Contract

Before changing a user interface, an AI agent must:

1. Read this document and the relevant ViewModel/controller path.
2. Identify the affected states from section 6.
3. Check the corresponding behavior in the other frontend.
4. State whether the change preserves parity or introduces a documented
   temporary exception.
5. Reuse the native toolkit and current architecture before adding an
   abstraction or dependency.

While implementing, an agent must:

- Keep the change scoped to the requested workflow.
- Preserve `app/` and `core/` toolkit independence.
- Use the shared vocabulary and semantic roles in this document.
- Implement real controls and states, not a static visual mockup.
- Add focused behavior/layout coverage proportional to the change.
- Avoid unrelated redesign and generated metadata churn.

Before finishing, an agent must report:

- Which shared states and both surfaces were considered.
- What executable checks ran.
- Which manual visual checks remain and why they could not be automated.

## 14. Design Review Checklist

- [ ] The result reads as a native system utility, not a web page.
- [ ] Device and module facts remain the dominant visual signal.
- [ ] Normal, selected, focused, disabled, loading, warning, and error states
      are distinct without color alone.
- [ ] Dangerous and advanced operations explain consequence and persistence.
- [ ] Qt and FTXUI use native patterns while preserving shared semantics.
- [ ] Long text, minimum sizes, scaling, and terminal resizing do not overlap or
      hide controls.
- [ ] Keyboard-only operation is complete and focus is visible.
- [ ] Empty, partial, unavailable, refusal, and failure states are handled.
- [ ] No blocking work entered a UI/render thread.
- [ ] No new AI-design anti-pattern from section 11 was introduced.
- [ ] Focused automated checks pass and remaining manual checks are named.

## 15. Provenance

This guide adapts useful general principles from the Impeccable design skill -
explicit product context, deliberate hierarchy, complete states, restrained
motion, accessibility, and rejection of generic AI-generated visual patterns -
for this repository's native C++ architecture. Web-only rules, browser live
mode, CSS detectors, mobile platform assumptions, and bundled web typography
are intentionally excluded.

The implementation constraints and parity requirements come from the existing
Phase 1, Phase 3, and Phase 5 design specifications under
`docs/superpowers/specs/`.
