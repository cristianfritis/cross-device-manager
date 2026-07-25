## Why

A user on the released `v0.6.0-beta.1` GUI opened the Updates tab on a machine without `fwupd` and saw a primary-surface banner reading `fwupd unavailable: org.freedesktop.DBus.Error.ServiceUnknown: The …` — a raw D-Bus exception name — while the list region simultaneously showed `(no updates available)`. That is two `docs/DESIGN.md` violations at once: §6 forbids reporting raw exceptions, errno values, D-Bus names, or filesystem paths as the only explanation, and the empty-result string asserts a successful query that never happened (§2.1 truth, §6 "Partial failure"). The same defect class exists on the other daemon and the other surface: the TUI presents `helper devmgrd is not available` at danger loudness over a screen that is still fully readable, against §1 (calm) and §5.5 (no loud alert for a steady state).

Now that the GUI ships, §9's wording-parity invariant is fully live — the "GUI does not color yet" exception covers color, not wording. Today GUI and TUI disagree on the daemon-unavailable string because no shared wording exists for it. There is no single place a backend's unavailability sentence, its severity, and its raw diagnostic are decided, so every surface improvises.

## What Changes

- **New shared wording source in `core`.** `unavailabilityText(BackendId, UnavailabilityKind)` returns a calm, translated sentence per backend. It is a pure function of `(backend, kind)`; the raw `core::Error::message` is not an input, so a D-Bus name cannot structurally reach a user-visible sentence.
- **New `app::BackendStatusVM`** — the one accessor both frontends read for degraded backends, carrying `{backend, kind, text, diagnostic, role}`. Cross-surface wording parity becomes a string-equality test against a single accessor rather than a convention.
- **Severity is decided once, and `Danger` is not reachable for unavailability.** `Absent → Information`; `Unreachable`/`NotPermitted` → `Warning`; any kind escalates to `Warning` in the context of a verb it blocks. This corrects the TUI's danger-loud `devmgrd` presentation.
- **Raw diagnostics are demoted, not hidden.** The raw string goes to the log (once per availability state transition, not per poll) and to an expandable affordance — GUI `Details ▾` disclosure on the banner row, TUI `i` key opening a bordered Diagnostics region, added to the footer legend. `d` is unavailable as a binding (already bound at `tui/src/tui_app.cpp:798` and `:837`).
- **`UpdatesVM` distinguishes "checked, zero updates" from "source unreachable".** `(no updates available)` renders only when every provider is available and reported zero candidates. A mixed state shows reachable providers' rows plus the degraded note, never the empty-result string.
- **`UpdatesVM::availabilityCell()` stops concatenating `error->message`** (`app/src/updates_vm.cpp:71-76`) — the origin of the reported banner.
- **`DkmsStatusProvider` availability text stops naming a filesystem path** (`platform/linux/src/dkms_status_provider.cpp:163`, `"no /var/lib/dkms — DKMS not present"`) — same §6 violation class, fixed in the same pass.
- **Banners carry a role instead of having one parsed out of them.** The TUI currently derives banner valence by substring-matching rendered text (`tui/src/state_roles.hpp:204`), which is the §11 anti-pattern; banners this change touches expose their role from the VM.
- **Reads stay usable while a backend is down**, and mutation controls explain unavailability through the same shared sentence rather than a separate string.
- Not breaking: `"helper devmgrd is not available"` (`platform/linux/include/devmgr/platform/linux/dbus_contract.hpp:65`) is retained verbatim as the *diagnostic* string. Four test sites and the `cli/src/cli.cpp:37` hint matcher continue to pass unchanged.

## Capabilities

### New Capabilities
- `backend-availability`: Surface-agnostic contract for a degraded backend — the per-backend translated sentence and where it lives, kind derivation from `core::Error::Code`, the severity mapping and its escalation rule, separation of presentation text from raw diagnostic, and the rule that an empty-result string may not render while a source is unreachable.

### Modified Capabilities
- `ui-accessibility`: The "Defined loading, empty, and error states" requirement gains a prohibition on raw backend strings (exception names, errno values, D-Bus names, filesystem paths) on the primary surface, and a clause requiring the empty-versus-unreachable distinction. Its daemon-down scenario generalizes from Snapshots+devmgrd to any view whose backend is unreachable, with the fwupd/Updates case named explicitly. The expandable diagnostic must be keyboard-reachable, so it falls under this capability rather than being a GUI cosmetic.
- `tui-presentation`: Under per-view semantic coloring, a degraded-but-readable backend renders as information or warning and never danger — extending the existing "Steady-state security is calm" scenario to daemon and provider availability. Adds the documented `i` diagnostics key and the requirement that the degraded state is identifiable in MONO/PLAIN by glyph plus the translated sentence, with no color dependence.

## Impact

- **Code — new:** `core/include/devmgr/core/backend_wording.hpp`, `core/src/backend_wording.cpp`, `app/include/devmgr/app/backend_status_vm.hpp`, `app/src/backend_status_vm.cpp`.
- **Code — modified:** `app/src/updates_vm.cpp` (availability cell, placeholder gate, banner role), `app/include/devmgr/app/updates_vm.hpp`, `platform/linux/src/dkms_status_provider.cpp` (presentation text demoted to diagnostic), `gui/src/main_window.cpp` (banner role expression + `Details ▾` disclosure), `tui/src/tui_app.cpp` (`i` key, Diagnostics region, legend), `tui/src/state_roles.hpp` (retire banner-text parsing for touched banners).
- **Unchanged by design:** `platform/linux/include/devmgr/platform/linux/dbus_contract.hpp`, `cli/src/cli.cpp` — no test churn.
- **Tests:** GUI offscreen assertions that no primary widget text matches `org\.freedesktop|DBus\.Error|ServiceUnknown|errno` and that `(no updates available)` is absent while a source is unreachable; TUI fixed-screen renders at 120x32 / 100x28 / 80x24 in FULL and MONO; a cross-surface string-equality test on the shared accessor; core unit tests for the wording table and kind/role mapping.
- **Docs:** `docs/DESIGN.md` §6.1 gains a shared-wording row for the source-unreachable state; §6's existing prose is cited as the source of truth, not rewritten.
- **Dependencies:** none added.
- **Parked as separate changes, not in scope:** `cua-design-sandbox` (Cua Docker sandbox for automated design review — reusable verification infrastructure this change's own gates do not depend on), `calm-normal-state-color` (the Devices "wall of green" — a color-direction decision needing its own `enabled→success` amendment), `tab-contextual-toolbar` (Updates tab showing device/module verbs, §5.3/§10.1 tab gating).
