#!/usr/bin/env bash
# Reproducibility check (release-supply-chain spec, task 5.3) — runs INSIDE the
# pkg packaging container (Dockerfile / the pkg compose service). It builds the
# SAME source twice into the SAME build path with a pinned SOURCE_DATE_EPOCH,
# then compares the sha256 of the four shipped binaries.
#
# Building into ONE path (not two side-by-side dirs) is deliberate: it removes
# build-directory-derived nondeterminism, so a surviving diff means a real
# reproducibility problem in the source/toolchain, not just an absolute path
# baked into the binary.
#
# Output lists MATCH/DIFFER per binary. A differing binary whose basename is not
# in KNOWN_DEVIATIONS is an UNEXPLAINED difference and fails the job — a release
# blocker (release-supply-chain spec). Known, explained deviations (and how to
# add one) live in docs/REPRODUCIBILITY.md; every KNOWN_DEVIATIONS entry MUST be
# justified there. Ends with an explicit REPRODUCIBILITY OK when all pass.
set -euo pipefail
cd "$(dirname "$0")/../.."

# Binaries permitted to differ between builds, each justified in
# docs/REPRODUCIBILITY.md. Empty = the build is expected to be fully bit-identical.
KNOWN_DEVIATIONS=()

# Pin the embedded build timestamp so __DATE__/__TIME__ (GCC honors
# SOURCE_DATE_EPOCH) are identical across both builds. The value itself is
# irrelevant to the comparison — only that both builds share it — but the tag
# commit time is the meaningful, machine-independent choice. The release
# workflow passes it in; standalone runs derive it from git or fall back to a
# fixed epoch (1980-01-01, the common SOURCE_DATE_EPOCH floor).
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
  SOURCE_DATE_EPOCH=$(git log -1 --pretty=%ct 2>/dev/null || echo 315532800)
  echo "SOURCE_DATE_EPOCH not provided; using $SOURCE_DATE_EPOCH"
fi
export SOURCE_DATE_EPOCH

BINARIES=(daemon/devmgrd cli/devmgr tui/devmgr-tui gui/devmgr-gui)
BUILD=build/linux-packaged

build_once() {
  local dest=$1
  rm -rf "$BUILD"
  cmake --preset linux-packaged >/dev/null
  cmake --build "$BUILD" -j"$(nproc)"
  mkdir -p "$dest"
  for b in "${BINARIES[@]}"; do
    [ -f "$BUILD/$b" ] || { echo "expected binary missing: $BUILD/$b"; exit 1; }
    cp "$BUILD/$b" "$dest/$(basename "$b")"
  done
}

echo "==> [1/2] first build (SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH)"
build_once /tmp/repro-a
echo "==> [2/2] second build (same path, same epoch)"
build_once /tmp/repro-b

echo "--- reproducibility report ---"
unexplained=()
for b in "${BINARIES[@]}"; do
  name=$(basename "$b")
  a=$(sha256sum "/tmp/repro-a/$name" | cut -d' ' -f1)
  c=$(sha256sum "/tmp/repro-b/$name" | cut -d' ' -f1)
  if [ "$a" = "$c" ]; then
    printf 'MATCH   %-12s %s\n' "$name" "$a"
  else
    known=no
    for k in "${KNOWN_DEVIATIONS[@]:-}"; do [ "$k" = "$name" ] && known=yes; done
    if [ "$known" = yes ]; then
      printf 'DIFFER  %-12s (known deviation) a=%s b=%s\n' "$name" "$a" "$c"
    else
      printf 'DIFFER  %-12s (UNEXPLAINED)     a=%s b=%s\n' "$name" "$a" "$c"
      unexplained+=("$name")
    fi
  fi
done
echo "------------------------------"

if [ "${#unexplained[@]}" -gt 0 ]; then
  echo "unexplained reproducibility differences: ${unexplained[*]}" >&2
  echo "fix the source of nondeterminism, or record it in docs/REPRODUCIBILITY.md and add it to KNOWN_DEVIATIONS." >&2
  exit 1
fi

echo "REPRODUCIBILITY OK"
