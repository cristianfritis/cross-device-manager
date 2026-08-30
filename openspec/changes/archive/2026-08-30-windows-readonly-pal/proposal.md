## Why

The project has always described itself as Linux-first with a portable core, and
`core/include/devmgr/pal/interfaces.hpp` was written to make a second platform a
matter of supplying implementations. That claim has never been tested. Nine
phases of Linux work have quietly leaned on the assumption that there is exactly
one backend set, and the tree now encodes it structurally:

- `CMakeLists.txt:35` wraps `platform/linux`, `daemon`, `cli`, `tui`, `gui`, and
  `packaging` in a single `if(UNIX AND NOT APPLE)`. On any other host the build
  produces `core`, `app`, and unit tests — no user-facing program at all.
- Every composition root names Linux types directly: `gui/src/gui_app.cpp:24-32`,
  `tui/src/tui_app.cpp:34-42`, and `cli/src/main.cpp:10` include
  `devmgr/platform/linux/...` and stack-construct `UdevDeviceEnumerator`,
  `KmodDriverManager`, `DbusPrivilegedChannel`, and the rest. There is no seam to
  substitute at; the frontends *are* the wiring.
- `core` names Linux concepts in its data model — `Device::sysfsPath`,
  `Device::modalias` — and those names have since travelled outward into the
  D-Bus wire contract, the persisted snapshot store, and 19 main specs.

A second platform written against this tree would not exercise the PAL; it would
fork the frontends. The abstraction has to be made real before it can be used,
and the only honest way to prove it is real is to land one platform through it.

Windows read-only is the right first proof. It needs no privilege model, no
daemon, and no installer, so it isolates the question that actually matters —
does the PAL generalise — from the much larger question of how privileged device
mutation should work on Windows. It also arrives at a favourable moment: the
`calm-backend-unavailability` work already built the machinery for presenting a
backend that cannot serve a view, and the `Unsupported` error code is already
mapped to an `unsupported` unavailability kind that no backend currently emits.
A platform where `devmgrd`, `fwupd`, and `dkms` do not exist is precisely the
case that mapping was reserved for.

## What Changes

**The portability seam (does the larger share of the work).**

- Introduce a backend-selection seam so that no frontend names a platform. The
  frontends ask for a backend set and receive one; which implementations back it
  is decided in one place, per build.
- Restructure the build so target inclusion is gated on *capability*
  (a privileged channel is available, Qt is present, a platform backend exists)
  rather than on `UNIX AND NOT APPLE`. `gui` and `cli` move out from under the
  Linux guard; `daemon`, `tui`, `platform/linux`, and `packaging` stay Linux-only
  for now.
- Audit and repair Linux assumptions that leaked past the PAL into `core`, `app`,
  `gui`, and `cli` — identity fields, bus taxonomy, property-key expectations,
  path-shaped string handling.
- **BREAKING (source only)**: `core::Device`'s platform identity field is
  renamed to a neutral name and documented as an opaque platform-native
  identifier. The D-Bus member/argument names and the persisted snapshot JSON
  keys are deliberately **not** renamed, so ApiVersion stays 4 and existing
  snapshot stores keep loading. This is a compile-time break for out-of-tree
  callers only; there are none.

**The Windows backend.**

- A `platform/windows` backend supplying read-only implementations only:
  device enumeration, hotplug notification, and system information. Device
  identity is the Windows device instance ID; hardware IDs carry the role
  `modalias` plays on Linux.
- No `IDeviceController`, no `IDriverManager` write verbs, no
  `IPrivilegedChannel`, no `IUpdateProvider` on Windows. These are absent
  backends, not stubs that fail at call time — absence is expressed in the
  backend set and resolved by the existing availability machinery.
- Windows driver *facts* (bound driver name, version, provider, date) are read
  through the enumerator as device properties rather than through
  `IDriverManager`, because the Windows notion has no load/unload counterpart.

**The frontends.**

- The Qt GUI builds and runs on Windows, showing Devices with full detail, and
  presenting Modules, Updates, and Snapshots as unsupported-on-this-platform
  through the existing calm-degradation path rather than as errors or empty
  lists. Mutating verbs are unavailable, by the same rule that already governs a
  missing `devmgrd`.
- The `devmgr` CLI gains read-only inventory verbs (`devices list`,
  `devices show`) that go through the PAL directly and need no daemon. Today the
  CLI is snapshot-only and therefore has nothing to do on a platform with no
  helper; these verbs also make it useful on Linux hosts where `devmgrd` is not
  installed.
- The TUI stays Linux-only in this change. FTXUI's Windows console behaviour and
  the DESIGN.md parity rules deserve their own pass.

**Verification.**

- CI gains a `windows-latest` job building `core`, `app`, `platform/windows`,
  `gui`, `cli`, and running unit tests.
- An owner-gated manual smoke on real Windows hardware, in the cadence every
  prior phase used.

**Explicitly out of scope**, and named so the next change has a starting point:
macOS/IOKit, any Windows write path or privilege model, Windows packaging or
installers, the TUI on Windows, and Windows firmware update integration.

## Capabilities

### New Capabilities

- `platform-portability`: How a platform backend set is declared, selected, and
  built; what a backend is permitted to omit; how the application behaves when a
  capability has no implementation on the running platform; and the rule that
  layers above the PAL do not name a platform.
- `windows-device-inventory`: The read-only Windows backend's observable
  contract — device identity, the mapping from Windows device properties onto
  the shared device model, bus classification, hotplug delivery and coalescing,
  and system information.
- `cli-inventory`: Daemon-free read-only CLI verbs for listing and inspecting
  devices, including output shape and exit codes, on every platform that has a
  device enumerator.

### Modified Capabilities

- `backend-availability`: Add the `unsupported` kind's wording rows — a backend
  with no implementation on the running platform — and fix its severity as
  information permanently, never warning, since nothing about it is actionable.
  Distinguish it from `absent` (installable but not installed).
- `gui-presentation`: Verb availability currently composes from the active tab
  and daemon reachability. It must also account for verbs whose backing
  capability does not exist on the running platform, and a tab whose entire
  backend is unsupported must present that state rather than an empty list.
- `ui-accessibility`: The "Consistent device presentation" requirement names
  `modalias` directly and assumes udev-shaped identity fields. Restate it so
  identity and bus presentation stay consistent on a platform that supplies
  neither.
- `acceptance-suite`: Add the Windows build-and-test gate and the owner-gated
  Windows manual smoke to the release exit criteria, and record which gates are
  Linux-only.

## Impact

**Code.** `CMakeLists.txt` and every subdirectory `CMakeLists.txt`; new
`platform/windows/`; composition roots in `gui/src/gui_app.cpp`,
`tui/src/tui_app.cpp`, `cli/src/main.cpp`; `core/include/devmgr/core/models.hpp`
and its dependents (`core/src/device_key.cpp`, `core/src/device_presentation.cpp`,
`core/src/criticality.cpp`, `core/include/devmgr/services/*`); `app/` facade and
ViewModels where backend absence is resolved; `cli/src/cli.cpp` for the new verbs.

**APIs and contracts.** `devmgr::pal` interfaces gain no methods, but the
backend-set type is new. The D-Bus contract (`org.devmgr.Manager1`, ApiVersion 4)
and the persisted snapshot format are unchanged by design — the rename stops at
the C++ boundary.

**Dependencies.** Windows: `cfgmgr32`, `setupapi`, `version` (system libraries,
no new third-party dependency). The vcpkg manifest gains an `x64-windows`
triplet; `spdlog`, `nlohmann-json`, `tl-expected`, `gtest` are already portable.
Qt 6 on Windows is a developer/CI prerequisite, matching how Linux consumes
system Qt.

**Systems.** CI matrix grows a Windows runner. No change to the Linux runtime,
the `devmgrd` service, the polkit policy, or any packaging artifact.

**Risk.** The seam refactor touches every frontend's startup path, which is the
least test-covered code in the tree; a regression there is invisible to unit
tests and visible immediately to a user. Linux manual smoke is a required gate
of this change, not only the Windows one.
