#!/usr/bin/env bash
# Launch the app from the build tree (Wayland). Extra args go to the app.
set -euo pipefail
cd "$(dirname "$0")/.."
export QT_FORCE_STDERR_LOGGING=1   # keep Qt messages on stderr (they go to journald otherwise)
exec ./build/app/lumen "$@"
