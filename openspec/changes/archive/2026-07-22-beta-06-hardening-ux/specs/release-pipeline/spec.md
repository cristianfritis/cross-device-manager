# release-pipeline Delta

## MODIFIED Requirements

### Requirement: Tag-triggered release workflow
A GitHub Actions workflow SHALL trigger on tags matching `v*`, run inside the existing Ubuntu build container (plus a Fedora container job for the RPM), and: verify tag == built version, build the release configuration, run the full ctest suite (fail = no artifacts), run `cpack` for DEB, RPM, and TGZ, generate `SHA256SUMS` covering all artifacts, generate the SBOM, sign the artifacts, and produce provenance attestations.

#### Scenario: Test failure blocks release
- **WHEN** the tag build's ctest has any failure
- **THEN** the workflow fails and no release or artifacts are created

### Requirement: Draft prerelease with complete artifact set
The workflow SHALL create a **draft** GitHub release marked prerelease, attaching the deb, the rpm, the tarball, `SHA256SUMS`, the detached signatures, and the SBOM, with the body pre-filled from the release-notes template. Publishing is a manual owner action.

#### Scenario: Draft awaits owner
- **WHEN** the workflow succeeds for a `v0.6.0-beta.*` tag
- **THEN** a draft prerelease exists with the full asset set (packages, checksums, signatures, SBOM) and nothing is publicly visible until the owner publishes

### Requirement: Checksum verifiability
`SHA256SUMS` SHALL be generated from the final artifacts — deb, rpm, tarball, and SBOM — such that `sha256sum -c SHA256SUMS` passes in a directory with the downloaded assets, and the install docs SHALL include that verification step alongside the signature verification.

#### Scenario: User verifies download
- **WHEN** a tester downloads the artifacts plus SHA256SUMS and runs `sha256sum -c`
- **THEN** every line reports OK
