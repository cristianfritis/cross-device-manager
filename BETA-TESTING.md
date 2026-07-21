# devmgr beta testing guide

Thanks for testing the v0.5.0 beta. This guide walks a loop of scenarios that
exercises the whole stack — device control, modules, firmware visibility, and
the snapshot/rollback safety net — and tells you what "working" looks like at
each step, plus how to report anything that doesn't match.

## Before you start — read these warnings

- **Use spare devices only.** Disable/enable and driver operations should be
  pointed at hardware you can afford to lose for a session (a spare USB stick,
  mouse, or similar) — never your only keyboard, your network card mid-SSH, or
  a disk controller. devmgr's guard refuses the most dangerous cases (root
  filesystem, only keyboard/pointer), but it cannot know everything about your
  setup.
- **Module restore is config-level only.** Restoring a snapshot rewrites
  devmgr's `modprobe.d` files but never force-unloads a loaded module; a
  removed blacklist takes effect on the next boot or device hotplug.
- **Firmware is never rolled back.** Snapshots do not capture firmware; a
  firmware update is permanent as far as devmgr is concerned.
- Run the GUI/TUI from a **graphical session** — the polkit password prompt
  needs a desktop agent. The first privileged action prompts; the grant is
  cached for ~5 minutes.

## Verify before installing

The release assets are signed and carry a provenance attestation. Before
installing, verify the download — the copy-paste commands (minisign signature +
`gh attestation verify`) are in the README under
[Verifying a download](README.md#verifying-a-download). A failed signature check
means: do not install, and please report it.

## The test loop

Run these in order; each mutation is followed by a snapshot-restore round-trip
so you also exercise the safety net. Use `devmgr-gui` or `devmgr-tui`
(TUI tabs cycle Devices → Modules → Updates → Snapshots).

### 1. Enumerate + hotplug

Open the Devices tab. **Expected:** your USB/PCI devices are listed with
name, bus, and status. Unplug and replug a spare USB device. **Expected:** the
row disappears and reappears within a couple of seconds, no refresh needed —
`devmgrd` was started on demand by the first UI launch (no manual daemon
setup).

### 2. Disable + restore a spare device

Select a **spare** USB device and disable it (TUI: `e`, confirm `y`; GUI:
Disable action + dialog). **Expected:** a polkit prompt, then the device shows
disabled; unplug/replug keeps it disabled (enforcement). Now open the
Snapshots tab. **Expected:** an auto snapshot from just before the disable is
listed. Restore that snapshot (TUI: `r`; GUI: Restore). **Expected:** the
restore reports success (a safety snapshot of "now" is created first) and the
device is enabled again.

### 3. Module blacklist + restore

On the Modules tab, pick a module that backs no critical device (the guard
will refuse otherwise — that refusal, with a reason, is itself a pass).
Blacklist/unload it, then restore the pre-mutation auto snapshot from the
Snapshots tab. **Expected:** devmgr's `modprobe.d` blacklist file is removed
again (`ls /etc/modprobe.d/devmgr-*.conf`), remembering the config-level
limit above — the module's *loaded* state does not change until reboot or
hotplug.

### 4. Firmware update check

Open the Updates tab. **Expected:** firmware-updatable devices appear with
current versions (banner shows the fwupd version and Secure Boot state);
DKMS rows, if any, are status-only. A release whose file is only available
online shows install disabled with "external download required — run
`fwupdmgr update`". Only install an update if you would have installed it
anyway — **there is no rollback** (see warnings).

### 5. Manual snapshot + CLI round-trip

Create a manual snapshot with a label (Snapshots tab: TUI `s` / GUI Create),
then from any terminal:

```sh
devmgr snapshot list
```

**Expected:** your labeled snapshot is listed (manual snapshots are never
auto-pruned; autos keep the newest 20). Make any small change (e.g. disable
the spare device again), then:

```sh
devmgr snapshot restore <id-prefix>
```

**Expected:** restore succeeds by unique id prefix and re-enables the device.
Exit codes are a contract: `0` ok, `1` usage, `2` not found/ambiguous,
`3` not authorized, `4` daemon unreachable, `5` failed.

## Collecting logs

Attach these to any issue:

- Versions: `devmgrd --version; devmgr --version` plus your distro and kernel
  (`uname -a`).
- Daemon log: `journalctl -u devmgrd.service -b` (deb/systemd) or
  `/var/log/devmgrd.log` (tarball/OpenRC).
- Snapshot state, if snapshot-related: `devmgr snapshot list --json`.
- For UI issues: the exact tab/keys or menu actions, and what the status
  line/status bar said.

## Filing an issue

Use the **Beta bug report** template on GitHub
(`.github/ISSUE_TEMPLATE/beta-bug-report.md` — preselected when you open a new
issue). One issue per problem, logs attached, and please note which scenario
number above you were running.
