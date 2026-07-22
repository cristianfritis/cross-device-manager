#!/usr/bin/env bash
# Local pre-image of the CI clang-format gate (.github/workflows/ci.yml). CI is
# the source of truth: clang-format-18 (ubuntu:24.04's default) over the C++
# tree, `--dry-run --Werror`. This script mirrors that gate so a violation is
# caught before it reaches CI.
#
#   scripts/check-format.sh             # check the whole tree (host clang-format)
#   scripts/check-format.sh --fix       # reformat the whole tree in place
#   scripts/check-format.sh --staged    # check only staged files (pre-commit hook)
#   scripts/check-format.sh --container # check via the container == exact CI (18)
#
# The host runner uses whatever clang-format is on PATH (prefers clang-format-18).
# On a machine whose clang-format is NOT 18 (e.g. Arch/CachyOS ships the latest),
# host output can differ from CI on trailing-comment alignment and `T x{}`
# spacing — the two features the CI pin exists for. Use --container for a
# byte-identical result. Exit non-zero on any violation.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Same directory set as the CI `find`.
DIRS=(core tests app platform tui gui daemon cli)

mode=check    # check | fix
scope=tree    # tree | staged
runner=host   # host | container

for arg in "$@"; do
    case "$arg" in
        --fix) mode=fix ;;
        --staged) scope=staged ;;
        --container) runner=container ;;
        -h | --help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "check-format: unknown arg '$arg'" >&2
            exit 2
            ;;
    esac
done

# --- exact CI parity: run inside the container (clang-format 18 on noble) -----
if [ "$runner" = container ]; then
    echo "check-format: running exact CI parity in the container (clang-format-18)…" >&2

    compose_override="$(mktemp)"
    trap 'rm -f "$compose_override"' EXIT

    # Force readable logs for the ephemeral run container.
    cat > "$compose_override" <<'EOF'
services:
  unit:
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"
EOF

    compose=(
        docker compose
        -f "$REPO/test/docker-compose.yml"
        -f "$compose_override"
    )

    "${compose[@]}" build unit

    action_args=(--dry-run --Werror)
    [ "$mode" = fix ] && action_args=(-i)

    "${compose[@]}" run --rm -T \
        -v "$REPO:/src" \
        -w /src \
        --entrypoint bash \
        unit -euo pipefail -c '
            dirs=(core tests app platform tui gui daemon cli)

            existing=()
            for d in "${dirs[@]}"; do
                [ -d "$d" ] && existing+=("$d")
            done

            if [ "${#existing[@]}" -eq 0 ]; then
                echo "check-format: no source directories to check" >&2
                exit 0
            fi

            files=()
            while IFS= read -r -d "" f; do
                files+=("$f")
            done < <(
                find "${existing[@]}" \( -name "*.hpp" -o -name "*.cpp" \) -print0
            )

            if [ "${#files[@]}" -eq 0 ]; then
                echo "check-format: no C++ files to check" >&2
                exit 0
            fi

            cf="$(command -v clang-format-18 || command -v clang-format || true)"
            if [ -z "$cf" ]; then
                echo "check-format: container has no clang-format-18/clang-format" >&2
                exit 3
            fi

            printf "%s\0" "${files[@]}" | xargs -0 -r "$cf" "$@"

            if [ "$1" = "-i" ]; then
                echo "check-format: reformatted ${#files[@]} file(s) with $cf" >&2
            else
                echo "check-format: OK — ${#files[@]} file(s) clean ($cf)" >&2
            fi
        ' bash "${action_args[@]}"

    exit $?
fi

# --- pick the clang-format binary (prefer the CI-pinned major, 18) ------------
cf=""
for c in clang-format-18 clang-format; do
    if command -v "$c" >/dev/null 2>&1; then cf="$c"; break; fi
done
[ -n "$cf" ] || {
    echo "check-format: no clang-format on PATH — install clang-format-18, or use --container" >&2
    exit 3
}
ver="$("$cf" --version | grep -oE '[0-9]+' | head -1)"
[ "$ver" = 18 ] || echo "check-format: note — $cf is v$ver; CI pins 18. Exact parity: scripts/check-format.sh --container" >&2

# --- collect the target file list ---------------------------------------------
cd "$REPO"
targets=()
if [ "$scope" = staged ]; then
    # Added/copied/modified staged paths, filtered to our dirs + C++ extensions.
    re="^($(
        IFS='|'
        echo "${DIRS[*]}"
    ))/.*\.(hpp|cpp)$"
    while IFS= read -r f; do targets+=("$f"); done < <(
        git diff --cached --name-only --diff-filter=ACM | grep -E "$re" || true
    )
else
    while IFS= read -r f; do targets+=("$f"); done < <(
        find "${DIRS[@]}" \( -name '*.hpp' -o -name '*.cpp' \) -print
    )
fi

[ "${#targets[@]}" -eq 0 ] && {
    echo "check-format: no C++ files to check"
    exit 0
}

if [ "$mode" = fix ]; then
    printf '%s\0' "${targets[@]}" | xargs -0 "$cf" -i
    echo "check-format: reformatted ${#targets[@]} file(s) with $cf"
    exit 0
fi

if printf '%s\0' "${targets[@]}" | xargs -0 "$cf" --dry-run --Werror; then
    echo "check-format: OK — ${#targets[@]} file(s) clean ($cf)"
else
    echo "check-format: FAILED — run 'scripts/check-format.sh --fix' then re-stage the files" >&2
    exit 1
fi
