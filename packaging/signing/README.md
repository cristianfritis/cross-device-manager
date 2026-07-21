# Release signing (minisign)

devmgr release assets are signed two independent ways (beta-06 task 5.2):

1. **minisign** detached signatures (`*.minisig`) — a tester verifies each asset
   against the committed public key `packaging/signing/devmgr.pub` with a single
   command, no accounts or keyservers.
2. **GitHub build provenance attestation** — binds each asset to the workflow run
   and source commit, verifiable with `gh attestation verify`. Independent of the
   minisign key, so a lost/rotated key never invalidates provenance.

Tester-facing verification commands live in `README.md` (§Verifying a download)
and `BETA-TESTING.md`. This file is the **owner/maintainer** side.

## One-time setup (owner) — required before the first signed release

The release workflow's signing step **fails on purpose** until this is done, so a
release can never ship unsigned.

1. Generate a key pair (pick a strong password; store it in a password manager):
   ```sh
   minisign -G -p devmgr.pub -s devmgr.key
   ```
2. Commit the **public** key, overwriting the placeholder:
   ```sh
   cp devmgr.pub packaging/signing/devmgr.pub
   git add packaging/signing/devmgr.pub && git commit -m "chore(signing): add release public key"
   ```
   The committed file must no longer contain the `REPLACE_ME` sentinel — the
   workflow checks for it and refuses to sign otherwise.
3. Add the **secret** key and its password as GitHub Actions secrets on the repo:
   - `MINISIGN_SECRET_KEY` — the entire contents of `devmgr.key` (two lines).
   - `MINISIGN_PASSWORD` — the password chosen in step 1. Leave unset only if the
     key was generated passwordless (`minisign -G -W`).

   ```sh
   gh secret set MINISIGN_SECRET_KEY < devmgr.key
   gh secret set MINISIGN_PASSWORD    # paste the password
   ```
4. Keep `devmgr.key` **out of the repo** (it is git-ignored under this dir as a
   safety net). The only copies should be your password manager and the GH secret.

## What the workflow signs

Every file in `dist/` that a tester downloads — currently the `.deb`, the
`.tar.gz`, `SHA256SUMS`, and `SBOM.spdx.json`; the `.rpm` joins the set when task
5.4 wires it into the release job. Each gets a detached `<file>.minisig`. The
workflow then self-verifies every signature against the committed public key, so
a mismatched key or a corrupt signature blocks the release.

## Key rotation

1. Generate a new pair; commit the new `devmgr.pub`; update the two secrets.
2. Note the rotation date and the retired key id in this file's history (git).
3. Past releases keep verifying against the public key committed *at their tag*;
   the provenance attestation path verifies independently of any key, so
   downloaders of old assets are never stranded by a rotation.
