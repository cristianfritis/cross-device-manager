# release-pipeline Specification

## Purpose
TBD - created by archiving change beta-05-packaging. Update Purpose after archive.
## Requirements
### Requirement: Tag-triggered release workflow
A GitHub Actions workflow SHALL trigger on tags matching `v*`, run inside the existing Ubuntu build container, and: verify tag == built version, build the release configuration, run the full ctest suite (fail = no artifacts), run `cpack` for DEB and TGZ, and generate `SHA256SUMS` covering both artifacts.

#### Scenario: Test failure blocks release
- **WHEN** the tag build's ctest has any failure
- **THEN** the workflow fails and no release or artifacts are created

### Requirement: Draft prerelease with complete artifact set
The workflow SHALL create a **draft** GitHub release marked prerelease, attaching the deb, the tarball, and `SHA256SUMS`, with the body pre-filled from the release-notes template. Publishing is a manual owner action (paired with flipping the repo public for the first beta).

#### Scenario: Draft awaits owner
- **WHEN** the workflow succeeds for `v0.5.0-beta.1`
- **THEN** a draft prerelease exists with 3 assets and nothing is publicly visible until the owner publishes

### Requirement: Checksum verifiability
`SHA256SUMS` SHALL be generated from the final artifacts such that `sha256sum -c SHA256SUMS` passes in a directory with the downloaded assets, and the install docs SHALL include that verification step.

#### Scenario: User verifies download
- **WHEN** a tester downloads both artifacts plus SHA256SUMS and runs `sha256sum -c`
- **THEN** both lines report OK

