<!-- Release-notes template (beta-docs spec). The release workflow substitutes
     @VERSION@ (e.g. 0.5.0-beta.1) and @TAG@ (e.g. v0.5.0-beta.1) and uses the
     result as the draft release body. Keep the placeholders intact. -->
# devmgr @VERSION@

devmgr manages devices, drivers, firmware, and state snapshots on Linux: a
polkit-gated daemon (D-Bus activated), a Qt 6 GUI, a terminal UI, and a
recovery CLI.

## Highlights (0.6)

- **Snapshot history & diff**: browse the snapshot chain (HEAD / last-good
  markers), diff any two snapshots or against live state, and preview a restore
  before applying it — across the GUI, TUI, and `devmgr snapshot` CLI.
- **Accessibility pass**: GUI accessible names, keyboard shortcuts, tab order,
  a minimum window size, and full-value detail for elided rows; TUI minimum-size
  guard; shared loading/empty/error wording across UIs.
- **Fedora RPM packages** alongside the Debian/Ubuntu `.deb` and the portable
  tarball, plus documented upgrade/removal behavior for every format.
- **Hardened supply chain**: an SPDX SBOM, minisign signatures, and GitHub
  provenance attestation on every artifact, a reproducibility double-build check,
  and a dependency/license audit in the repo.
- Daemon hardening: a central IPC validation layer and a tightened systemd unit.

Carried from 0.5: device enable/disable with persistent enforcement, kernel
module load/unload/blacklist (Secure Boot & lockdown aware), fwupd firmware +
DKMS status, and fail-closed automatic snapshots with rollback.

## Install

See the [README Install section](https://github.com/cristianfritis/cross-device-manager/blob/@TAG@/README.md#install-beta).
Ubuntu 22.04/24.04: download the `.deb`; Fedora: the `.rpm`; other distros: the
tarball with its bundled `install.sh`.

**Verify your download.** Every artifact below is covered by `SHA256SUMS`, which
is itself signed and attested. Full step-by-step instructions are in the
[README](https://github.com/cristianfritis/cross-device-manager/blob/@TAG@/README.md#verifying-a-download);
the quick path:

```sh
# 1. checksums — covers deb, rpm, tarball, and the SBOM
sha256sum -c SHA256SUMS --ignore-missing
# 2. minisign signature on the checksum list (public key committed in the repo)
minisign -Vm SHA256SUMS -p devmgr.pub
# 3. provenance — confirms this repo's release workflow built the asset
gh attestation verify <artifact> --repo cristianfritis/cross-device-manager
```

An SPDX SBOM (`SBOM.spdx.json`) enumerating every shipped and vendored
dependency is attached and covered by `SHA256SUMS`.

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
- **Windows is read-only and unpackaged.** Devices are listed with detail and
  hotplug is live; nothing can be changed, and there is no installer — the GUI
  runs from the build tree. GUI needs Windows 10 1809+ (Qt's floor); the CLI
  works from Windows 10 1607.

### What the Windows CI job verifies — and what it does not

The `windows-build-and-test` job covers **compilation and unit tests only**. A
hosted runner has a synthetic device set, so a green Windows job is not evidence
that the build works on real hardware. It does **not** cover:

- that enumerated devices match what Device Manager reports for the same machine
  (names, vendors, buses, detail rows);
- device arrival and removal — no physical device is ever plugged or unplugged;
- shutdown while devices are being plugged and unplugged, which is the one
  interleaving where the notification API can hang or use freed state;
- that unreported properties omit their detail rows rather than showing blanks;
- that the Devices toolbar shows only `Refresh`, with no disabled mutating verb
  and no orphan separator;
- that a device in a problem state renders through the shared status colour and
  word.

Those are verified by the owner-gated manual smoke on real Windows before a
release claims Windows support, and the Windows build number tested is recorded
with the result.

## Testing & reporting

Please follow
[BETA-TESTING.md](https://github.com/cristianfritis/cross-device-manager/blob/@TAG@/BETA-TESTING.md)
and file problems with the **Beta bug report** issue template.
