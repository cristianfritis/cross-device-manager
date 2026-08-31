# Task 1 spike — accessibility bridge gate

**Verdict: GATE PASSED (task 1.4).** D1 holds. The container exposes a full,
named, addressable AT-SPI tree for `devmgr-gui`, and the tree can be driven by
element with no pointer, no `/dev/uinput`, and no foreground activation.

Run on 2026-08-30, image `design-spike:latest` layered on `test-unit:latest`
(Ubuntu 24.04, glibc 2.39, Qt 6.4.2, at-spi2-core 2.52.0).

## Result per task

| Task | Result |
| --- | --- |
| 1.1 image | PASS — `Dockerfile.spike` |
| 1.2 bring-up | PASS — Xvfb `1024x640` + Openbox + session bus + a11y bus + registryd; `devmgr-gui` stays up |
| 1.3 driver | **Not needed** — see *Finding 3* |
| 1.4 **GATE** | **PASS** — 305 nodes, 255 named (`tree.txt`) |
| 1.5 stop-on-fail | Not triggered |
| 1.6 element action | PASS — no escalation, no uinput (`click.txt`) |

## Findings

### 1. The Qt AT-SPI bridge is compiled into QtGui, not shipped as a plugin

`/usr/lib/x86_64-linux-gnu/qt6/plugins/` has **no** `accessiblebridge`
directory, and searching the archive for a bridge plugin finds nothing. The
bridge is inside `libQt6Gui.so.6.4.2` — it carries the `org.a11y.atspi.*`
interface strings and the `qt.accessibility.atspi` logging category, and links
`libQt6DBus` + `libdbus-1`.

An absent plugin directory is therefore **not** evidence the bridge is missing.
The image needs `at-spi2-core` and `dbus-x11`; it does not need a Qt a11y
package, because none exists on this distro.

### 2. `QT_ACCESSIBILITY=1` is the wrong knob for Qt 6

With `QT_ACCESSIBILITY=1` alone the app runs but never registers:
`desktop.childCount == 0`. Two knobs work, tested independently, either alone
sufficient:

- `QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` — **use this.** Deterministic, no dconf.
- Setting `org.a11y.Status` `IsEnabled` / `ScreenReaderEnabled` true on the
  session bus — works, but depends on a live dconf service.

### 3. AT-SPI alone covers perception *and* input — `cua-driver` was not required

`python3-pyatspi`, a distro package, reads the tree with states and drives the
app through the `Action` interface. Switching Devices → Modules via
`queryAction().doAction(0)` recomposed the toolbar (`click.txt`):

```
BEFORE (Devices): ['Refresh', 'Disable', 'Bind driver…', 'Unbind driver (advanced)']
AFTER  (Modules): ['Load Module…', 'Unload']
```

This is the focus-free `x11_atspi` path the driver's Linux guide documents as
driver-verifiable. The `/dev/uinput` limitation recorded in the design's risks
applies only to the MPX **pixel** path, which D1 already rejected — so that risk
does not bind this harness.

Screenshots come from ImageMagick `import` against the Xvfb display
(`devices.png`, `modules.png`, 1024x640 PNG), so evidence capture needs no
driver either.

**This is a decision for the user, not a settled outcome** — see *Open decision*.

### 4. Hidden actions stay in the tree — assert on `STATE_SHOWING`

The toolbar exposes all 24 children on every tab. On Devices (`states.txt`):

```
Refresh                   push button  SHOWING=True   ENABLED=True
Disable                   push button  SHOWING=True   ENABLED=False
Bind driver…              push button  SHOWING=True   ENABLED=False
Unbind driver (advanced)  push button  SHOWING=True   ENABLED=False
Load Module…              push button  SHOWING=False  ENABLED=False
Create Snapshot…          push button  SHOWING=False  ENABLED=False
…
```

A harness asserting on **tree presence** would pass `tab-contextual-toolbar`'s
central requirement — a verb from another tab is *hidden*, not merely disabled —
no matter what the code did. That is an assertion that cannot fail: the same
defect class as F5. The harness MUST filter on `STATE_SHOWING`.

This belongs in the spec and is not yet stated there.

### 5. The container default posture is already degraded, and it is legible

`gui.log` shows `devmgrd unavailable (unreachable): Failed to open bus`, and the
screenshots show the banner rendered with its `?` glyph and `Details ▾`
disclosure. D4's claim that the unit container's "no devmgrd" default *is* the
`devmgrd`-down posture holds, with no product seam.

### 6. Two archived requirements re-confirmed live, for free

Incidental, but worth recording — the spike reproduced them against the running
app rather than a ViewModel:

- `tab-contextual-toolbar` "Standing on Devices": exactly `Refresh`, the
  enable/disable verb, `Bind driver…`, `Unbind driver (advanced)` are showing.
- `calm-backend-unavailability` "Daemon unreachable on the active tab":
  `Refresh` stays visible **and enabled** while the other three are visible and
  disabled.

## Open decision — `cua-driver` or `pyatspi`?

Finding 3 means the harness can be built two ways. D1's *decision* (a11y tree
over pixels) is proven either way; only the implementation is in question.

- **`pyatspi` + ImageMagick** — everything from `apt`, no network fetch at image
  build, nothing to version-pin, no third-party binary in a build image. Costs
  the driver's tested input ladder and its screenshot/recording plumbing.
- **`cua-driver`** — matches the proposal's name and the 2026-07-27 method, and
  brings the escalation ladder. Its installer fetches and execs a second remote
  script; pinning is via `CUA_DRIVER_RS_VERSION`. Adds a supply-chain surface to
  a build image, which `release-supply-chain` and `docs/REPRODUCIBILITY.md`
  would both have to account for.

Not decided here. Nothing was installed into any image.

## Reproducing

```bash
docker build -t design-spike:latest -f Dockerfile.spike .
docker run --rm -v "$PWD":/spike design-spike:latest \
  dbus-run-session -- bash /spike/capture.sh
```
