# devmgr

Manage devices, drivers, firmware, and state snapshots on Linux — one
polkit-gated daemon (`devmgrd`), a Qt 6 GUI (`devmgr-gui`), a terminal UI
(`devmgr-tui`), and a recovery CLI (`devmgr`).

## Install (beta)

### Ubuntu 22.04 / 24.04 (.deb)

1. Download `devmgr_*_amd64.deb` and `SHA256SUMS` from the
   [latest release](https://github.com/cristianfritis/cross-device-manager/releases).
2. Verify the download, then install:
   ```sh
   sha256sum -c SHA256SUMS --ignore-missing
   sudo apt install ./devmgr_*_amd64.deb
   ```
3. From a **graphical session** (the polkit prompt needs a desktop agent),
   launch `devmgr-gui` — or `devmgr-tui` in a terminal. No daemon setup:
   `devmgrd` starts on demand via D-Bus activation.

### Fedora (.rpm)

1. Download `devmgr-*.x86_64.rpm` and `SHA256SUMS` from the
   [latest release](https://github.com/cristianfritis/cross-device-manager/releases).
2. Verify the download, then install:
   ```sh
   sha256sum -c SHA256SUMS --ignore-missing
   sudo dnf install ./devmgr-*.x86_64.rpm
   ```
   `dnf` resolves the Qt6/udev/kmod dependencies; there is no manual
   `sdbus-c++` step (it is statically linked, same as the deb).
3. Launch `devmgr-gui` from a graphical session, or `devmgr-tui` in a terminal.
   `devmgrd` bus-activates on first use — no daemon setup.

### Other distros (tarball)

Download `devmgr-*-linux-x86_64.tar.gz` plus `SHA256SUMS`, verify, and run the
bundled installer (detects systemd vs OpenRC; binaries go to
`/usr/local/bin`):

```sh
sha256sum -c SHA256SUMS --ignore-missing
tar xf devmgr-*-linux-x86_64.tar.gz
sudo devmgr-*-linux-x86_64/packaging/install.sh
```

Uninstall with `packaging/uninstall.sh`; add `--purge` to also delete the
daemon state in `/var/lib/devmgrd`. (`apt remove` / plain uninstall preserve
that state for reinstalls.)

## Verifying a download

Every release asset is **signed** (minisign) and carries a **build-provenance
attestation**, in addition to `SHA256SUMS`. Verifying the signature on
`SHA256SUMS` and then checking your files against it is enough to trust the whole
set.

**Signature** — download the public key
[`packaging/signing/devmgr.pub`](packaging/signing/devmgr.pub) once, then:

```sh
# verify the checksum file's signature, then trust the checksums it contains
minisign -Vm SHA256SUMS -p devmgr.pub
sha256sum -c SHA256SUMS --ignore-missing
```

Each asset also ships its own `<file>.minisig`, so you can verify one file
directly: `minisign -Vm devmgr_*_amd64.deb -p devmgr.pub`.

**Provenance** (optional, needs the [GitHub CLI](https://cli.github.com/))
confirms the asset was built by this repo's release workflow from the tagged
commit:

```sh
gh attestation verify devmgr_*_amd64.deb \
  --repo cristianfritis/cross-device-manager
```

## Removing & upgrading

**State is preserved by default.** `/var/lib/devmgrd` — every snapshot and all
daemon state — survives removal and upgrades. Only an explicit purge deletes it:

| Format  | Remove (keep state)     | Full cleanup (delete state)        |
|---------|-------------------------|------------------------------------|
| deb     | `sudo apt remove devmgr`| `sudo apt purge devmgr`            |
| rpm     | `sudo dnf remove devmgr`| `sudo dnf remove devmgr` then `sudo rm -rf /var/lib/devmgrd` |
| tarball | `sudo packaging/uninstall.sh` | `sudo packaging/uninstall.sh --purge` |

rpm has no purge concept, so `dnf remove` / `rpm -e` always keep the state dir;
remove it by hand for a full cleanup.

**Upgrading a package install** (deb over deb, rpm over rpm) preserves state and
all pre-upgrade snapshots and needs no manual migration — install the newer
package the same way you installed the first; the daemon restarts on the new
version and every prior snapshot still lists and restores. An install
interrupted mid-transaction is recoverable by simply re-running it (the
maintainer scripts are idempotent and never leave a half-configured daemon).

**Switching from the tarball to a package:** the tarball and the packages share
the systemd/D-Bus/polkit paths (only the binary dir differs — the tarball uses
`/usr/local/bin`). **Uninstall the tarball first, then install the package:**

```sh
sudo packaging/uninstall.sh          # keeps /var/lib/devmgrd; snapshots survive
sudo apt install ./devmgr_*_amd64.deb   # or: sudo dnf install ./devmgr-*.x86_64.rpm
```

Do not run the tarball uninstaller *after* installing the package — it would
remove the now-package-owned unit and policy files.

**Upgrading from an old source install:** remove any manually copied policy
files first, or at minimum re-install the polkit policy — the pre-Phase-7 file
lacks the snapshot action and every mutating snapshot verb will be refused (see
[Authorization (snapshot verbs)](#authorization-snapshot-verbs)):

```sh
sudo install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
```

## Enable/Disable (Phase 4)

Cross-device-manager can disable and re-enable a **USB** device from either UI.
It toggles the kernel's per-device `authorized` sysfs attribute only — no other
bus is mutated this phase. The mutation always travels through a polkit-gated
root helper (`devmgrd`); the UIs never write sysfs directly.

## Driver & Module Management (Phase 5)

Phase 5 extends enable/disable to every bus and adds kernel-module management:

- **Universal, persistent enable/disable with active enforcement.** Non-USB
  devices (PCI, virtio, …) are disabled by unbinding their driver; USB keeps
  the `authorized` mechanism. Re-enable performs a targeted `driver_override`
  rebind. A disable now *sticks*: the daemon records the device identity in
  its state store, re-applies it when the device reappears (replug/hotplug),
  and sweeps on startup — so a disabled device stays disabled across replugs
  and daemon restarts.
- **Module load/unload with Secure Boot/lockdown awareness.** A **Modules**
  view in both UIs lists loaded kernel modules (the SIGNED column fills
  asynchronously) under a banner showing the machine's Secure Boot and kernel
  lockdown state. Load/unload travel through the same polkit-gated helper;
  unloading a module that backs a critical device is refused with a reason,
  and modules blacklisted in `modprobe.d` refuse to load.
- **Surgical bind/unbind.** Advanced per-device driver bind/unbind verbs that
  deliberately leave no persistent state — a replug returns the kernel
  default binding.

New runtime dependency: **libkmod** (module enumeration and load/unload). The
daemon persists its state under `/var/lib/devmgrd`; override the directory
with `--state-dir PATH` for testing (never in production).

### Running it on a real host

The helper is not installed by a service manager yet — run it in the
foreground:

1. Install the D-Bus and polkit data files:
   ```sh
   sudo install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
   sudo install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
   ```
2. Start the helper (watch its log lines):
   ```sh
   sudo ./build/linux-debug/daemon/devmgrd
   ```
3. In a **graphical session**, launch either UI and select a device:
   - TUI: `./build/linux-debug/tui/devmgr-tui` — press `e`, confirm with `y`.
   - GUI: `./build/linux-debug/gui/devmgr-gui` — use the **Disable/Enable**
     toolbar (or right-click context) action and confirm the dialog.

### Authorization

The polkit action uses `auth_admin_keep`: the first disable/enable prompts for
an administrator password, and the grant is cached (~5 minutes) so the reverse
toggle needs no new prompt. **A desktop polkit agent must be running** —
pure-console authentication is not supported this phase, so run the UIs from a
graphical session.

### Safety guard

Before sending a *disable*, both UIs consult an advisory guard (the daemon
re-checks authoritatively on every request). It refuses, with a reason, to
disable devices that would break the running system:

- a device backing the root filesystem ("backs the root filesystem"),
- the only keyboard or only pointer ("would disable the only keyboard").

Refused devices show the reason on the TUI status line and as a greyed-out
action with a tooltip in the GUI. Re-enabling is never guarded.

## Firmware & driver updates (Phase 6)

Phase 6 adds an **Updates** tab to both UIs, covering device firmware (via
fwupd) and DKMS out-of-tree kernel module status.

- **Updates tab, both UIs.** TUI: third tab in the cycle (`Devices` →
  `Modules` → `Updates`); `u` installs the selected release (confirm modal:
  version delta, needs-reboot warning, estimated duration), `r` refreshes.
  GUI: a dedicated **Updates** tab with the same install/refresh actions on
  the toolbar and row context menu, progress shown in the status bar.
- **fwupd, frontend-direct.** `FwupdUpdateProvider` talks to
  `org.freedesktop.fwupd` directly over the system D-Bus (sdbus-c++ v2) —
  unlike enable/disable and module management, updates never go through
  `devmgrd`. One connection per process; daemon signals (`DeviceAdded`,
  `DeviceChanged`, …) are forwarded onto the app `EventBus` and debounced
  into a coalesced refresh. fwupd's own polkit action gates the privileged
  `Install` call, so a desktop polkit agent is required exactly as for
  Phase 4/5 — a bare TTY session gets a non-hanging "no polkit agent" error
  instead of a hang.
- **Local-cab-only installs.** A release is only actionable when its cab
  file resolves to a path we can open ourselves (`file://`, an absolute
  path, or a relative path under a `directory`-kind remote's cache) — never
  an `https://` download. Remote-only releases are still shown, so you can
  see what's available, but their install action is disabled; the detail
  pane explains "external download required — run `fwupdmgr update`", which
  performs the network fetch that fwupd's own CLI supports and that we
  deliberately do not reimplement.
- **DKMS: status only.** `DkmsStatusProvider` walks `/var/lib/dkms` and
  `/lib/modules` read-only — no `dkms` binary invocation, no D-Bus. Rows
  report a per-kernel state (`added` / `built` / `built + installed` /
  `unknown`) as a status view only; DKMS rows never have an install action.
- **Reboot-required banner.** An install that completes with disposition
  `NeedsReboot` (or `Scheduled`/offline) is recorded as a durable pending
  action — independent of whether the device stays visible in the live
  candidate list — and surfaces as a banner plus a row marker in both UIs
  until fwupd's own history reports it resolved (e.g. after the reboot
  actually happens). The banner also carries the provider's availability
  line (e.g. `fwupd 2.0.20`) and the host's Secure Boot state.

## Snapshots & rollback (Phase 7)

Every mutating operation now has an automatic safety net. Before the daemon
executes any mutating verb (device enable/disable, module load/unload, driver
bind/unbind), it takes an **auto snapshot** of all devmgr-owned state: the
`state.json` entry list (which carries each disable's mechanism, including
driver unbind/override state) and the content of every devmgr-written
`modprobe.d` file (`devmgr-*.conf`). Snapshot creation is **fail-closed**: if
the snapshot cannot be written, the mutation is refused with an Io error —
a change without an undo is worse than no change.

- **Store.** One JSON file per snapshot under `/var/lib/devmgrd/snapshots/`,
  identified by the SHA-256 of its payload and linked to its parent (a linear
  history with a `HEAD` pointer). Unchanged state is deduplicated: two
  mutations with no state change between them share one snapshot. Auto
  snapshots are pruned to the newest 20; **manual** snapshots are exempt and
  persist until deleted. A snapshot whose content no longer matches its hash
  is quarantined (renamed `*.bad-<timestamp>`), listed as corrupt, and never
  restored from or silently deleted.
- **Restore.** Restoring a snapshot first takes its own auto safety snapshot
  (so a restore is undoable), then atomically writes the payload back and
  converges hardware: entries in the restored state are re-applied through
  the existing enforcement path, and devices whose entries disappeared get a
  re-enable attempt. The per-device criticality guard is re-checked and can
  refuse individual items — refusals are reported in the per-item outcome
  summary, never bypassed, so a restore may complete *partially converged*.
- **Surfaces.** A **Snapshots** tab in both UIs (TUI: 4th tab, `s` create /
  `r` restore / `x` delete with confirmation modals; GUI: same verbs as
  toolbar actions with Qt confirmation dialogs) and a minimal recovery CLI:

  ```sh
  devmgr snapshot list [--json]        # one row per snapshot; --json for raw metadata
  devmgr snapshot create [--label t]   # manual snapshot, prints its id
  devmgr snapshot restore <id>         # id may be any unique prefix
  devmgr snapshot delete <id>
  ```

  The CLI has zero UI dependencies — it is the recovery path when the UIs are
  unusable. Exit codes are a stable contract: `0` ok, `1` usage, `2` not
  found/ambiguous, `3` not authorized, `4` daemon unreachable, `5` operation
  failed.

### Restore limits

- **Modules are restored at config level only.** A restore rewrites the
  devmgr-owned `modprobe.d` files but never force-unloads a currently loaded
  module — a removed blacklist takes effect on the next boot or hotplug of
  the affected device.
- **Firmware is never rolled back.** fwupd owns firmware; snapshots do not
  capture or restore it.
- **Guard refusals are final.** If convergence would disable a device the
  guard now considers critical (e.g. it hosts the root disk), that item is
  reported as refused and the device stays enabled.

### Authorization (snapshot verbs)

Create/restore/delete require the new polkit action
`org.devmgr.manage-snapshots` (`auth_admin_keep`); `list` is unprivileged.
**When upgrading a source install, re-install the polkit policy** — the file
shipped before Phase 7 does not contain the new action, and every mutating
snapshot verb will be refused until it is updated:

```sh
sudo install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
```

### VM smoke test

`test/vm/phase6-smoke.sh` drives `FwupdUpdateProvider`/`DkmsStatusProvider`
directly (never `fwupdmgr`/`dkms` as a shortcut) against fwupd's built-in
`fwupd-tests` fake-device remote: it enables the test devices, confirms a
`fakedevice` upgrade is visible with a resolvable local cab, installs it
end-to-end (`1.2.2` → `1.2.4`), and — where DKMS headers are available in the
VM — registers a throwaway DKMS module and checks its status through
`DkmsStatusProvider`. `test/vm/phase7-smoke.sh` covers the snapshot engine:
disable → auto snapshot exists → CLI restore re-enables the device, a module
blacklist round-trip through a manual snapshot, undoing a restore via its
safety snapshot, and the CLI's daemon-down exit code. Run the whole rig
(Phase 4 + 5 + 6 + 7) with:

```sh
./test-vm.sh
```

which provisions the disposable VM (now including the `fwupd` and `dkms`
packages), builds the tree with `-DDEVMGR_WITH_SDBUS=ON`, and runs
`test/vm/phase4-smoke.sh` through `test/vm/phase7-smoke.sh` in sequence —
expect `PHASE4 VM SMOKE OK` … `PHASE7 VM SMOKE OK`.

## License

MIT — see [LICENSE](LICENSE). The `.deb` installs it as
`/usr/share/doc/devmgr/copyright`; the tarball carries it at the archive root.
