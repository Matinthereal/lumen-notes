#!/usr/bin/env bash
# Create the Python 3.13 venv the workers run in (D-006), install deps, freeze the lock.
set -euo pipefail
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3.13}"
command -v "$PY" >/dev/null || { echo "need $PY (dnf install python3.13)"; exit 1; }
[ -d .venv ] || "$PY" -m venv .venv
.venv/bin/pip install --upgrade pip -q
if [ -s workers/requirements.txt ] && [ "${FRESH:-0}" != "1" ]; then
  .venv/bin/pip install -r workers/requirements.txt --extra-index-url https://download.pytorch.org/whl/cpu
else
  .venv/bin/pip install -r workers/requirements.in --extra-index-url https://download.pytorch.org/whl/cpu
  .venv/bin/pip freeze > workers/requirements.txt
fi
echo "workers venv ready: $(.venv/bin/python --version)"
