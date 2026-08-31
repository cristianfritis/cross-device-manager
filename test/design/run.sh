#!/bin/bash
# Design verification sweep (D6): every view x every size x every posture.
# No result is ever derived from a single view -- F3 was under-reported for
# exactly that reason, because the live check happened to be standing on
# Snapshots while Devices overflowed too.
set -u
OUT="${OUT:-/out}"
mkdir -p "$OUT"

POSTURES="${POSTURES:-degraded healthy}"
GUI_SIZES="${GUI_SIZES:-1024x640 800x520}"
TUI_SIZES="${TUI_SIZES:-120x32 100x28 80x24}"
S=/src/test/design/session.sh

# Fixture self-tests first (tasks 3.3, 3.5). A posture that silently failed to
# apply would otherwise surface as a wall of design failures rather than as the
# harness fault it is -- the same reasoning as the a11y-registration check.
for posture in $POSTURES; do
    echo "=== fixture self-test: $posture ==="
    POSTURE="$posture" DISPLAY_NUM=99 "$S" python3 /src/test/design/test_fixtures.py || {
        echo "FIXTURES FAILED for posture $posture -- aborting sweep" >&2
        exit 1
    }
done

for posture in $POSTURES; do
    echo "=== posture: $posture ==="
    # ONE session for the whole posture: the X screen is sized to the largest
    # GUI window and narrower ones are resized inside it, so every size is
    # captured without restarting the backends (see capture.py:sweep).
    POSTURE="$posture" SCREEN_W=1024 SCREEN_H=640 DISPLAY_NUM=99 \
        "$S" python3 /src/test/design/capture.py sweep "$OUT/$posture"
done

echo "=== assertions ==="
python3 /src/test/design/assertions.py "$OUT"
