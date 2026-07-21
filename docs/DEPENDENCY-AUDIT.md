# devmgr dependency & license audit

Every dependency that ships in a devmgr release artifact, its version, license,
and how it is linked — and the verdict on whether that license is compatible
with distributing devmgr under **MIT**.

This is the license side of the release SBOM. Its vendored/static rows are
mirrored, machine-readable, in `packaging/sbom/overlay.spdx.json`; the release
workflow merges that overlay into syft's output (`packaging/sbom/merge-sbom.py`)
so the published `SBOM.spdx.json` names every dependency below. **Refresh policy:
** whenever a shipped dependency is added, removed, or version-bumped, update
this table *and* the overlay in the same change, before the release that ships
it. gtest is the one dependency deliberately excluded — it is test-only and
never linked into a shipped binary.

Audited at beta-06 (task 5.1; `devmgr-fwupd-smoke` added task 6.1). Shipped
binaries: `devmgrd`, `devmgr` (CLI), `devmgr-gui`, `devmgr-tui`, and
`devmgr-fwupd-smoke` — the headless firmware-acceptance diagnostic. The latter
links only `devmgr_core` + the Linux PAL (sdbus-c++, nlohmann-json, tl-expected,
in-tree sha256), so it introduces **no new dependency**.

## Shipped dependencies

| Dependency | Version | License | Linkage | In binary | MIT-compatible? |
|---|---|---|---|---|---|
| sdbus-c++ | 2.3.1 | LGPL-2.1-or-later | static | devmgrd, devmgr, devmgr-fwupd-smoke | Yes — see LGPL note below |
| ftxui | 6.1.9 | MIT | static | devmgr-tui | Yes (permissive) |
| spdlog | 1.17.0 | MIT | static | devmgrd | Yes (permissive) |
| fmt | 12.1.0 | MIT | static (via spdlog) | devmgrd | Yes (permissive) |
| nlohmann-json | 3.12.0 | MIT | header-only | devmgrd, devmgr, devmgr-fwupd-smoke | Yes (permissive) |
| tl-expected | 1.3.1 | CC0-1.0 | header-only | all | Yes (public domain) |
| devmgr-sha256 (in-tree) | — | MIT | in-tree source | daemon + core | Yes (own code) |
| Qt 6 (Widgets, Gui, Core) | distro-provided | LGPL-3.0-only | dynamic | devmgr-gui | Yes — dynamic LGPL, no relink obligation |

Versions: sdbus-c++ is pinned by the release containers (`Dockerfile`,
`Dockerfile.fedora`, `--branch v2.3.1`); the vcpkg rows track
`vcpkg_installed/x64-linux/share/<pkg>/vcpkg.spdx.json` as resolved by
`vcpkg.json`. Qt is supplied by the target distribution (Ubuntu 22.04 build
host ships Qt 6.2.x; Fedora 42 ships Qt 6.9.x) and linked dynamically, so its
exact version is the installed system's, not something devmgr vendors.

## System libraries (dynamic, distro-provided)

Linked dynamically from the target distribution; not vendored, not redistributed
by devmgr. Listed for completeness — none imposes a copyleft obligation on the
MIT application because each is dynamically linked and shipped by the OS.

| Library | Typical license | Used by |
|---|---|---|
| glibc (libc, libm, libpthread, …) | LGPL-2.1-or-later (+ exceptions) | all binaries |
| libstdc++ | GPL-3.0-or-later WITH GCC-exception | all binaries |
| libudev / libsystemd | LGPL-2.1-or-later | devmgrd (enumeration, hotplug) |
| libkmod | LGPL-2.1-or-later | devmgrd (module load/unload) |
| libGL / Mesa | MIT | devmgr-gui (Qt platform) |
| polkit | LGPL-2.0-or-later | runtime authorization (IPC, not linked) |

## LGPL static-link note (sdbus-c++)

sdbus-c++ is **LGPL-2.1-or-later** and is **statically** linked into `devmgrd`
and `devmgr`. LGPL §6 permits combining the library into a work under other
terms (here, MIT) provided recipients can relink the combined work against a
modified library. devmgr satisfies this because:

1. The full application source is published under MIT (public repo at release).
2. The build is reproducible from a pinned recipe: `Dockerfile` /
   `Dockerfile.fedora` clone sdbus-c++ at tag `v2.3.1` and build it static; a
   recipient can substitute a modified sdbus-c++ and rebuild the exact binaries.
3. The SBOM records sdbus-c++'s exact version, upstream, and license, so the
   library is identifiable for relinking.

No separate written offer is required while the source and build recipe are
public. If the project ever ships a binary **without** publishing the
corresponding source and recipe, an LGPL §6 written offer (or a switch to
dynamic linking) becomes mandatory — flag that here before it happens.

Qt 6 is **LGPL-3.0** but **dynamically** linked, which carries no relink
obligation beyond allowing the user to swap the system Qt — already true of any
distro-provided shared library.

## Verdict

All shipped dependencies are compatible with distributing devmgr under MIT. The
only copyleft obligation is sdbus-c++'s LGPL static-link relink requirement,
satisfied by publishing MIT source + a pinned, reproducible build recipe. No
GPL code is linked into any shipped binary (libstdc++ carries the GCC runtime
exception; gtest — BSD-3-Clause — is test-only and excluded from all artifacts).
