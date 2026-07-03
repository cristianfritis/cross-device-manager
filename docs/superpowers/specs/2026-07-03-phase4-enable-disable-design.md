# Phase 4 — Enable/Disable + privileged helper — Design

**Status:** DESIGNED — brainstormed and approved 2026-07-03. Next: implementation plan (`docs/superpowers/plans/`).
**Milestone:** Safely disable and re-enable a **non-critical USB device from either UI**, through a polkit-gated root helper (`devmgrd`). First mutating phase.
**Builds on:** Phases 1–3 (`feature/phase4` branch, forked from committed Phase 3 work).

---

## Decisions locked (brainstorm 2026-07-03)

| Decision | Choice | Rationale |
|---|---|---|
| sdbus-c++ version | **v2 API only.** Host: `dev-cpp/sdbus-c++ 2.3.1` (portage). CI/Docker: Ubuntu 24.04 base + pinned **v2.3.1 source build** in the Dockerfile | Ubuntu 24.04 ships 1.4.0 and v2.0 was a breaking API rewrite — no clean floor exists. Source build (small lib, `libsystemd-dev` satisfies sd-bus) keeps the 24.04 Qt-floor story untouched. Rejected: vcpkg (drags a libsystemd/meson chain, messy on non-systemd Gentoo host); 1.4-floor compat shim (permanent noise, host/CI would test different majors) |
| IPC contract scope | **Minimal + versioned**: one interface, `SetDeviceEnabled` + read-only `ApiVersion=1` property | D-Bus interfaces extend cleanly; Phase 5–7 verbs get designed in their own phases. YAGNI |
| Critical-device policy | **Refuse outright** (`Error::Conflict` + reason); no force/override path | Safest first mutation phase; an override can be added later if ever needed |
| polkit defaults | **`auth_admin_keep`** (active session), `auth_admin` otherwise | Admin password, cached ~5 min — standard desktop-system-tool posture (fwupd, GNOME Disks) |
| Disable mechanism | **USB `authorized` attribute only.** Non-USB → `Error::Unsupported` | The one clean, fully round-trippable mechanism; disabled-state derives purely from sysfs (no daemon state). bind/unbind + PCI need driver knowledge → Phase 5 |
| Architecture | **Approach A: seamed daemon** — thin composition root over `devmgr_core`; injectable sysfs root; `IAuthority` polkit seam; sdbus-c++ quarantined to two leaf files; full IPC round-trip tested on a private session bus in Docker | Matches Phase 0–3 discipline: the first privileged component must be the *most* CI-tested, not the least. Rejected: direct daemon (VM-only verification); pkexec one-shot helper (loses mutation serialization, defers rather than saves the daemon) |

---

## Context

Phases 1–3 built read-only enumeration, hotplug, and two frontends over one core. Phase 4 adds the **write path**: unprivileged frontends ask a root daemon to flip a USB device's `authorized` attribute, gated by polkit and a criticality policy. The PAL stubs for this have existed since Phase 0 (`IDeviceController`, `IPrivilegedChannel` in `core/include/devmgr/pal/interfaces.hpp`); Phase 4 gives them their first real implementations.

Host environment facts: Gentoo/**OpenRC** (no systemd — daemon runs manually in the foreground for dev; D-Bus activation and the systemd unit are Phase 8), polkitd running, `pkexec` present. Supported auth path is a **graphical session** (desktop polkit agent prompts — covers GUI and TUI-in-a-terminal-emulator, including the manual smoke). Pure-console auth (`pkttyagent` collides with FTXUI's raw-mode terminal ownership) is a documented limitation. Root callers are implicitly authorized by polkit — this is what the VM E2E uses.

## Architecture & process model

```
devmgr-tui / devmgr-gui  (unprivileged; layering unchanged)
        │ ApplicationFacade::setDeviceEnabled(id, enabled) → TaskScheduler worker
        ▼
DbusPrivilegedChannel (platform/linux; implements IPrivilegedChannel; sdbus leaf #2)
        │ D-Bus system bus: org.devmgr.Manager1.SetDeviceEnabled(s sysfs_path, b enabled)
        ▼
devmgrd (daemon/, root; 4th composition root over devmgr_core)
   1. validate: canonicalize path; must be a USB device with an `authorized` attr
   2. CriticalDeviceGuard — pure core policy × fresh LinuxCriticalityProber facts
   3. IAuthority::check — polkit CheckAuthorization (interactive, AllowUserInteraction)
   4. SysfsDeviceController::setEnabled — write `authorized` 0/1
        │ D-Bus error name ⇄ core::Error::Code, mapped both directions
        ▼
TaskCompletedEvent on EventBus → UI triggers standard refresh → status shows Disabled
```

Rules:
- `devmgrd` is a composition root exactly like the TUI/GUI binaries — no business logic in D-Bus handler code.
- **Guard before polkit**: a policy-refused request never triggers a password prompt (criticality facts are world-readable sysfs anyway).
- **Serialization for free**: sdbus-c++ dispatches methods on its event-loop thread → mutations are serial by construction (the master plan's concurrent-instances answer). The loop blocking during interactive auth is accepted Phase 4 semantics, bounded by timeouts.
- The daemon trusts nothing from the client: re-canonicalizes the path, re-derives device facts from sysfs, re-runs the guard.

## D-Bus contract (v1)

- Bus name / interface: `org.devmgr.Manager1`; object path `/org/devmgr/Manager1`; **system** bus (daemon flag `--bus session` for tests).
- `SetDeviceEnabled(s sysfs_path, b enabled) → ()` — errors via named D-Bus errors (table below).
- Property `ApiVersion (u) = 1`, read-only. Phase 4 clients don't check it (single version); it exists so future clients can detect skew.
- polkit action: `org.devmgr.set-device-enabled` — `allow_active=auth_admin_keep`, `allow_inactive/allow_any=auth_admin`.
- Data files in `daemon/data/`: `org.devmgr.Manager1.conf` (D-Bus system policy: root may own the name; anyone may call — polkit does the real gating) and `org.devmgr.policy`. Manual install documented in README (`/etc/dbus-1/system.d/`, `/usr/share/polkit-1/actions/`); packaged installs are Phase 8.

## Components

### `daemon/` — new top-level directory
- **`devmgrd_lib`** (static) + thin `devmgrd` main, so unit tests link the logic without the bus:
  - `RequestProcessor` — the validate → guard → authorize → act pipeline: `Result<void> setDeviceEnabled(const CallerId&, const std::string& sysfsPath, bool enabled)`, where `CallerId` is the caller's unique D-Bus bus name (string). Depends only on `IDeviceController&`, `ICriticalityProber&`, `IAuthority&`.
  - `IAuthority` (daemon-local interface): `Result<bool> checkAuthorized(const CallerId&, const std::string& actionId)`. Impls: `PolkitAuthority` (calls `org.freedesktop.PolicyKit1.Authority.CheckAuthorization` over the same bus — **no libpolkit dependency**), `AllowAllAuthority` / `DenyAllAuthority` (tests, `--authority` flag).
  - `ManagerAdaptor` — sdbus-c++ **leaf file #1**: D-Bus ⇄ `RequestProcessor` translation only.
  - Flags: `--bus system|session`, `--sysfs-root <path>`, `--authority polkit|allow-all|deny-all`. Non-default flag combinations log a loud warning (test seams, not production config).
- Links `devmgr_core` + `devmgr_platform_linux` + `sdbus-c++`; spdlog to stderr.

### `platform/linux/` additions
- `SysfsDeviceController : pal::IDeviceController` — injectable sysfs root (defaults `/sys`); resolves the device, requires bus==Usb and an `authorized` attribute, writes `0`/`1`. Used **in-process by the daemon only**.
- `LinuxCriticalityProber : core ICriticalityProber` — gathers `CriticalityFacts`: root/boot filesystem backing-device sysfs paths (via `/proc/self/mounts` → block device → sysfs ancestry, walking dm/RAID `slaves/` recursively) and input devices from `/sys/class/input` classified keyboard/pointer.
- `DbusPrivilegedChannel : pal::IPrivilegedChannel` — sdbus-c++ **leaf file #2**: blocking proxy call, **120 s timeout** (interactive-auth budget), maps D-Bus error names back to `core::Error`. Bus selectable for tests.
- Mapper change: USB device with `authorized == 0` → `DeviceStatus::Disabled` (existing udev mapper).

### `core/` additions
- `CriticalityFacts` + `ICriticalityProber` (pure data + interface, `core/pal/` — it is a platform-probing contract like the other PAL interfaces).
- `CriticalDeviceGuard` (`core/services/`) — **pure function** `GuardVerdict evaluate(const CriticalityFacts&, const std::string& targetSysfsPath)`; `GuardVerdict{bool allowed; std::string reason;}`. Rules (path-prefix on canonical paths with `/`-boundary):
  - any root/boot backing path under (or equal to) the target → refuse `"backs the root/boot filesystem"`;
  - a keyboard is under the target and **no keyboard remains outside it** → refuse `"would disable the only keyboard"`; same for pointers.
- `IPrivilegedChannel::setDeviceEnabled` signature changes from `(const DeviceId&, bool)` to `(const core::Device&, bool)` — the wire needs `sysfsPath`; the stub has no implementations yet, so the change is free.

### `app/` additions
- `ApplicationFacade::setDeviceEnabled(const core::DeviceId& id, bool enabled) → std::future<void>` — `refresh()` pattern: resolves the `Device` snapshot (missing → immediate failed `TaskCompletedEvent`, satisfied future), schedules a worker task (id `set-enabled:<deviceId>`) that calls the channel and publishes exactly one `TaskCompletedEvent{ok, message}`. Same future-custody contract as `refresh()` (caller prunes/drains; both UIs already have this machinery from Phase 2).
- Advisory guard for UX: the facade exposes `GuardVerdict canDisable(const DeviceId&)` using the injected `ICriticalityProber` + pure guard (probe on demand; no caching). Advisory only — the daemon re-checks authoritatively.
- Constructor gains the two optional collaborators (`IPrivilegedChannel*`, `ICriticalityProber*`). Null channel → `setDeviceEnabled` fails `Unsupported("built without privileged-helper support")`. Null prober → `canDisable` returns *allowed* (advisory unavailable; the daemon remains authoritative).

### UI (parity in both frontends)
- TUI: key **`e`** toggles enable/disable on the selected device row; inline y/n confirm; result in the status line. GUI: toolbar + context-menu Enable/Disable with `QMessageBox` confirm.
- Both consult `canDisable()` first: refused → action greyed/annotated with the reason, request never sent.
- On `TaskCompletedEvent{ok}` the frontends trigger their standard refresh (deauthorization also detaches USB interfaces → hotplug events arrive; both paths converge on the debounced Phase 2 rebuild flow). No `Transitioning` state this phase — the op is one fast sysfs write; the running task shows via the existing status-line pattern.

### Build gating
- Mirror the Qt pattern: `find_package(sdbus-c++ 2 QUIET)` → option default ON when found; **the tree still configures/builds without it** (daemon + `DbusPrivilegedChannel` skipped; composition roots pass null channel/prober → `Unsupported`).
- Dockerfile: pinned sdbus-c++ **v2.3.1** source build (`libsystemd-dev`; codegen OFF) into the image.

## Error handling

One mapping table, both directions (daemon throws named errors → client maps to `core::Error`):

| Condition | D-Bus error name | `Error::Code` | UI message |
|---|---|---|---|
| Guard refusal | `org.devmgr.Error.CriticalDevice` | `Conflict` | guard reason verbatim |
| polkit denied / dialog dismissed | `org.devmgr.Error.NotAuthorized` | `Permission` | "authorization denied" |
| Path invalid / device vanished | `org.devmgr.Error.NotFound` | `NotFound` | "device no longer present" |
| Not USB / no `authorized` attr | `org.devmgr.Error.Unsupported` | `Unsupported` | "enable/disable not supported for this device" |
| sysfs write failed | `org.devmgr.Error.Io` | `Io` | message incl. errno text |
| Daemon absent (`ServiceUnknown`) | — | `Io` | "helper devmgrd is not available" |
| Call timeout (`NoReply`) | — | `Busy` | "helper timed out" |

Rules: every failure ends in **exactly one** `TaskCompletedEvent{ok=false, message}` — no silent drops, no double reporting via `ErrorEvent`. Mid-flight unplug is safe (write fails → reported → refresh reconciles). Concurrent instances queue serially on the daemon loop.

## Testing & verification

- **Unit (host + CI, every push):** `CriticalDeviceGuard` table-driven (facts × targets; sole-keyboard/pointer edges; path-boundary cases like `/sys/…/1-1` vs `1-10`); `SysfsDeviceController` on a temp-dir fake sysfs tree (success, non-USB, missing attr, write failure); `RequestProcessor` with fakes (ordering: guard refusal short-circuits before authority; deny-all → Permission; happy path); error-mapping round-trip; facade `setDeviceEnabled` with `FakePrivilegedChannel` (single completion event, teardown drain, null-channel → Unsupported); mapper `authorized→status`.
- **Integration (Docker, every push):** `dbus-run-session` private bus → real `devmgrd --bus session --sysfs-root <tmp> --authority allow-all` + real `DbusPrivilegedChannel`: round-trips for success (attr actually flips), guard refusal, deny-all → Permission, bad path, unsupported device, daemon-absent → Io. Runs unprivileged.
- **VM (dangerous, user-run, gated):** real system bus + real polkit; daemon as root with installed `.conf`/`.policy`; QEMU-attached spare USB device (`usb-storage` not backing root); root `busctl call … SetDeviceEnabled` (root ⇒ implicit polkit authorization, no agent needed) → verify `authorized` flips both ways.
- **Host manual smoke (phase exit gate):** install the two data files; run `devmgrd` as root in the foreground (OpenRC — no unit); in a graphical session toggle a spare USB device from **both** TUI and GUI: polkit prompt appears once (`auth_admin_keep` caches), status flips Disabled⇄Active in both UIs, critical devices (e.g. the sole keyboard, root-disk hub chain) show greyed/refused with reason.
- **Gates:** existing build + ctest + `clang-format` + `clang-tidy` extended to `daemon/`; purity greps extended — Qt/FTXUI stay out of `daemon/`; **sdbus-c++ includes allowed only in the two leaf files** (daemon adaptor, client channel); core/app stay sdbus-free.

## Key risks

- **sdbus-c++ v2-only** — the container must build the pinned tag; drift between host 2.3.1 and pin is caught by CI compiling the same tag.
- **Interactive auth blocks the daemon loop** — accepted (serialization is a feature); bounded by client 120 s timeout. Revisit if/when long-running verbs arrive (Phase 6).
- **Guard coverage is deliberately narrow** — root/boot storage + sole input only; e.g. a network adapter mid-SSH is *not* protected (console/remote use is out of supported scope this phase). Documented residual.
- **No polkit agent in session** → auth fails as `Permission` with a clear message; documented (graphical session is the supported path).
- **First root-privileged code in the repo** — mitigations: daemon validates everything itself, tiny verb surface, guard-before-auth, no client-supplied data reaches sysfs writes except the canonicalized+validated path, CI covers the full pipeline with fakes + private-bus integration.

## Out of scope

Bind/unbind + PCI remove/rescan (Phase 5). Snapshot-before-mutation + rollback (Phase 7). `Transitioning` state machine/timeouts. Console (`pkttyagent`) auth. Force-override for critical devices. Task *progress* streaming (completion only). D-Bus activation, systemd unit, packaged install of policy files (Phase 8). Any non-Linux path.
