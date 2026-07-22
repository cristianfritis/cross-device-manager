# release-supply-chain Specification

## Purpose

Supply-chain integrity for releases: signed artifacts with documented verification, a published SBOM, a dependency/license audit, provenance attestations, and a reproducibility check with reported deviations.

## Requirements

### Requirement: Signed release artifacts with documented verification
Every release artifact (deb, rpm, tarball, SHA256SUMS) SHALL be signed in the release workflow, with detached signatures published as release assets. The public key SHALL be published in the repo, and README/BETA-TESTING SHALL document a copy-paste verification procedure that a tester can run before installing.

#### Scenario: Tester verifies a download
- **WHEN** a tester downloads an artifact, its signature, and the published public key, and runs the documented verification command
- **THEN** verification succeeds for authentic artifacts and fails for a tampered artifact

### Requirement: SBOM published per release
The release workflow SHALL generate an SBOM (SPDX or CycloneDX) covering the shipped binaries including statically-linked and vendored dependencies (sdbus-c++, FTXUI, in-tree sha256), attached as a release asset and covered by SHA256SUMS.

#### Scenario: SBOM names the static dependency
- **WHEN** the release SBOM is inspected
- **THEN** it lists sdbus-c++ with its exact version and license, alongside all other shipped dependencies

### Requirement: Dependency and license audit
A recorded audit SHALL confirm every shipped dependency's license is compatible with MIT distribution, stored in the repo docs and refreshed whenever a dependency is added or its version changes in a release.

#### Scenario: Audit gates a new dependency
- **WHEN** a release adds or upgrades a shipped dependency
- **THEN** the audit document is updated for it before the release is published

### Requirement: Provenance attestation for release assets
The release workflow SHALL generate provenance attestations (e.g., GitHub artifact attestation) binding each asset to the workflow run and source commit, verifiable by a downloader with standard tooling.

#### Scenario: Asset provenance verifies
- **WHEN** a downloader runs the documented attestation verification against a release asset
- **THEN** it confirms the asset was built by this repo's release workflow from the tagged commit

### Requirement: Reproducibility check with reported deviations
The pipeline SHALL include a reproducibility check: build the same tag twice in the release container and compare binary checksums. Bit-identical output is not required, but the check SHALL report which files differ and the repo docs SHALL record known, explained sources of deviation.

#### Scenario: Rebuild comparison reported
- **WHEN** the reproducibility job rebuilds a tag
- **THEN** the job output lists matching and differing files, and unexplained differences are treated as a release blocker
