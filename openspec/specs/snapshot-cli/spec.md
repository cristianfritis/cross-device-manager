# snapshot-cli Specification

## Purpose

Minimal `devmgr` recovery CLI: a UI-free D-Bus client exposing snapshot list/create/restore/delete for bare-console recovery when the GUI/TUI are unusable.

## Requirements

### Requirement: Minimal recovery CLI
A new binary `devmgr` SHALL provide `devmgr snapshot list`, `devmgr snapshot create [--label <text>]`, `devmgr snapshot restore <id>`, and `devmgr snapshot delete <id>`. It SHALL be a thin D-Bus client over the existing privileged-channel code with zero UI dependencies (no FTXUI, no Qt), so it works on a bare console when the UIs are unusable. Ids MAY be abbreviated to any unique prefix.

#### Scenario: Restore from a broken-GUI session
- **WHEN** `devmgr snapshot restore a1b2c3` is run on a console and polkit authorizes
- **THEN** the restore executes and the per-item outcome summary prints to stdout

#### Scenario: Ambiguous prefix
- **WHEN** the given id prefix matches two snapshots
- **THEN** the command fails listing the matching ids and changes nothing

### Requirement: Exit codes and output
Exit codes SHALL be: 0 success, 1 usage error, 2 not found / ambiguous, 3 not authorized, 4 daemon unreachable, 5 operation failed. `list` SHALL print one row per snapshot (short id, date, trigger, reason) and support `--json` for the raw metadata. Errors print to stderr, one line, no stack traces.

#### Scenario: Daemon down
- **WHEN** any snapshot verb runs while devmgrd is not reachable
- **THEN** the command exits 4 with a one-line stderr message naming the bus error

### Requirement: Build gating
The CLI target SHALL be gated on `DEVMGR_WITH_SDBUS` exactly like the daemon (absent from the nosdbus configuration) and included in the standard presets, tests, and format/tidy gates.

#### Scenario: nosdbus build
- **WHEN** the project is configured with `-DDEVMGR_WITH_SDBUS=OFF`
- **THEN** the build succeeds and no `devmgr` CLI target exists

### Requirement: History, diff, and restore preview commands
The CLI SHALL add read-only commands: `devmgr snapshot history` (parent-chain listing, newest first, HEAD and last-good marked), `devmgr snapshot diff <a> [<b>]` (per-entry diff; `<b>` omitted means current live state), and `devmgr snapshot restore --preview <id>` (prints the restore preview — the diff against live state plus the partial-convergence note — and exits without restoring). The default `restore` behavior SHALL remain unchanged and non-interactive so scripted recovery keeps working. Id prefixes and `--json` apply as in the existing commands; exit codes follow the existing table.

#### Scenario: Preview without side effects
- **WHEN** `devmgr snapshot restore --preview a1b2c3` runs
- **THEN** the pending changes print to stdout, nothing is restored, and the exit code is 0

#### Scenario: Diff two snapshots
- **WHEN** `devmgr snapshot diff a1b2c3 d4e5f6` runs with both prefixes unique
- **THEN** the per-entry differences print, or an explicit no-differences line when payloads match
