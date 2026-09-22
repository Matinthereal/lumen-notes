#!/usr/bin/env bash
# Create the Python 3.13 venv the workers run in (D-006), install deps, freeze the lock.
set -euo pipefail
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3.13}"
command -v "$PY" >/dev/null || { echo "need $PY (dnf install python3.13)"; exit 1; }
[ -d .venv ] || "$PY" -m venv .venv
# venv layout is bin/ on Unix, Scripts/ on Windows (the interpreter that created it decides this,
# not the shell running this script — matters under git-bash/MSYS2 on Windows).
VENV_BIN=.venv/bin
[ -d .venv/Scripts ] && VENV_BIN=.venv/Scripts
"$VENV_BIN/pip" install --upgrade pip -q
if [ -s workers/requirements.txt ] && [ "${FRESH:-0}" != "1" ]; then
  "$VENV_BIN/pip" install -r workers/requirements.txt --extra-index-url https://download.pytorch.org/whl/cpu
else
  "$VENV_BIN/pip" install -r workers/requirements.in --extra-index-url https://download.pytorch.org/whl/cpu
  "$VENV_BIN/pip" freeze > workers/requirements.txt
fi
echo "workers venv ready: $("$VENV_BIN/python" --version)"
