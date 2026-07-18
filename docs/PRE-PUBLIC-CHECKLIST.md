# Pre-public checklist (owner actions)

The beta pipeline stops at a **draft** prerelease on purpose (beta-docs spec):
nothing becomes visible until the owner completes this list, in order. The
pipeline and artifacts must never depend on the repo being public.

1. **Add a LICENSE file.** License choice is the owner's decision; the repo
   must not go public without one.
2. **Secret-scan the full git history** (not just HEAD), e.g.:
   ```sh
   docker run --rm -v "$PWD:/repo" trufflesecurity/trufflehog:latest \
       git file:///repo --only-verified
   ```
   or `gitleaks git .` — resolve anything found (rewrite history or rotate the
   secret) before the flip.
3. **Flip repository visibility to public** (GitHub → Settings → Danger Zone).
4. **Publish the draft prerelease** for the current `v*` tag (verify the three
   assets are attached: `.deb`, `.tar.gz`, `SHA256SUMS`) and mark it
   *prerelease* (the workflow already sets this on the draft).

Notes:

- Order matters: license and secret scan strictly before the visibility flip;
  the publish is last so the first public artifact set is complete.
- Re-runs: a re-tagged build creates a new draft — delete stale drafts so only
  one candidate exists when publishing.
