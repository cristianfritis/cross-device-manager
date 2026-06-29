#!/usr/bin/env bash
# Snapshot / run / revert helper around the disposable test VM.
set -euo pipefail

SNAPSHOT_NAME="devmgr-clean"

usage() { echo "usage: $0 {create|run <cmd>|revert}" >&2; exit 2; }

case "${1:-}" in
    create) vagrant snapshot save "$SNAPSHOT_NAME" ;;
    run)    shift; vagrant ssh -c "${*:?command required}" ;;
    revert) vagrant snapshot restore "$SNAPSHOT_NAME" ;;
    *)      usage ;;
esac
