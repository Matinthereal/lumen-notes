#!/usr/bin/env bash
# The two typefaces of Lumen's look (ADR mynotes-003): Figtree for everything you tap, Newsreader
# for titles. Both SIL OFL 1.1, from github.com/google/fonts. Qt handles fixed weights the same
# way on every platform, so the variable fonts are cut into the weights the app uses. Needs curl
# and fonttools. The TTFs and licences it writes are committed.
set -euo pipefail
cd "$(dirname "$0")/fonts"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
base=https://raw.githubusercontent.com/google/fonts/main/ofl
curl -fsSL -o "$tmp/figtree.ttf" "$base/figtree/Figtree%5Bwght%5D.ttf"
curl -fsSL -o "$tmp/newsreader.ttf" "$base/newsreader/Newsreader%5Bopsz,wght%5D.ttf"
curl -fsSL -o OFL-Figtree.txt "$base/figtree/OFL.txt"
curl -fsSL -o OFL-Newsreader.txt "$base/newsreader/OFL.txt"
python3 - "$tmp" <<'PY'
import sys
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
tmp = sys.argv[1]
cuts = [("figtree.ttf", "Figtree", w, name, {"wght": w}) for w, name in ((400, "Regular"), (500, "Medium"), (600, "SemiBold"), (700, "Bold"))]
cuts += [("newsreader.ttf", "Newsreader", 500, "Medium", {"wght": 500, "opsz": 36})]
for src, family, weight, style, axes in cuts:
    font = instancer.instantiateVariableFont(TTFont(f"{tmp}/{src}"), axes, updateFontNames=False)
    names = font["name"]
    for rec in list(names.names):
        if rec.nameID in (16, 17, 21, 22, 25):     # typographic and variation names no longer apply
            names.removeNames(nameID=rec.nameID)
    full = f"{family} {style}"
    for nid, value in ((1, family if style in ("Regular", "Bold") else full), (2, style if style in ("Regular", "Bold") else "Regular"),
                       (4, full), (6, full.replace(" ", "-")), (16, family), (17, style)):
        names.setName(value, nid, 3, 1, 0x409)
    font["OS/2"].usWeightClass = weight
    font.save(f"{family}-{style}.ttf")
    print("wrote", f"{family}-{style}.ttf")
PY
