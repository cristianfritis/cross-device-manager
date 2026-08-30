# Build reproducibility

The release pipeline builds each tag **twice** in the packaging container and
compares the five shipped binaries. This is a defense against a compromised or
drifting build environment: if the same source and toolchain stop producing the
same bytes, either something nondeterministic crept into the build or the
environment changed underneath it — both worth knowing before a release ships.

Bit-identical output is the goal but **not a hard requirement**. What *is*
required: every difference is either eliminated or **explained and recorded
here**. An unexplained difference is a release blocker.

## What the check does

`test/container/reproducibility-check.sh` (run by the `reproducibility` job in
`.github/workflows/release.yml`):

1. Builds `linux-packaged` (Release) into `build/linux-packaged`, saves the four
   binaries, then **removes that build dir and rebuilds into the same path**.
   Using one path for both builds removes build-directory-derived noise (an
   absolute path baked into a binary), so a surviving diff is a real problem.
2. Compares the `sha256` of `devmgrd`, `devmgr`, `devmgr-tui`, `devmgr-gui`, and
   `devmgr-fwupd-smoke` (the shipped firmware-acceptance diagnostic).
3. Prints a `MATCH`/`DIFFER` line per binary and ends `REPRODUCIBILITY OK`, or
   exits non-zero listing any **UNEXPLAINED** differing binary.

### Scope

The check compares the **binaries**, not the `.deb`/`.rpm`/`.tar.gz` archives.
Archive containers embed their own timestamps and member ordering (ar/tar/gzip
metadata); the binaries are the meaningful reproducibility target, and they are
what a downstream consumer ultimately runs. Archive-level reproducibility is out
of scope for this gate.

## Windows build inputs

Windows produces **no release artifact** — no installer, no package, no signed
binary — so the two-build byte-comparison above does not apply to it. What does
apply is the rule underneath that check: a build must be re-creatable from
**declared inputs**, never from whatever happens to be on a machine.

Every input to the Windows build is declared:

| Input | Declared where | Value |
|---|---|---|
| Qt | `.github/workflows/ci.yml` | `6.10.3`, `win64_msvc2022_64`, provisioned by `jurplel/install-qt-action@v4` from the official Qt distribution |
| vcpkg | `.github/workflows/ci.yml` | pinned commit `a34a3811fce990f9d2809cf0356dd443143c7000` |
| vcpkg packages | `vcpkg.json` | `gtest`, `spdlog`, `nlohmann-json`, `tl-expected` (`ftxui` is Linux-only) |
| Triplet | `CMakePresets.json` (`windows-debug`) | `x64-windows` |
| SDK floor | `platform/windows/CMakeLists.txt` | `NTDDI_VERSION` ≥ `0x0A000001`; configuring lower fails |
| Toolchain | `.github/workflows/ci.yml` | `windows-latest` runner image, MSVC v143 |

Nothing in that list is resolved by inspecting an installed artifact. In
particular **no step reads a pre-existing local Qt**: the version above is the
definition of what the build depends on, and a developer's machine is checked
*against* it (`docs/WINDOWS-DEVELOPMENT.md`) rather than being consulted for it.
Windows has no system Qt for the Linux system-Qt rule to point at, which is
exactly why the rule inverts here.

Two boundaries worth stating rather than leaving to be discovered. The
`windows-latest` runner image floats — it is a declared input, but not a pinned
one, and MSVC moves with it. And the Linux container clones vcpkg at image-build
time rather than at a pinned commit, so the Linux and Windows jobs do not
necessarily resolve the same package versions; the Linux release binaries are
covered by the two-build comparison above instead.

## SOURCE_DATE_EPOCH

Both builds run with the same `SOURCE_DATE_EPOCH` so any embedded build
timestamp (`__DATE__`/`__TIME__`, which GCC pins to this value) is identical
across them. The release workflow sets it to the **tagged commit's** time
(`git log -1 --pretty=%ct`); a standalone run of the script derives it from git
or falls back to `315532800` (1980-01-01). Its exact value does not affect the
comparison — only that both builds share it.

## Known, explained deviations

None. The build is currently expected to be fully bit-identical, so
`KNOWN_DEVIATIONS` in the check script is empty.

| Binary | Reason it differs | Recorded |
|--------|-------------------|----------|
| _(none)_ | — | — |

### Adding a deviation

Only when a difference is understood and genuinely cannot be removed:

1. Explain the root cause in the table above (what varies and why it is benign).
2. Add the binary's basename to `KNOWN_DEVIATIONS` in
   `test/container/reproducibility-check.sh`, referencing this file.

The script reports known deviations but does not fail on them; it fails only on
differences that are **not** listed — those must be investigated, not
allow-listed away, before the release is published.

## Running it locally

```sh
docker compose -f test/docker-compose.yml build pkg
docker compose -f test/docker-compose.yml run --rm \
  -e SOURCE_DATE_EPOCH="$(git log -1 --pretty=%ct)" \
  pkg bash test/container/reproducibility-check.sh
```
