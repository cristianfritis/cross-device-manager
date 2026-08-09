# Manual verification matrix — calm-backend-unavailability §11

Run 2026-07-27 on the owner's machine (X11/XFCE, cachyos 7.1.4), driven through
`cua-driver` 0.12.6 against the real host build — not a fixture, not a mock.

**The host was already in the degraded posture the matrix asks for:** `devmgrd`
was not running, and `/var/lib/dkms` does not exist. `fwupd` 2.0.20 answered
(D-Bus activated), so the matrix exercises *mixed* availability — the hardest
case and the one §3.4 was written for. No service had to be stopped to produce
it.

Binaries: `build/gui/devmgr-gui`, `build/tui/devmgr-tui`. TUI captured in
fixed-size `tmux` panes at 120x32 and 80x24 so the sizes match the render tests.

**The five defects this matrix found were fixed as §14 and RE-VERIFIED LIVE on
2026-07-28**, on the same machine, with the daemon down again. Rows below carry
the original finding; the fixed rows say so.

## Verdict

**11.1, 11.2, 11.3 and 11.4 all pass.** The D-Bus name is gone, the sentence is
shared and byte-exact across surfaces, the raw detail is demoted behind a
disclosure rather than deleted, nothing is painted danger, and no empty-state
string lies.

Five defects surfaced — none a regression of the two reported bugs. They were
gaps between what §13 claims and what the GUI actually renders, plus two
assertions that could not fail. All five are fixed (§14) and the three that are
user-visible were re-checked on the running app:

- **F1 fixed, verified live:** GUI Devices now renders `? Device service
  unavailable — showing read-only system state.` in bold with `Details ▾`, and
  the disclosure reveals `Device service: helper devmgrd is not available`.
- **F2 fixed, verified live:** GUI Modules now renders the same sentence with
  the `?` glyph, warning weight and a `Details ▾` disclosure.
- **F3 fixed, verified live:** the degraded Snapshots legend at 80x24 is now 77
  columns — ` Snapshots (/=filter s=create r=restore d=diff h=hist x=delete
  i=diag q=quit)` — all eight shortcuts present, `q=quit` included.

## 11.1 — `devmgrd` unreachable

| # | Check | Result |
| --- | --- | --- |
| 1 | Reads still work — device/module lists render from sysfs/udev, no crash | **PASS** both surfaces |
| 2 | Note is calm and bounded: `?` glyph + one sentence, not a red bar | **PASS** — no danger paint on either surface |
| 3 | Sentence byte-identical across surfaces | **PASS** — `? Device service unavailable — showing read-only system state.` on GUI Snapshots and on TUI Devices/Modules/Snapshots; matches `core::unavailabilityText(Devmgrd, Unreachable)` exactly |
| 4 | Devices, Modules and Snapshots each carry the note | **TUI PASS / GUI FAIL as found** — F1, F2. **Fixed and re-verified live 2026-07-28: all four GUI pages carry it.** |
| 5 | `(no snapshots)` withheld while degraded | **PASS** both surfaces — list renders blank, banner carries the reason |
| 6 | Daemon-backed verbs disabled, History still enabled | **PASS** — GUI toolbar: Disable/Unbind/Bind/Load/Unload/Create-Restore-Diff-Delete Snapshot all dim; **History bright**, Refresh bright |
| 7 | Raw detail reachable — GUI `Details ▾`, TUI `i` | **PASS on Updates+Snapshots / FAIL on GUI Modules as found** — F2. **Fixed and re-verified live: Devices and Modules both disclose.** |
| 8 | Closed disclosure leaks nothing | **PASS** — scanned the whole 120x32 TUI screen for `org.freedesktop`, `DBus.Error`, `ServiceUnknown`, `errno`, `/var/lib`, `/sys/`, `helper devmgrd`: zero hits |

GUI disclosure, opened: `Details ▾` → `Details ▴` revealing
`Device service: helper devmgrd is not available`. TUI `i` opens

    ────────────────────────────────
     -- Diagnostics --
     Device service: helper devmgrd is not available
     DKMS status: DKMS root not found: /var/lib/dkms

— a muted header plus a rule, **not a box** (§4.3 border discipline, task 6.1).
`Escape` closes it and the header leaves the screen.

## 11.2 — `fwupd` present, DKMS absent (mixed availability)

| # | Check | Result |
| --- | --- | --- |
| 9 | Translated sentence appears | **PASS** — `DKMS status unavailable — DKMS is not installed on this system.`, byte-identical to the table |
| 10 | `(no updates available)` absent | **PASS** — 12 real fwupd rows render; the placeholder is correctly suppressed |
| 11 | No D-Bus name or path until the diagnostic is opened | **PASS** — `/var/lib/dkms` appears **only** inside the opened region, as `DKMS status: DKMS root not found: /var/lib/dkms`. Task 4.1 holds. |
| 12 | Healthy provider's rows render beside the degraded note | **PASS** — this is the §3.4 mixed case, live |

GUI Updates banner as rendered:

    ? fwupd 2.0.20 · lvfs metadata 63 days old — run fwupdmgr refresh | DKMS status unavailable — DKMS is not installed on this system. | Secure Boot: off · Lockdown: none    [Details ▾]

## 11.3 — No colour / narrow

`NO_COLOR=1` at 80x24.

| # | Check | Result |
| --- | --- | --- |
| 13 | State identifiable from glyph and words alone | **PASS** |
| 14 | Sentence byte-identical to the coloured run | **PASS** — verified by `od -c`; the em-dash is U+2014 (`342 200 224`) in both |
| 15 | Nothing painted danger | **PASS** |
| — | *Regression found at this size* | **FAIL as found** — F3. **Fixed and re-verified live: legend is 77 columns, nothing cut.** |

## 11.4 — Log volume

Run 2026-07-28 with `devmgrd` down, against `build/gui/devmgr-gui` with stdout
captured. (The TUI is the wrong vehicle: spdlog's default logger writes to
STDOUT, which is the canvas FTXUI is drawing on, so the lines are swallowed.)

| # | Check | Result |
| --- | --- | --- |
| 16 | The raw diagnostic appears **once per transition**, not once per poll | **PASS** — ten Snapshots-tab entries, each a real `snapshotList()` call against an unreachable daemon, produced exactly ONE line |
| 17 | down → up → down logs the line twice | **PASS** — owner ran it live 2026-07-28 12:08–12:09 with `rc-service devmgrd start/stop` against one GUI instance; two `devmgrd` warn lines, 72s apart |

The whole captured log after ~40 tab switches:

    [2026-07-28 00:07:52.320] [warning] devmgrd unavailable (unreachable): helper devmgrd is not available
    [2026-07-28 00:08:09.295] [warning] dkms unavailable (absent): DKMS root not found: /var/lib/dkms

Two backends, one line each — that is row 16.

Row 17 was run separately, on a host without systemd: `devmgrd` was installed as
an OpenRC service (binary to `/usr/local/bin`, `packaging/devmgrd.initd` to
`/etc/init.d/devmgrd`) so its lifetime could be driven by hand. The D-Bus
activation file was deliberately NOT installed — with it in place the bus
re-launches the daemon on the first client call and the down state cannot be
held. The captured log from the single GUI instance:

    [2026-07-28 12:08:29.968] [warning] devmgrd unavailable (unreachable): helper devmgrd is not available
    [2026-07-28 12:08:43.682] [warning] dkms unavailable (absent): DKMS root not found: /var/lib/dkms
    [2026-07-28 12:09:41.963] [warning] devmgrd unavailable (unreachable): helper devmgrd is not available

The two `devmgrd` lines prove the transition themselves, independently of the
click sequence. `BackendStatusVM::observe` clears `logged_` only on an
error-free observe, and re-logs otherwise only when the *kind* changes; both
lines are `(unreachable)`, so the second cannot be a kind change. It can exist
only because a successful `devmgrd` call landed between them — that is the
recovery. Down → up → down in one process, logged twice.

## Defects as originally found (all five now fixed)

Fix detail is in §14 of `tasks.md`. Two things the matrix taught that the
defects themselves do not say:

- **F3 was under-reported when first written.** Devices overflows 80 columns
  too (89 while degraded, 74 healthy). It went unnoticed because the live check
  happened to be standing on the Snapshots tab. A single-tab spot check is not
  a size check.
- **The first attempt at fixing F5 was wrong.** Replacing the tautology with a
  clipping *detector* — flag a row whose last column holds ordinary text —
  immediately failed the healthy 80-column Snapshots legend, which fits exactly.
  A clipped row and an exactly-fitting one look identical from the screen alone.
  The property that can actually be checked is completeness: a string the view
  means to show must be present in full.


- **F1 — the GUI Devices tab carries no availability note at all.** The TUI
  Devices tab does (`? Device service unavailable — …`, with `i=diagnostics` in
  its legend). `gui/src/main_window.hpp` has `bannerLabel_` (Modules),
  `updatesBannerLabel_`, `snapshotsBannerLabel_` — and no devices banner. Its
  verbs are correctly disabled, so the user sees dimmed controls with no visible
  reason on that tab. §13.8's "Devices/Modules carry the note" is true of the
  TUI only.
- **F2 — the GUI Modules banner ignores the role seam task 7.1 built.**
  `main_window.cpp:593` sets it with `modulesVm_.banner()` (the plain string)
  rather than `bannerLine()` (text + severity). Consequence: no `?` glyph, no
  warning weight, and no `Details` disclosure — the one place a degraded
  sentence appears with no route to its diagnostic. TUI Modules has all three.
- **F3 — the degraded Snapshots legend overflows 80 columns.** Healthy it is 81
  columns (one over; only a trailing space is lost). Adding `i=diagnostics`
  takes it to 96. At 80x24 it renders `… x=delete  i=diagn` — the advertised key
  is cut mid-word and **`q=quit` is entirely off-screen**. Task 6.3 caused this.
- **F4 — the test that would have caught F3 only runs at 120x32.**
  `DiagnosticsKeyIsListedOnlyWhileDegraded`
  (`tui/tests/test_backend_availability_render.cpp:205`) pins `const Size
  s{120, 32}` for the degraded case, and asserts `q=quit)` survives **only on
  the healthy renders**. The other tests in the file loop over all three sizes.
- **F5 — `expectNoOverflow` cannot fail.** It reads each row out of a
  fixed-width `ftxui::Screen` (`rowText` iterates `x < screen.dimx()`), then
  asserts that row's width is `<= s.w`. That holds by construction. Every
  "no row overflows" guarantee in tasks 6.4/6.6/6.7 rests on a tautology —
  truncation is exactly what it cannot see.

## Observed, out of scope

- **The toolbar shows every tab's verbs at once** — on Devices it offers Create
  Snapshot, Install Update, Diff Snapshot. Live confirmation of the parked
  `tab-contextual-toolbar` change; not touched here.
- **The TUI's selected row is left-clipped** — `> wupd` for `fwupd`,
  `> + controller` for `AMD USB controller`. FTXUI scrolls the focused row
  horizontally to reveal its tail. Pre-existing and unrelated to availability.
