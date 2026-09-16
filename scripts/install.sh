#!/usr/bin/env bash
# Install for this user (no root): ~/.local/bin/lumen + launcher entry + icon. Re-run after a rebuild.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -x build/app/lumen ] || scripts/build.sh
cmake --install build --prefix "$HOME/.local" >/dev/null
# the workers' venv: reuse the repo's if present, else create one under the data dir
if [ -d .venv ]; then
  mkdir -p "$HOME/.local/share/lumen" && ln -sfn "$PWD/.venv" "$HOME/.local/share/lumen/venv"
fi
update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
xdg-icon-resource forceupdate --theme hicolor 2>/dev/null || true
kbuildsycoca6 --noincremental >/dev/null 2>&1 || true     # Plasma's launcher/menu picks up the entry at once
echo "installed: $HOME/.local/bin/lumen (launcher entry: Lumen)"
