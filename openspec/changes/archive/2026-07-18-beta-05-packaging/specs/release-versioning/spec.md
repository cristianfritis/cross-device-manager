# release-versioning

## ADDED Requirements

### Requirement: Single-source version
The project version SHALL be declared once in the root `CMakeLists.txt` (`project(devmgr VERSION 0.5.0)`) and propagated via a configure-generated `version.hpp` (version string, major/minor/patch, plus a prerelease suffix cache variable defaulting to `beta.1` for this cycle). No other file may hard-code the version.

#### Scenario: One edit bumps everywhere
- **WHEN** the CMake version is changed and the project rebuilt
- **THEN** all binaries, package metadata, and `--version` output reflect the new version with no other edits

### Requirement: --version on every binary
`devmgrd`, `devmgr-tui`, `devmgr-gui`, and `devmgr` SHALL each accept `--version`, print `<name> <semver>[-<prerelease>]` to stdout, and exit 0 without touching D-Bus, the display, or the terminal alternate screen.

#### Scenario: Version without side effects
- **WHEN** `devmgr-tui --version` runs on a console with no daemon running
- **THEN** it prints the version line and exits 0 without entering the TUI

### Requirement: Prerelease tag scheme
Beta releases SHALL be tagged `v<version>-beta.<n>` (first: `v0.5.0-beta.1`), semver-ordered, and marked as prereleases on GitHub. The tag SHALL match the built version string; the release pipeline MUST fail on mismatch.

#### Scenario: Tag/version mismatch rejected
- **WHEN** tag `v0.5.0-beta.2` is pushed while CMake still says beta.1
- **THEN** the release workflow fails before producing artifacts
