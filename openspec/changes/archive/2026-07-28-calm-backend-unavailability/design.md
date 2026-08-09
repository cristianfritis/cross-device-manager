## Context

Two shipped defects share one root cause: nothing owns the answer to "what do we say, how loud, and where does the raw detail go, when a backend is not there."

- `app/src/updates_vm.cpp:71-76` — `availabilityCell()` builds `"unavailable: " + s.availability.error->message`. For fwupd, `fwupd::mapError` (`platform/linux/src/fwupd_contract.cpp:64`) composes that message as `"<dbus-name>: <dbus-message>"`, so `org.freedesktop.DBus.Error.ServiceUnknown` lands in `banner()` (`updates_vm.cpp:279`) and from there in `updatesBannerLabel_` (GUI) and the TUI banner. This is the string the beta user reported.
- `app/src/updates_vm.cpp:12` — `kPlaceholderRow = "(no updates available)"` is emitted with no availability gate, so the empty-result string renders during an unreachable source.
- `platform/linux/src/dkms_status_provider.cpp:163` — `"no /var/lib/dkms — DKMS not present"` puts a filesystem path in user-visible text. Same §6 clause, same fix.
- `tui/src/state_roles.hpp:204` — `modulesBannerRole()` derives a banner's color by searching its rendered text for `"will be rejected"`. Role is being reverse-engineered from presentation, which docs/DESIGN.md §11 lists as an anti-pattern and which the "VM-owned per-row state seam" requirement already outlawed for rows.

Constraints that shaped the design:

- `"helper devmgrd is not available"` (`platform/linux/include/devmgr/platform/linux/dbus_contract.hpp:65`) is asserted verbatim by `tests/unit/test_dbus_contract.cpp:28`, `tests/ipc/test_ipc_round_trip.cpp:194`, `cli/tests/test_cli.cpp:297,332`, and is substring-matched by the hint list at `cli/src/cli.cpp:37`. It is load-bearing as a *diagnostic*, so it must not be repurposed as the presentation string.
- `cli/` does not link `app/`, so any wording home inside `app/` leaves the CLI with a permanently divergent copy.
- `core/src/snapshot_presentation.cpp:55` already emits user-facing sentences from `core`, so this is an established layer for shared wording, not a new precedent.
- The GUI expresses role through weight and iconography rather than color (`gui/src/main_window.cpp:38` uses bold for the Warning role); the `tui-presentation` "GUI color parity — temporary DESIGN §9 exception" requirement keeps that split legitimate. Wording parity is not covered by that exception.
- TUI `d` is bound twice already (`tui/src/tui_app.cpp:798`, `:837`), so it cannot carry diagnostics.

## Goals / Non-Goals

**Goals:**

- One shared, translated sentence per backend and unavailability kind, reachable by both surfaces through one accessor, with parity enforced by a test rather than by convention.
- Make it structurally impossible for a raw D-Bus name, errno, exception name, or path to reach a user-visible sentence — not merely discouraged by review.
- Retire `danger` as a reachable role for backend unavailability, and make availability notes bounded regions rather than full-bleed bars.
- Keep the raw diagnostic fully available: in the log, and behind a keyboard-reachable disclosure on both surfaces.
- Separate "checked, nothing to report" from "could not check" wherever an empty-state string implies a completed query.

**Non-Goals:**

- Redesigning the Updates view, its layout, or its verbs.
- Lifting the §9 GUI color exception. The GUI continues to express roles through weight and iconography.
- A general i18n/translation framework. "Translated" here means "written in the user's language rather than the machine's", which is what §6 requires; the strings remain English literals in one table.
- Changing `core::Error` codes, the D-Bus contract, or any daemon-side wording.
- The Cua Docker sandbox, the Devices "wall of green" color direction, and the tab-unaware toolbar. Each is parked as its own change (`cua-design-sandbox`, `calm-normal-state-color`, `tab-contextual-toolbar`).

## Decisions

### D1: Wording table lives in `core`, surfaces read it through a ViewModel accessor

`core/include/devmgr/core/backend_wording.hpp` exposes:

```
enum class BackendId { Devmgrd, Fwupd, Dkms };
enum class UnavailabilityKind { Absent, Unreachable, NotPermitted, Unsupported };

UnavailabilityKind kindFor(Error::Code);
std::string unavailabilityText(BackendId, UnavailabilityKind);
```

`unavailabilityText` takes no error object and no string. It cannot leak a diagnostic because it never receives one — the ban is enforced by the signature, not by a code-review rule. `kindFor` is the only place `Error::Code` is interpreted for presentation.

*Alternative considered:* the table in `app/`, which reads more literally as "wording originates in the ViewModels" (docs/DESIGN.md §6.1). Rejected because `cli/` cannot link `app/`, so the CLI's hint strings at `cli/src/cli.cpp:37` would remain a third, untested wording source. §6.1's intent — one place to change a string, no frontend inventing its own — is satisfied by `core` owning the literals and the VMs owning the accessor the frontends actually call.

*Alternative considered:* passing the `Error` in and sanitizing it. Rejected: sanitizing is a filter that can be bypassed or fall out of date, and every future backend would have to remember to apply it.

### D2: `app::BackendStatusVM` is the single accessor, and the parity test's target

```
struct BackendNote {
    core::BackendId backend;
    core::UnavailabilityKind kind;
    std::string text;        // core table — the only thing a surface renders by default
    std::string diagnostic;  // raw — disclosure and log only
    StatusSeverity role;
};
std::vector<BackendNote> notes() const;   // degraded backends only; empty when all healthy
```

Two separate fields rather than one formatted string is the point: with no combined field to reach for, a surface cannot concatenate them by accident. `UpdatesVM` delegates its per-provider availability presentation to this type rather than formatting its own cell.

The cross-surface parity test asserts that what the GUI puts in its banner and what the TUI renders both equal `notes()[i].text` — one accessor, so "the two surfaces agree" is checkable rather than aspirational.

### D3: Role mapping is total, and `Danger` is not in its range

```
role(kind, blockedVerbContext) =
    Warning  if kind ∈ {Unreachable, NotPermitted}
    Warning  if blockedVerbContext
    Information otherwise            // Absent, Unsupported
```

`Danger` is unreachable because it is not a branch, not because a reviewer will notice it. `Absent` never escalates on its own — a machine that never had DKMS installed should not carry a standing warning, which is exactly the §5.5 "steady-state configuration is not an error" rule applied to a second domain.

*Alternative considered:* information for everything, warning only on an attempted verb. Rejected: a stopped `devmgrd` on a machine configured to run it would read as no more notable than an optional package that was never installed, and the user's own report was that the fact must stay visible.

*Alternative considered:* warning by default. Rejected: it makes the Updates tab permanently warning-colored on any machine without fwupd, which is the loudness this change exists to remove.

### D4: Diagnostics — GUI disclosure, TUI `i`, log on transition

- **GUI:** a `Details ▾` `QToolButton` on the banner row toggles an inline read-only region containing the raw text. It is in the tab order and carries an accessible name. A bare tooltip was rejected: it is pointer-only, so it would leave the diagnostic unreachable for keyboard and assistive-technology users, which `ui-accessibility` forbids.
- **TUI:** `i` toggles a bordered Diagnostics region and appears in the shortcut legend while any backend is degraded. `d` was the natural mnemonic but is taken twice (`tui_app.cpp:798`, `:837`); `i` also matches the GUI's information metaphor.
- **Log:** the raw string is written at warn level on each availability *state transition*, keyed by `(backend, kind)`. Availability is polled, so logging per observation would produce one line per poll forever on a machine that simply lacks fwupd.

*Alternative considered:* an always-visible dim second line in the TUI. Rejected — it puts the raw D-Bus name back on the default-rendered primary surface, which is the defect.

### D5: The empty-result gate is a whole-view predicate, not a per-provider one

`(no updates available)` renders only when **every** provider reported available and **all** returned zero candidates. Any unreachable provider suppresses it. A mixed state renders the reachable providers' rows plus the unreachable provider's note.

*Alternative considered:* per-provider placeholder rows, so an unreachable provider shows its note and an available-but-empty provider shows the placeholder. Rejected for this change: it multiplies rows in the region the user reported as already confusing, and the empty-result string is a whole-view claim ("nothing to update"), not a per-provider one.

### D6: Banners carry their role instead of having one parsed out of them

Banner accessors this change touches return text and role together; `modulesBannerRole()`'s substring match is removed for them. This mirrors the "VM-owned per-row state seam" requirement already in `tui-presentation`, extended from rows to banners. It also removes the failure mode where rewording a banner silently recolors it.

### D7: `?` is the glyph, and no new glyph is introduced

The existing ASCII set already defines `?` as "unavailable" (`tui-presentation`, "Status glyph policy"). A degraded backend is exactly that, so the note reuses `?` and the "Status glyph policy" requirement needs no amendment. `~` was considered and rejected — its documentation is parked in a separate change, and introducing an undocumented glyph here would create the very gap that change exists to close.

## Risks / Trade-offs

- **Losing a detail the user actually needed** ("fwupd is masked" vs "fwupd is not installed" both collapse to "not responding") → the kind mapping keeps the categories that change what the user should *do*, and the exact cause stays one keystroke or one click away in the diagnostic. If a specific cause proves worth its own sentence, the table is the one place to add it.
- **`unavailabilityText` becomes a dumping ground** as backends grow → it is keyed by `(backend, kind)` with a total mapping and a generic fallback, so a new backend adds rows, never branches in the frontends.
- **The GUI disclosure adds a control to a crowded toolbar area** → it renders only while a backend is degraded, and collapses to nothing otherwise.
- **TUI `i` could collide with a future per-view binding** → it is claimed globally and documented in the legend, matching how `m` and the digit keys are already handled.
- **The empty-result gate could hide a genuinely empty view** if a provider reports unavailable spuriously → the degraded note is shown in that case, so the user sees *more* explanation, not less; the failure mode is verbose, not silent.
- **Test coupling to exact sentences** → intended. The sentences are the contract; a change to one should fail a test and be made deliberately in the table.
- **`modulesBannerRole()` removal touches a path the Modules view depends on** → scope is limited to banners this change touches, and the existing "Steady-state security is calm" scenario already pins the Modules banner's behavior.

## Migration Plan

No data, schema, or IPC migration. Rollout is a normal build:

1. Land `core` wording table plus its unit tests — no behavior change until consumed.
2. Land `BackendStatusVM` plus the parity test — still unconsumed.
3. Switch `UpdatesVM` to the accessor, gate the placeholder, remove the raw concatenation. The GUI banner stops leaking the D-Bus name at this point even before surface work lands.
4. Demote the DKMS availability text to a diagnostic.
5. Land the GUI disclosure, then the TUI `i` region and legend entry.
6. Land banner role accessors and remove `modulesBannerRole()`'s substring match for touched banners.

Rollback at any step is a revert; nothing persists state. Steps 1–2 are inert on their own, so a partial landing cannot leave the build in a worse state than today.

## Open Questions

None blocking. Two deliberately deferred:

- Whether `fwupd` masked-versus-absent deserves distinct sentences — deferred until a user reports the collapsed wording as misleading; the table is the single place to split it.
- Whether the `snapshot-ui` and Devices views should adopt `BackendStatusVM` notes in place of their current daemon-down handling — this change makes them able to, and does not require them to.
