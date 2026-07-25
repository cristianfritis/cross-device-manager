## ADDED Requirements

### Requirement: Shared per-backend unavailability wording
When a backend (the `devmgrd` helper, the `fwupd` update provider, the `dkms` status provider, or any future source) cannot serve a view, the sentence shown to the user SHALL originate from one shared wording table in `core`, resolved as a pure function of the backend identity and an unavailability kind. The raw `core::Error::message` SHALL NOT be an input to that function, so a D-Bus name, errno value, exception name, or filesystem path cannot structurally reach a user-visible sentence. The unavailability kind SHALL be derived from `core::Error::Code`: `NotFound` → absent, `Io` → unreachable, `Permission` → not permitted, `Unsupported` → unsupported. Both frontends SHALL read the sentence through a single ViewModel accessor and SHALL NOT reword, prefix, or suffix it.

The current shared sentences are:

| Backend | Kind | Sentence |
| --- | --- | --- |
| devmgrd | unreachable | `Device service unavailable — showing read-only system state.` |
| fwupd | unreachable | `Firmware updates unavailable — the fwupd service is not responding.` |
| dkms | absent | `DKMS status unavailable — DKMS is not installed on this system.` |

#### Scenario: Both surfaces show the same sentence for the same backend
- **WHEN** the same backend is unavailable with the same kind and the GUI and TUI each render their degraded state
- **THEN** the text each surface displays is byte-identical and is the value returned by the shared ViewModel accessor

#### Scenario: Raw backend detail cannot reach the sentence
- **WHEN** a backend reports an error whose message contains a D-Bus name, an errno value, or a filesystem path
- **THEN** the resolved sentence is the table entry for that backend and kind, and contains none of the error message's text

#### Scenario: Unknown backend or kind still yields a sentence
- **WHEN** a backend or kind has no specific entry in the table
- **THEN** a calm generic sentence naming the backend is returned rather than an empty string, and no raw diagnostic is substituted

### Requirement: Unavailability severity and escalation
A degraded backend SHALL be presented as information by default. It SHALL escalate to warning in exactly two cases: the backend is present but unreachable or refusing (kind `unreachable` or `not permitted`), or the note is explaining a verb the user attempted that the unavailability blocks. A backend that is simply absent (kind `absent`, an optional service that was never installed) SHALL remain information indefinitely. The danger role SHALL NOT be reachable for backend unavailability; danger remains reserved for an operation that ran and failed. This mirrors docs/DESIGN.md §5.5: a steady-state configuration is not an error.

#### Scenario: Optional service absent stays calm
- **WHEN** the `dkms` provider reports absent because DKMS is not installed
- **THEN** the note renders in the information role on both surfaces, and never in warning or danger

#### Scenario: Present but unreachable warns
- **WHEN** the `devmgrd` helper or the `fwupd` service is unreachable
- **THEN** the note renders in the warning role on both surfaces, and never in danger

#### Scenario: Blocked verb escalates an otherwise calm note
- **WHEN** a user attempts a mutation that an absent backend blocks
- **THEN** the refusal is explained with the same shared sentence, raised to the warning role

#### Scenario: Danger is not reachable for unavailability
- **WHEN** any backend, any kind, and any context are mapped to a role
- **THEN** the resulting role is information or warning, never danger

### Requirement: Raw diagnostic is preserved but never primary
The raw backend error text SHALL be retained and reachable, and SHALL NOT be rendered on the primary surface by default. It SHALL be written to the log once per availability state transition — not per poll or per frame — and SHALL be exposed through an expandable diagnostic affordance on each surface. Presentation text and diagnostic text SHALL be separate fields on the shared accessor, so neither surface can concatenate them.

#### Scenario: Raw string is absent from the default render
- **WHEN** the Updates view renders with `fwupd` unreachable and no diagnostic affordance has been opened
- **THEN** no primary widget or pane text matches `org\.freedesktop`, `DBus\.Error`, `ServiceUnknown`, or `errno`

#### Scenario: Raw string is reachable on request
- **WHEN** the user opens the diagnostic affordance for that backend
- **THEN** the raw error text is displayed there, and the same text is present in the log

#### Scenario: Log is not spammed by polling
- **WHEN** an unavailable backend is polled repeatedly without its availability changing
- **THEN** the raw diagnostic is logged once for that transition, not once per poll

### Requirement: Empty result and unreachable source are distinct states
A view whose empty-state string asserts a completed query SHALL NOT render that string while any source feeding it is unreachable. The Updates view SHALL render `(no updates available)` only when every provider reported availability and returned zero candidates. Where some providers are reachable and others are not, the view SHALL render the reachable providers' rows together with the degraded note, and SHALL omit the empty-result string.

#### Scenario: Unreachable source suppresses the empty-result string
- **WHEN** the Updates view renders while `fwupd` is unreachable
- **THEN** `(no updates available)` does not appear on either surface, and the degraded note is shown instead

#### Scenario: Genuinely empty result still says so
- **WHEN** every update provider is available and each returns zero candidates
- **THEN** `(no updates available)` renders and no degraded note is shown

#### Scenario: Mixed availability shows both truths
- **WHEN** one update provider is available with candidates and another is unreachable
- **THEN** the available provider's rows render, the unreachable provider's degraded note renders, and no empty-result string appears

### Requirement: Reads remain usable while a backend is degraded
A degraded backend SHALL NOT blank, replace, or block the regions of a view that other sources can still populate. Mutation controls that the unavailability blocks SHALL remain visible and SHALL explain their unavailability using the same shared sentence rather than a separately authored string.

#### Scenario: Degraded daemon leaves reads intact
- **WHEN** `devmgrd` is unreachable and a view that reads system state renders
- **THEN** the readable content renders normally, the degraded note is one bounded region, and no region is replaced by an error screen

#### Scenario: Blocked verb reuses the shared sentence
- **WHEN** a verb is disabled because its backend is unavailable
- **THEN** the reason presented for the disabled verb is the shared sentence for that backend, not a separate wording
