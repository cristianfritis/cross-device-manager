<!-- Release-notes template (beta-docs spec). The release workflow substitutes
     @VERSION@ (e.g. 0.5.0-beta.1) and @TAG@ (e.g. v0.5.0-beta.1) and uses the
     result as the draft release body. Keep the placeholders intact. -->
# devmgr @VERSION@

devmgr manages devices, drivers, firmware, and state snapshots on Linux: a
polkit-gated daemon (D-Bus activated), a Qt 6 GUI, a terminal UI, and a
recovery CLI.

## Highlights (0.5)

- Universal device enable/disable with persistent enforcement across replug
  and daemon restarts, guarded against disabling critical devices.
- Kernel module load/unload/blacklist with Secure Boot & lockdown awareness.
- Firmware visibility and local-cab installs via fwupd; DKMS status.
- **Snapshots & rollback**: automatic pre-mutation snapshots (fail-closed),
  manual snapshots, restore with its own safety snapshot, recovery CLI.

## Install

See the [README Install section](https://github.com/cristianfritis/cross-device-manager/blob/@TAG@/README.md#install-beta).
Ubuntu 22.04/24.04: download the `.deb` below; other distros: the tarball with
its bundled `install.sh`.

**Verify your download** (both artifacts are covered):

```sh
sha256sum -c SHA256SUMS --ignore-missing
```

## Known limitations (beta)

- Module restore is **config-level only** — no force-unload of loaded modules;
  changes apply at next boot/hotplug.
- **No firmware rollback** — snapshots exclude firmware by design.
- Restore can complete *partially converged* when the criticality guard
  refuses an item; refusals are reported, never bypassed.
- Supported: Ubuntu 22.04/24.04 (deb, smoke-tested on 22.04) and
  systemd/OpenRC distros via tarball, x86_64 only. GUI needs a graphical
  session with a polkit agent.
- Statically linked `sdbus-c++ 2.3.1` (Ubuntu ships an incompatible 1.x);
  security updates for it ride devmgr releases during the beta.

## Testing & reporting

Please follow
[BETA-TESTING.md](https://github.com/cristianfritis/cross-device-manager/blob/@TAG@/BETA-TESTING.md)
and file problems with the **Beta bug report** issue template.
