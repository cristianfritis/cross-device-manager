# snapshot-cli Delta

## ADDED Requirements

### Requirement: History, diff, and restore preview commands
The CLI SHALL add read-only commands: `devmgr snapshot history` (parent-chain listing, newest first, HEAD and last-good marked), `devmgr snapshot diff <a> [<b>]` (per-entry diff; `<b>` omitted means current live state), and `devmgr snapshot restore --preview <id>` (prints the restore preview — the diff against live state plus the partial-convergence note — and exits without restoring). The default `restore` behavior SHALL remain unchanged and non-interactive so scripted recovery keeps working. Id prefixes and `--json` apply as in the existing commands; exit codes follow the existing table.

#### Scenario: Preview without side effects
- **WHEN** `devmgr snapshot restore --preview a1b2c3` runs
- **THEN** the pending changes print to stdout, nothing is restored, and the exit code is 0

#### Scenario: Diff two snapshots
- **WHEN** `devmgr snapshot diff a1b2c3 d4e5f6` runs with both prefixes unique
- **THEN** the per-entry differences print, or an explicit no-differences line when payloads match
