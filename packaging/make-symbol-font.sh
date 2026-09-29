#!/usr/bin/env bash
# The handful of symbols the interface draws with (☐ ▾ ⇥ ★ ∑ …), cut out of DejaVu Sans. Android's
# own fonts lack most of them; main.cpp loads this as the fallback there. Needs fonttools and the
# dejavu-sans-fonts package. Re-run when the QML gains a new symbol.
set -euo pipefail
cd "$(dirname "$0")/fonts"
python3 -m fontTools.subset /usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf \
  --text='›↧⇤⇥⇧∅∑≥⌫⏎⏱▸▾★☐☑☒⚠✓✔•·' \
  --output-file=symbols.ttf --layout-features='' --no-hinting --desubroutinize
cp /usr/share/licenses/dejavu-sans-fonts/LICENSE LICENSE-DejaVu
