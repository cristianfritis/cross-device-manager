# beta-docs Specification

## Purpose
TBD - created by archiving change beta-05-packaging. Update Purpose after archive.
## Requirements
### Requirement: Three-step install documentation
The README SHALL contain an Install section for the beta with at most three user steps on Ubuntu (download assets, verify checksums + `sudo apt install ./devmgr_*.deb`, launch `devmgr-gui` or `devmgr-tui`), plus the tarball path for other distros, and the polkit-policy re-install note for source upgrades.

#### Scenario: New user installs from README alone
- **WHEN** a tester follows only the README Install section on Ubuntu 22.04
- **THEN** they reach a running UI listing their devices without consulting any other document

### Requirement: Beta test guide
A `BETA-TESTING.md` SHALL define the tester loop: scenarios to exercise (enumerate + hotplug; disable/enable a spare device; module blacklist; firmware update check; manual snapshot; restore round-trip after each mutation), what "expected" looks like per scenario, explicit warnings (spare devices only; module config-level restore limit; no firmware rollback), how to collect logs, and how to file an issue (template reference).

#### Scenario: Tester exercises rollback
- **WHEN** a tester follows the disable-then-restore scenario
- **THEN** the guide's expected-result text matches what the UIs display at each step

### Requirement: Release notes template
A release-notes template SHALL exist in the repo (used by the pipeline to pre-fill the draft): feature summary for 0.5, install steps link, known limitations (restore limits, supported distros, beta caveats), checksum verification, and issue-reporting link.

#### Scenario: Draft body is complete
- **WHEN** the release workflow creates the draft
- **THEN** its body contains all template sections with version fields substituted

### Requirement: Public-flip prerequisites are owner-gated
The docs SHALL record the pre-public checklist as owner actions: add LICENSE (owner's license choice), scan git history for secrets, flip visibility, publish the draft. The pipeline and artifacts MUST NOT auto-publish or depend on the repo being public.

#### Scenario: Everything ready while still private
- **WHEN** all beta-05-packaging tasks are complete but the repo is private
- **THEN** artifacts, draft release, and docs are all in place, waiting only on the owner checklist

