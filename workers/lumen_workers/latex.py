"""LaTeX worker (Phase 4, D-009 interim): renders math with matplotlib's mathtext to PNG. Covers
the everyday A-level set (fractions, roots, sums, integrals, Greek, sub/superscripts, matrices via
\\begin{matrix} is NOT supported by mathtext — falls back to an error image with the message)."""
from __future__ import annotations

import hashlib
import os
import sys

import matplotlib

matplotlib.use("Agg")
from matplotlib import pyplot as plt  # noqa: E402
from matplotlib.mathtext import MathTextParser  # noqa: E402

from .rpc import serve  # noqa: E402

_parser = MathTextParser("path")


def render(tex: str, out_dir: str, dpi: int = 160, color: str = "#000000", **_: object) -> dict:
    tex = tex.strip()
    if not tex.startswith("$"):
        tex = f"${tex}$"
    key = hashlib.sha1(f"{tex}|{dpi}|{color}".encode()).hexdigest()[:20]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"tex-{key}.png")
    if os.path.exists(out):
        return {"file": out, "ok": True}
    fig = plt.figure(figsize=(0.01, 0.01))
    try:
        fig.text(0, 0, tex, fontsize=12, color=color)
        fig.savefig(out + ".part", dpi=dpi, transparent=True, bbox_inches="tight", pad_inches=0.04, format="png")
        os.replace(out + ".part", out)
        return {"file": out, "ok": True}
    except Exception as exc:  # unsupported command, unbalanced braces …
        plt.close(fig)
        fig = plt.figure(figsize=(0.01, 0.01))
        fig.text(0, 0, f"LaTeX error: {str(exc)[:80]}", fontsize=9, color="#AB1D19")
        fig.savefig(out + ".part", dpi=dpi, transparent=True, bbox_inches="tight", pad_inches=0.04, format="png")
        os.replace(out + ".part", out)
        return {"file": out, "ok": False, "error": str(exc)}
    finally:
        plt.close(fig)


def check(tex: str, **_: object) -> dict:
    try:
        _parser.parse(tex if tex.startswith("$") else f"${tex}$")
        return {"ok": True}
    except Exception as exc:
        return {"ok": False, "error": str(exc)}


if __name__ == "__main__":
    sys.exit(serve("latex", {"render": render, "check": check}))
