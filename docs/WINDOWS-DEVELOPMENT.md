# Building and running devmgr on Windows

Windows gets a **read-only** build: the GUI (`devmgr-gui`) and the CLI
(`devmgr`), backed by a platform layer that enumerates devices, watches hotplug,
and reports system information, and implements nothing that changes a device.
What that means for a user is in
[COMPATIBILITY-POLICY.md](COMPATIBILITY-POLICY.md#0-supported-platforms); this
document is for building it.

There is **no installer, no packaging, and no code signing** for Windows. The
GUI runs from the build tree. Producing an unsigned artifact that users might
install is worse than producing none, and Linux packaging is a whole capability
(signing, reproducibility, distro policy) that a half-version would not honour.

## Prerequisites

| Input | Version | Where it comes from |
|---|---|---|
| Windows SDK target | Windows 10 version 1607 (`NTDDI_VERSION` `0x0A000001`) or later | `platform/windows/CMakeLists.txt` refuses to configure below it |
| Visual Studio | 2022 (MSVC v143), x64 | Local install |
| Qt | **6.8.3**, `win64_msvc2022_64` | Official Qt online installer / `aqtinstall`; see below |
| vcpkg | commit `a34a3811fce990f9d2809cf0356dd443143c7000` | `github.com/microsoft/vcpkg` |
| vcpkg triplet | `x64-windows` | Set by the `windows-debug` CMake preset |

## The pinned Qt version

**CI owns the Qt version. Your machine is checked against it, never the other
way round.**

Linux consumes the distribution's Qt because a distribution ships a coherent,
patched "system Qt" that packages link against. Windows has no such thing: no
distribution, no system package, and every developer's install is an
independently chosen version at an arbitrary path. Taking whatever happens to be
installed would make the build depend on an undeclared local artifact — the
opposite of what the Linux rule protects.

So the version is pinned in `.github/workflows/ci.yml`, provisioned there by
`jurplel/install-qt-action@v4` from the official Qt distribution:

```yaml
version: '6.8.3'
host: windows
target: desktop
arch: win64_msvc2022_64
```

Qt **6.8.3** is a long-term-support release and is the minimum-supporting choice
for the GUI's Windows 10 1809 floor — Qt 6 officially supports Windows 10 1809
and later, which is why the GUI floor is 1809 while the backend and CLI floor is
1607.

### Checking that your local Qt matches

The owner's machine is the **acceptance gate** — it verifies behaviour that CI
cannot — and it is expected to run the same Qt CI pins. That is a checkable
statement, not a source of truth. To check it:

```powershell
qmake -query QT_VERSION
```

or, if `qmake` is not on `PATH`, from the Qt install directory:

```powershell
& "C:\Qt\6.8.3\msvc2022_64\bin\qmake.exe" -query QT_VERSION
```

It must print `6.8.3`. If it does not, install that version rather than changing
the pin — and if the pin genuinely needs to move, move it in the workflow first,
then match locally.

## Configuring and building

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --test-dir build/windows-debug -C Debug --output-on-failure
```

The `windows-debug` preset selects `DEVMGR_PLATFORM=windows` (which is also the
auto-detected default on Windows) and the `x64-windows` vcpkg triplet. `ftxui` is
platform-qualified to Linux in `vcpkg.json`, because the TUI is a Linux surface.

## Running the GUI from the build tree

Qt's DLLs are not next to the executable after a build, so a freshly built
`devmgr-gui.exe` will not start. `windeployqt` copies what it needs into the
output directory:

```powershell
& "C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe" --debug build\windows-debug\gui\Debug\devmgr-gui.exe
build\windows-debug\gui\Debug\devmgr-gui.exe
```

Use `--release` instead of `--debug` for a Release build; mixing the two produces
a program that fails to start with a missing-DLL or mismatched-runtime error.
Re-run `windeployqt` after changing Qt versions, not after every rebuild.

The CLI needs no deployment step:

```powershell
build\windows-debug\cli\Debug\devmgr.exe devices list
build\windows-debug\cli\Debug\devmgr.exe devices show <id> --json
```

## What CI does and does not cover

The `windows-build-and-test` job compiles `core`, `app`, `platform/windows`,
`gui`, and `cli` and runs the unit tests. A hosted runner has a synthetic device
set, no physical USB, and no hotplug, so a green job means **the code builds and
its logic is correct** — never that it works on a real machine. Device
behaviour, hotplug, and the shutdown-under-plug-pressure hazard are verified by
the owner-gated manual smoke, not by CI.

## Where the platform code lives

`platform/windows/` splits deliberately:

- `windows_device_mapper.cpp` and `windows_hotplug_monitor.cpp` are
  **platform-neutral** — no `<windows.h>`, no native constant. Every decision
  lives there (property → model, identifier prefix → bus, when a callback may
  fire), so it is compiled and unit-tested on Linux CI too, from captured
  property fixtures and a fake notification source.
- `cfgmgr_*.cpp`, `windows_system_info.cpp`, and `platform_backends_windows.cpp`
  are the Win32 half: calls and constants only. Every native property key this
  project knows is in those files, and a CTest check
  (`scripts/check-native-keys.cmake`) fails the build if one appears anywhere
  else.
