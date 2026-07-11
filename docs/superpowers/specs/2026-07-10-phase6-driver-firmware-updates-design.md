# Phase 6 — Driver/Firmware Updates: Design Spec

**Date:** 2026-07-10 (rev 2 — post design-review; rev 1 same day)
**Status:** Approved w/ conditions (brainstorming 2026-07-10 + external design review). Review's 3 mandatory items spec'd: M1 reboot/offline state §8.2, M2 secure cab resolution §5.3, M3 install lifecycle §5.5. Plan ! cites all three.
**Branch:** `feature/phase_6`, stacks on `feature/phase5` (first phase after phase5→dev merge)
**Predecessors:** `2026-07-06-phase5-driver-module-management-design.md` (KmodDriverManager, StateStore, enforcement, IPC v2)
**Encoding:** ck:caveman (per user directive 2026-07-07)

---

## 1. Overview

Phase 6 delivers firmware/driver update visibility & apply: `FwupdUpdateProvider`
(LVFS firmware via fwupd daemon, query + install) + `DkmsStatusProvider`
(DKMS module build/install state, read-only) behind one core `IUpdateProvider`
seam. Both UIs gain **Updates** tab (parity, Phase 5 discipline). Also lands
11-item carry-over cleanup from Phase 5 final-review triage & wires Phase 5's
honest-`false` `rebootPending` stub to real, durable data.

### Goals

1. **fwupd provider:** enumerate firmware devices + upgrades; install
   locally-available cabs w/ attributed progress; explicit install lifecycle
   (preflight revalidation → install → finalize) safe vs TOCTOU, concurrent
   clients, daemon restart, frontend shutdown. Raw D-Bus (sdbus-c++ v2), no GLib.
2. **Durable pending/reboot model:** disposition-based install outcomes
   (Completed|Scheduled|NeedsReboot|NeedsUserAction); reboot state sticky +
   reconstructed from fwupd `GetHistory`/`GetResults`; cleared only on positive
   evidence. ⊥ derived from live candidate list.
3. **DKMS provider:** read-only status rows, documented layout contract,
   tri-state+unknown honesty. Hidden on hosts w/o `/var/lib/dkms` (Gentoo dev host).
4. **Multi-provider seam:** one `IUpdateProvider`; per-provider snapshot states
   (partial failure first-class); future providers slot in w/o UI change.
5. **Updates tab both UIs:** rows, detail pane, install verb (gated: only
   locally-installable releases actionable), availability banners, durable
   DeviceRequest banner, quit guard during flash.
6. **Carry-over T1:** 8 triaged minors + F-1/F-2/F-3 from `.superpowers/sdd/phase5-final-review.md`.

### Non-goals (deferred/rejected)

- In-app downloader / resume (libcurl) → ⊥ this phase. Remote-URL releases
  rendered **"update available — external download required"**, install verb
  disabled (⊥ enabled-then-fail), hint `fwupdmgr update`.
- In-app metadata refresh (`UpdateMetadata` = client-side download) → out. Daemon
  cache + local remotes suffice; host smoke may prime via `fwupdmgr refresh`.
  Remote staleness surfaced as notice (remote age via `GetRemotes`), ≠ state.
- Own reboot-state persistence file → ⊥. fwupd history db = durable source
  (survives app+daemon restart, authoritative); parallel store could disagree.
- DKMS rebuild/autoinstall verbs, pkg-manager install provider → future phase ?.
- DKMS distro weak-modules / exotic layouts → out of scope, labeled `unknown` (§6).
- fwupd BIOS settings, security attrs, downgrades, `Verify`/`Activate`/`Unlock` → out (YAGNI).
- Auto-reboot ⊥ (display only; host = OpenRC anyway).
- devmgrd changes beyond T1 fixes ⊥. IPC stays v2. No new polkit actions.

## 2. Locked decisions

| Decision | Choice |
|---|---|
| DKMS scope | Multi-provider iface; fwupd full (query+install); DKMS **status-only** |
| fwupd IPC | **Raw D-Bus via sdbus-c++ v2**, gated `DEVMGR_WITH_SDBUS`; libfwupd (GLib) rejected; fwupdmgr shell-out rejected |
| Downloads | **Delegate to fwupd**; no HTTP code in-app; local-cab installs only |
| Architecture | **A: frontend-direct.** UI ↔ `org.freedesktop.fwupd` system bus. fwupd's own polkit prompts user (root-proxy via devmgrd = auth bypass, rejected) |
| Local-install support | directory-kind remotes, `file://`, absolute paths — post §5.3 security checks. https → visible, ⊥ actionable |
| Release identity | `(remoteId, checksum)`; default release = fwupd `GetUpgrades` ordering first element; ⊥ version-string compare |
| Progress | Global `Status`/`Percentage` accepted ONLY while own install active; installs serialized in-process; 100% ≠ success |
| Reboot/pending state | Disposition model + sticky set + fwupd history reconciliation; ⊥ own store; clear on positive evidence only |
| Snapshot shape | Per-provider `UpdateProviderState` (partial failure first-class); keep last-good rows + banner on refresh-fail-after-success |
| Exit gate | VM fakedevice full update via our stack + host read-only smoke |
| Carry-overs | All 11 as T1, one reviewed commit before feature work |

## 3. Architecture

```
 ┌─ frontends (unprivileged) ────────────────────────────────┐
 │  TUI / GUI: Updates tab (banners/list/detail/install)     │
 │   └─ UpdatesVM (dispatcher-affine, alive-token)           │
 │   └─ ApplicationFacade: refreshUpdates / updatesSnapshot  │
 │        / installUpdate (async, TaskScheduler)             │
 │        + install lifecycle state machine (§5.5)           │
 │        + pending/reboot reconciler (§8.2)                 │
 │        ▼ core pal::IUpdateProvider (N providers)          │
 │   ┌────┴─────────────┐                                    │
 │   FwupdUpdateProvider DkmsStatusProvider                  │
 │   (sdbus-c++ v2)      (fs walk, RO)                       │
 └───────│────────────────────│──────────────────────────────┘
     D-Bus system bus     /var/lib/dkms, /lib/modules
         │
   fwupd daemon (root, own polkit: org.freedesktop.fwupd.*)
```

Principles:

- fwupd quarantined in `platform/linux` behind core iface (libkmod/udev pattern).
- Pure parse layer (`a{sv}` → structs, error table) bus-free ∴ unit-testable w/o daemon.
- devmgrd untouched (except T1). Wrapping privileged helper in another privileged helper ⊥.

## 4. Model & PAL changes (core)

### 4.1 New types (`core/update_models.hpp`)

```cpp
enum class InstallDisposition { Completed, Scheduled, NeedsReboot, NeedsUserAction };
struct InstallOutcome {
  InstallDisposition disposition;
  bool needsReboot = false;
  std::optional<std::string> observedVersion;  // ⊥ assume immediate bump (offline/scheduled)
  std::string message;
};
struct ReleaseRef { std::string remoteId; std::string checksum; };  // stable release identity
struct DeviceUpdateFacts { bool updatable = false; bool supported = false;
                           bool needsRebootAfterUpdate = false; };  // device facts ONLY
struct ReleaseInfo {
  std::string version; std::string summary;
  std::string remoteId; std::string checksum;    // == ReleaseRef fields
  std::vector<std::string> locations;            // fwupd "Locations" (fallback "Uri")
  bool localCab = false;                         // resolvable per §5.3 ⇒ actionable
  std::uint64_t sizeBytes = 0; bool isUpgrade = false;
  std::optional<std::uint32_t> installDurationSec;
};
struct UpdateCandidate {
  std::string providerId; std::string id;        // fwupd DeviceId | "dkms:<module>/<ver>"
  std::string displayName; std::string currentVersion;
  std::optional<std::string> candidateVersion;   // = releases.front().version (fwupd order)
  DeviceUpdateFacts facts;                       // updatable | supported | needsRebootAfterUpdate (device facts ONLY)
  std::vector<ReleaseInfo> releases;             // empty for dkms
  std::vector<std::pair<std::string,std::string>> details;
};
struct ProviderAvailability {
  bool available = false;
  std::optional<std::string> version;            // e.g. fwupd DaemonVersion
  std::optional<core::Error> error;              // machine state, ≠ display string
  std::vector<std::string> notices;              // e.g. "lvfs metadata 42 days old"
};
struct UpdateProviderState {
  std::string providerId;
  ProviderAvailability availability;
  std::vector<UpdateCandidate> candidates;
  std::optional<core::Error> refreshError;       // enumerate failed; availability may still be true
};
struct PendingAction {                           // app-level, from outcomes + fwupd history
  std::string providerId; std::string deviceId; std::string deviceName;
  InstallDisposition disposition; std::string version;
};
```

Flag separation (review item 10): device facts ∈ `DeviceUpdateFacts`; release
attrs ∈ `ReleaseInfo`; install/pending state ∈ `PendingAction` (⊥ on candidate ∴
⊥ survives snapshot replace accidentally); provider capability ∈
`capabilities()` (`statusOnly` row rendering derives from caps, ≠ candidate flag);
app-local progress ∈ facade lifecycle state.

### 4.2 `pal/interfaces.hpp`

```cpp
using UpdateProgressFn = std::function<void(int percent /*-1=indeterminate*/, std::string_view stage)>;
class IUpdateProvider {
  virtual std::string providerId() const = 0;
  virtual UpdateProviderCaps capabilities() const = 0;   // Query | Install
  virtual ProviderAvailability availability() const = 0;
  virtual core::Result<std::vector<core::UpdateCandidate>> enumerate() = 0;
  virtual core::Result<std::vector<core::PendingAction>> pendingActions() = 0;  // fwupd: GetHistory/GetResults; dkms: empty
  virtual core::Result<core::InstallOutcome> install(
      const std::string& candidateId, const core::ReleaseRef& release,  // ReleaseRef = {remoteId, checksum}
      const UpdateProgressFn& progress) = 0;
};
```

Invariants:

- V1: `install` reachable only for caps∋Install & `facts.updatable` &
  `release.localCab` — UI verb disabled otherwise (⊥ enabled-then-fail), provider
  re-checks & returns `Unsupported` (defense in depth).
- V2: ∀ provider paths → `Result`, ⊥ exception escapes (incl. malformed variants §7).
- V3: UI row/detail/banner formatting single-source in `UpdatesVM` (byte-frozen parity).
- V4: reboot/pending state ⊥ derived from live candidate list (§8.2).
- V5: progress accepted ⇔ own install active (§5.4).

## 5. `FwupdUpdateProvider` (platform/linux, gated `DEVMGR_WITH_SDBUS`)

### 5.1 D-Bus surface used (verified: host 2.0.20 introspection + shipped XML; all present ≥1.7)

```
iface org.freedesktop.fwupd @ /
methods: GetDevices()→aa{sv}; GetUpgrades(s)→aa{sv}; GetReleases(s)→aa{sv};
         GetRemotes()→aa{sv}; GetResults(s)→a{sv}; GetHistory()→aa{sv};
         Install(s deviceId, h cabFd, a{sv} options)
signals: Changed; DeviceAdded(a{sv}); DeviceRemoved(a{sv}); DeviceChanged(a{sv});
         DeviceRequest(a{sv})
props:   DaemonVersion(s); Status(u); Percentage(u)
NOT used: UpdateMetadata, Verify/Activate/Unlock, BIOS/security methods.
```

- Device keys mapped: `DeviceId`, `Name`, `Vendor`, `Version`, `Flags` (→ facts;
  symbolic constants pinned at plan time). Release keys: `Version`, `Summary`,
  `RemoteId`, `Locations` (fallback legacy `Uri`), `Checksum`, `Size`, `Flags`,
  `InstallDuration`.
- Parse robustness (review): known key w/ wrong variant type → key skipped +
  debug log, ⊥ throw; unknown keys/flag bits ignored + debug-logged (skew
  diagnosis); `Percentage > 100` clamped→indeterminate; oversized ints narrowed
  checked; empty `DeviceId` | missing `Version` → row dropped + debug log;
  duplicate devices/releases → dedupe by id / (remoteId,checksum).
- `GetUpgrades` error `NothingToDo` → empty list ≠ failure; per-device
  `GetUpgrades` failure → device row kept, upgrade list empty + notice
  (⊥ whole-enumerate fail).
- Skew: host 2.0.20, VM ~1.7.x; `DaemonVersion` display-only; no version floor.

### 5.2 Connection & threading

- One system-bus connection; signals via `enterEventLoopAsync`; method calls from
  facade worker threads (sdbus-c++ v2 async invocation for `Install`, timeout ≥
  release `InstallDuration`, ⊥ default 25s).
- Teardown = explicit ordered sequence (§5.5), alive token alone insufficient
  (sdbus-c++ docs: caller ensures no thread uses object during destruction).
- Signals → `UpdatesChanged` on EventBus; app-level `DelayedScheduler` debounce →
  coalesced refresh. Signal storm during flash safe; ⊥ refresh submitted after
  drain begins (Phase 2/5 teardown contract, tested).
- Service owner changes / daemon restart: match on well-known name;
  NameOwnerChanged → availability re-probe + refresh; mid-install vanish → §5.5.

### 5.3 Secure local-cab resolution (M2)

Resolution rule (verified on live fwupd: `FilenameCache` = cab **directory** for
`directory`-kind remotes; = metadata **file** for `download`-kind remotes ∴
⊥ cab resolution for download remotes):

```
location → candidate path:
  "file://<path>"        → <path>
  absolute "/..."        → as-is
  relative & remote.kind == directory → <remote.FilenameCache>/<location>
  anything else (https, empty, unknown scheme, relative w/o directory remote)
                         → localCab=false ∴ ⊥ actionable (V1)
```

Open contract (∀ items ! implemented & tested):

- Canonicalize; reject `..` traversal & escape outside allowed root
  (directory-remote root | `file://`/absolute allowed as-given post-checks).
- `open(O_RDONLY | O_CLOEXEC | O_NOFOLLOW)` — open ONCE; ⊥ reopen by pathname.
- `fstat` post-open: regular file !, size > 0, size ≤ (release.Size > 0 ?
  min(release.Size × 1.5, 512 MiB) : 512 MiB hard cap).
- fd ownership: kept alive until sdbus-c++ has serialized/taken message ownership;
  deleted-but-open file = safe (fd semantics).
- Empty/malformed location → `Unsupported`, ⊥ treated as fs path.
- Checksum of release re-verified against metadata identity at preflight (§5.5);
  content-hash verification = fwupd's job (it validates cab signatures).

`Install` options: `reason`, `no-history=false`; fd as `h`. fwupd polkit prompts
via session agent (Phase 4/5 UX). Polkit agent absent (e.g. bare TTY) →
`AuthFailed`→`Permission` + hint text, non-hanging (test: fake daemon; host
manual checklist).

### 5.4 Progress attribution (M3 part)

- V5: `Status`/`Percentage` PropertiesChanged forwarded to `UpdateProgressFn`
  ⇔ facade lifecycle == Installing (own request active). Else ignored (external
  fwupdmgr / other client ops ⊥ contaminate our UI).
- Installs serialized in-process (one at a time; queue ⊥ — second request while
  active → `Busy`). Concurrent OTHER client (GUI+TUI both open = 2 processes):
  fwupd rejects/serializes → mapped `Busy` + msg; explicit test.
- Percentage non-monotonic tolerated (multi-stage); absent/stale % w/ changing
  Status → indeterminate (-1) + stage text.
- 100% ≠ success. Authoritative = method reply + `GetResults` (§5.5 Finalizing).
- Late progress after completion/teardown → dropped (alive token + state gate).

### 5.5 Install lifecycle state machine (M3)

```
Idle → Preflight → Resolving → Installing → Finalizing → Idle
  Preflight (fresh queries, ⊥ trust snapshot):
    device ∃ (GetDevices) & updatable; release still ∈ GetUpgrades(device) matched
    by (remoteId, checksum); location & checksum unchanged; no conflicting
    pending op (GetResults). fail → Conflict "changed since refresh — refresh & retry"
  Resolving: §5.3 open+validate cab fd. fail → Unsupported | Io
  Installing: async Install; progress gated (§5.4); daemon vanish /
    NameOwnerChanged / timeout → Io + msg, → Finalizing (reconcile attempt)
  Finalizing: method reply + GetResults → InstallOutcome{disposition,...};
    observedVersion from re-read device (? absent for Scheduled/offline);
    disposition: Completed | Scheduled | NeedsReboot | NeedsUserAction
    → feed §8.2 pending/reboot state; success w/o version bump = valid (Scheduled)
```

Shutdown during flash (M3):

- Quit request while Installing → confirm modal both UIs: "firmware flash
  continues in fwupd daemon; closing does NOT cancel it". Confirmed quit allowed.
- Teardown order !: stop accepting ops → unsubscribe signals → leaveEventLoop +
  join → destroy proxies/connection → drain dispatcher (Phase 2/5 contract).
  ⊥ destroy proxy while another thread can invoke it.
- Next startup: `pendingActions()` (GetHistory/GetResults) surfaces interrupted/
  completed/scheduled/failed ops (§8.2) ∴ no false "cancelled" belief.

### 5.6 fwupd-owned guards

Battery floor, `OnlyTrusted`, dedupe, downgrade protection → fwupd enforces; we
surface refusal text. Our pre-flight = V1 gating + §5.5 Preflight only.

## 6. `DkmsStatusProvider` (platform/linux, no gate — pure fs)

Supported layout contract (= `dkms status` derivation; fixtures mirror exactly this):

```
<dkmsRoot>/<module>/<version>/<kernel>/<arch>/module/<file>.ko[.xz|.gz|.zst] → built
installed ⇔ ∃ <modulesRoot>/<kernel>/updates/dkms/<file>.ko[.xz|.gz|.zst]
  <file> = actual basename(s) found in build output dir (⊥ inferred from pkg name)
tri-state+unknown: added (source dir ∃, no build) | built | installed |
  unknown (evidence insufficient: failed-build residue, partial dirs, unreadable)
kernel ∈ registrations but ∉ /lib/modules → state "kernel absent" (shown)
symlinks: lstat-based walk; ⊥ follow links outside configured roots; ⊥ recursive follow
weak-modules / distro-specific install paths → out of scope → unknown, ⊥ "not installed"
```

- Rows: id `dkms:<module>/<version>`, currentVersion=version, caps=Query ∴
  rendered status-only; details = per-kernel state lines.
- `availability()`: available ⇔ dkmsRoot ∃ (hidden on Gentoo host, visible VM).
- Injectable roots (ctor) → fixture-tree unit tests incl. compressed exts,
  symlink traps, failed-build residue, removed-kernel registrations.
- `install` → `Unsupported`. `pendingActions` → empty.

## 7. Error mapping

Contract table (single source, tested). Target = existing
`core::Error::Code { Permission, NotFound, Busy, Io, Network, Unsupported, Conflict }`.
D-Bus error **name + message kept separately** in mapping input (⊥ flattened early);
`core::Error` message = `"<name>: <message>"` (name preserved for tests/diagnosis):

```
org.freedesktop.fwupd.NothingToDo    → enumerate: empty list; install: Conflict + msg
org.freedesktop.fwupd.AuthFailed     → Permission (auth-cancel vs deny: fwupd
                                       reports both as AuthFailed; msg text passed through)
org.freedesktop.fwupd.NeedsUserAction → Busy + msg (+ durable request banner §9)
org.freedesktop.fwupd.VersionNewer   → Conflict + msg
<unknown org.freedesktop.fwupd.*>    → Io + "<name>: <msg>" (⊥ throw)
empty error name/message             → Io + "fwupd: unknown error"
sdbus transport / daemon unreachable / activation fail → Io + reason (→ availability)
```

Full name list pinned at plan time. V2: ∀ paths → `Result`.

## 8. App layer

### 8.1 Snapshot & partial failure (review item 6)

- `refreshUpdates()` async: ∀ providers → `UpdateProviderState` (availability +
  enumerate + pendingActions). One provider fail ≠ snapshot fail: fwupd down +
  dkms ok → dkms rows + fwupd banner. Provider timeout → refreshError, others land.
- refresh-fail-after-previous-success → keep last-good rows + banner
  "refresh failed: <reason>" (deliberate retain, tested).
- States distinguished: unavailable | available+empty | available+remote-only
  (rows shown, verb disabled) | available+stale-metadata (notice) | refresh-failed.
- `updatesSnapshot()` = last per-provider states + pending actions.

### 8.2 Durable pending/reboot state (M1)

- Facade holds `PendingAction` set: fed by (a) own install outcomes
  (disposition ≠ Completed), (b) `pendingActions()` reconciliation ∀ refresh &
  startup (fwupd `GetHistory` + `GetResults` = durable source across app/daemon
  restarts).
- V4: ⊥ derived from live candidate list (candidate vanishing post-install ⊥
  clears banner; device temporarily absent ⊥ clears banner).
- Clear entry ⇔ positive evidence only: fwupd history/results no longer reports
  pending/needs-reboot for device (post-reboot fwupd updates its history), |
  device re-read shows target version & no pending state. Boot-id comparison ⊥
  needed in-app — fwupd already reconciles its history across reboots; we mirror it.
- Effective `rebootPending` = `systemInfo.rebootPending (PAL stub, stays false)`
  || `∃ PendingAction{NeedsReboot}` → banner + row marker both UIs.
- `installUpdate(providerId, candidateId, ReleaseRef)`: drives §5.5; progress →
  StatusLineVM task msgs + row state; on terminal outcome → targeted re-enumerate.

### 8.3 UpdatesVM

- Dispatcher-affine, alive-token, coalescing, teardown stress (ModulesVM
  discipline + §5.5 ordering). Row/banner/detail single-source (V3).
- Banner inputs: per-provider `ProviderAvailability` (+version, notices) +
  pending/reboot state + `LinuxSystemInfo` Secure Boot line (Phase 5 reuse).

## 9. UI (parity, Phase 5 discipline)

- TUI: Updates screen in tab cycle. Keys: nav, `u` install (confirm modal:
  version delta + needs-reboot warn + duration), `r` refresh, Escape/q consistent
  (post-T11-m-2). Placeholder ⊥ actionable.
- GUI: Updates tab; toolbar/context install + refresh; progress in status bar;
  action enablement tab-aware (F-1 fix folds in).
- Remote-only release rows: visible, verb disabled, detail shows "external
  download required — run `fwupdmgr update`" (V1; ⊥ enabled-then-fail).
- `DeviceRequest` (review item 7): **durable banner** w/ request kind/id
  (immediate|post-op), ⊥ overwritten by progress/status msgs; cleared on
  explicit user dismiss | request resolution (DeviceChanged); both UIs, tested.
- Quit guard during Installing: confirm modal (§5.5) both UIs.

## 10. Carry-over cleanup (T1, one commit)

| Item | Fix |
|---|---|
| T4 m-2 | `state_store.cpp` `.bad` → timestamped `.bad-<epoch>` (⊥ overwrite evidence) |
| T4 m-4 | `fs::remove(tmp)` before error returns (⊥ orphan `state.json.tmp`) |
| T4 m-5 | `is_array()` check on `entries` (⊥ silent drop on `{"entries": null}`) |
| T5 m-1 | `interfaces.hpp:53-56` `driversFor` doc → match pinned first-element-bound contract |
| T6 m-2 | kmod taxonomy test matrix: += EPERM-unload, EBUSY-load, Io fallback, unload-ENOENT, empty-holders |
| T9 m-1 | `dbus_privileged_channel.cpp` `m.at()` → `contains()`-checked loop or `catch(const std::exception&)` (⊥ escape kills refresh worker) |
| T9 m-2 | enforcement sweep-fallback tests += mechanism=="unbind" branch |
| T11 m-2 | TUI Escape quits from Modules tab (parity w/ Devices) |
| F-1 | GUI refusals → StatusLineVM (⊥ statusBar wipe); gate devices-side `canDisable` probe on tab/sender |
| F-2 | `phase5-smoke.sh` `sleep 1` → poll `busctl status org.devmgr.Manager1` w/ timeout |
| F-3 | `applyEnable` removal scan += `matchesDevice(e.key, deviceFromSysfs(canonical))` |

## 11. Testing

### 11.1 Unit (host+container)

- `FakeUpdateProvider` → facade/UpdatesVM: teardown stress, coalescing,
  alive-token, partial-provider snapshots, pending/reboot state transitions.
- fwupd parse layer pure-fn matrix: dict→struct; `Locations`/`Uri` fallback;
  flag bits incl. unknown bits; malformed variants (wrong type ∀ recognized key);
  `Percentage>100`; int narrowing (`Size`); duplicate device/release dedupe;
  empty `DeviceId`; missing `Version`/location; error table incl. empty name/msg.
- Cab resolution: traversal (`..`), symlink fixture, non-regular file, oversize,
  empty/malformed/unknown-scheme location, download-remote relative (rejected).
- `DkmsStatusProvider` fixtures: built/installed/added/unknown; `.ko.xz`/`.ko.gz`/
  `.ko.zst`; symlink traps; failed-build residue; kernel-absent registrations.
- V1 gating: statusOnly / !updatable / !localCab rows never actionable.

### 11.2 Integration (container, fake fwupd daemon)

Fake `org.freedesktop.fwupd` (hand-written sdbus-c++ server, ours) under
`dbus-run-session`; Dockerfile: no fwupd pkg. Scripted scenarios (review-required
tests numbered):

1. daemon restart: drop+reacquire bus name → availability re-probe, refresh, no crash
2. frontend destruction mid-install: no UAF/callback-after-free; no false-cancel
   (next start reconciles via GetHistory)
3. concurrent client: fake external client mid-install → our op unaffected;
   our second request → Busy
4. remote-only release: visible, verb disabled, guidance text
5. traversal/symlink cab → rejected BEFORE any D-Bus call
6. stale candidate: metadata mutated between enumerate & install → Preflight
   Conflict, ⊥ Install called
7. offline update: reply success, version unchanged → disposition Scheduled,
   pending action recorded
8. reboot persistence: candidate disappears post-install → banner remains
   (history-driven), clears on scripted positive evidence
9. malformed variants over real bus round-trip
10. partial provider: fwupd fails, dkms rows still shown
11. progress contamination: unrelated property changes while Idle → ignored
12. missing polkit agent: AuthFailed → Permission + hint, non-hanging
13. (unit §11.1) dkms compressed/symlink
14. signal storm during teardown: ⊥ refresh after drain begins
+ fd `Install` + scripted Status/Percentage progress assertions; NameOwnerChanged
  mid-install; GetUpgrades partial failure (some devices error).

### 11.3 VM (`test/vm/phase6-smoke.sh`)

Provision fwupd+dkms (Vagrantfile update); enable `fwupd-tests` directory remote
(`fwupdmgr enable-test-devices`; fallback: enable remote conf manually);
`devmgr_fwupd_smoke` integration binary drives OUR provider: enumerate →
fakedevice upgrade visible (localCab=true via directory remote) → install →
version bump + `GetResults` + disposition asserted. DKMS fixture module
registered → status rows correct. Phase 4+5 smokes stay green same run.

### 11.4 Host manual smoke (exit gate, read-only)

Both UIs: Updates tab lists real fw devices + pending LVFS upgrades (remote-only
→ verb disabled + guidance); banner `fwupd 2.0.20` + Secure Boot line; dkms rows
absent; polkit-agent-absent path sane (TTY session); TUI/GUI parity. ⊥ real flash.

### 11.5 Standing gates

sdbus ON+OFF builds; full ctest; CI-form clang-format (18.1.8 + 21.1.8) +
clang-tidy; purity greps += **no-GLib** (`glib|gobject` ∉ core/app/tui/gui/platform);
container suite; `PHASE4|5|6 VM SMOKE OK`; parity ledger; final whole-branch review.

## 12. Risks / open items (→ plan)

- fwupd dict-key drift 1.7↔2.0 (`Locations` vs `Uri`, flag bits) — parse both,
  pin constants at plan time; unknown keys debug-logged.
- `enable-test-devices` availability in VM's fwupd — verify early; fallback §11.3.
- sdbus-c++ event-loop + async-call teardown — §5.5 order; pin exact code pattern
  from `DbusPrivilegedChannel` precedent + sdbus-c++ v2 docs before UI tasks.
- fwupd history semantics for Scheduled/offline updates on VM 1.7 — verify
  `GetResults` shape early in plan (affects §8.2 reconciler).
- ASan note carried (Phase 5 T10 m-7): if CI gains ASan job, run UAF suites under it.

## 13. Exit gate checklist

1. T1 carry-over commit reviewed & landed.
2. M1/M2/M3 invariants demonstrably tested (§11.2 scenarios 2/5/6/7/8/11/14 + §11.1 cab matrix).
3. Host 100% ctest; container suite green.
4. `PHASE6 VM SMOKE OK` (+ Phase 4/5 still OK).
5. Host read-only manual smoke (§11.4) both UIs.
6. Parity ledger complete; purity greps (incl. no-GLib) clean.
7. Final whole-branch review (opus) approved.
8. Docs: README Updates section; spec + plan checkboxes closed.
