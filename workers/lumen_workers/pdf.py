"""PDF worker (Phase 3, D-019): open, render pages to PNG (cached), words with boxes, text, search,
and flattened export with ink drawn as filled vector outlines. Coordinates in and out are PAGE
UNITS (1/96 in); PyMuPDF works in points (1/72 in)."""
from __future__ import annotations

import hashlib
import math
import os
import re
import sys

import pymupdf

from .rpc import serve

PT = 96.0 / 72.0  # points → page units
from collections import OrderedDict

_docs: "OrderedDict[str, pymupdf.Document]" = OrderedDict()
_MAX_OPEN = 6


def _doc(path: str) -> pymupdf.Document:
    d = _docs.get(path)
    if d is None:
        d = pymupdf.open(path)
        _docs[path] = d
        while len(_docs) > _MAX_OPEN:          # a term of slide decks must not stay mapped forever
            _old_path, old = _docs.popitem(last=False)
            try:
                old.close()
            except Exception:
                pass
    else:
        _docs.move_to_end(path)
    return d


def info(path: str, **_: object) -> dict:
    d = _doc(path)
    sizes = [[pg.rect.width * PT, pg.rect.height * PT] for pg in d]
    return {"pages": d.page_count, "title": (d.metadata or {}).get("title") or "", "sizes": sizes}


def render(path: str, index: int, scale: float, out_dir: str, **_: object) -> dict:
    d = _doc(path)
    page = d[int(index)]
    scale = max(0.1, min(float(scale), 8.0))
    key = hashlib.sha1(f"{path}|{index}|{scale:.3f}".encode()).hexdigest()[:20]
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"pdf-{key}.png")
    m = pymupdf.Matrix(PT * scale, PT * scale)
    if not os.path.exists(out):
        pix = page.get_pixmap(matrix=m, alpha=False)
        tmp = out + ".part"
        with open(tmp, "wb") as f:      # explicit PNG bytes: save() would infer the format from ".part"
            f.write(pix.tobytes("png"))
        os.replace(tmp, out)
        w, h = pix.width, pix.height
    else:
        w, h = int(page.rect.width * PT * scale + 0.5), int(page.rect.height * PT * scale + 0.5)
    return {"file": out, "width": w, "height": h, "pageWidth": page.rect.width * PT, "pageHeight": page.rect.height * PT, "scale": scale}


def words(path: str, index: int, **_: object) -> dict:
    page = _doc(path)[int(index)]
    out = []
    for x0, y0, x1, y1, w, block, line, wno in page.get_text("words"):
        out.append([x0 * PT, y0 * PT, x1 * PT, y1 * PT, w, int(block), int(line), int(wno)])
    return {"words": out}


def text(path: str, index: int, **_: object) -> dict:
    return {"text": _doc(path)[int(index)].get_text("text")}


def search(path: str, query: str, **_: object) -> dict:
    hits = []
    for i, page in enumerate(_doc(path)):
        rects = page.search_for(query)
        if rects:
            r = rects[0]
            hits.append({"index": i, "count": len(rects), "first": [r.x0 * PT, r.y0 * PT, r.x1 * PT, r.y1 * PT]})
    return {"hits": hits}


def _outline(points: list[list[float]], cap_round: bool) -> list[pymupdf.Point]:
    """Closed outline of a variable-width centreline [[x, y, w], ...] (page units → points)."""
    n = len(points)
    if n == 0:
        return []
    if n == 1:
        x, y, w = points[0]
        r = max(w, 0.3) / 2
        return [pymupdf.Point((x + r * math.cos(a)) / PT, (y + r * math.sin(a)) / PT) for a in [i / 12 * 2 * math.pi for i in range(12)]]
    left, right = [], []
    for i, (x, y, w) in enumerate(points):
        if i == 0:
            dx, dy = points[1][0] - x, points[1][1] - y
        elif i == n - 1:
            dx, dy = x - points[i - 1][0], y - points[i - 1][1]
        else:
            dx, dy = points[i + 1][0] - points[i - 1][0], points[i + 1][1] - points[i - 1][1]
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln * w / 2, dx / ln * w / 2
        left.append((x + nx, y + ny))
        right.append((x - nx, y - ny))
    poly = list(left)
    if cap_round:
        x, y, w = points[-1]
        dx, dy = x - points[-2][0], y - points[-2][1]
        a0 = math.atan2(dy, dx) - math.pi / 2
        poly += [(x + w / 2 * math.cos(a0 + k / 8 * math.pi), y + w / 2 * math.sin(a0 + k / 8 * math.pi)) for k in range(1, 8)]
    poly += list(reversed(right))
    if cap_round:
        x, y, w = points[0]
        dx, dy = points[1][0] - x, points[1][1] - y
        a0 = math.atan2(dy, dx) + math.pi / 2
        poly += [(x + w / 2 * math.cos(a0 + k / 8 * math.pi), y + w / 2 * math.sin(a0 + k / 8 * math.pi)) for k in range(1, 8)]
    return [pymupdf.Point(px / PT, py / PT) for px, py in poly]


_MD_STRIP = [
    (re.compile(r"^#{1,6}\s*", re.M), ""),          # headings keep their words
    (re.compile(r"\*\*(.+?)\*\*", re.S), r"\1"),
    (re.compile(r"(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)", re.S), r"\1"),
    (re.compile(r"`([^`]*)`"), r"\1"),
    (re.compile(r"^\s*[-*+]\s+", re.M), "· "),
]


def _plain_markdown(md: str) -> str:
    """PDF text is flat: keep the words and the line breaks, drop the syntax."""
    out = md
    for pattern, repl in _MD_STRIP:
        out = pattern.sub(repl, out)
    return out.strip()


def _rgb(text: str):
    """'#RRGGBB' → (r, g, b) floats, or None for 'no colour'."""
    text = (text or "").strip().lstrip("#")
    if len(text) != 6:
        return None
    return tuple(int(text[i:i + 2], 16) / 255 for i in (0, 2, 4))


def _draw_shape(page, sh: dict) -> None:
    x, y = float(sh["x"]) / PT, float(sh["y"]) / PT
    w, h = float(sh["w"]) / PT, float(sh["h"]) / PT
    stroke, fill = _rgb(sh.get("stroke", "#000000")), _rgb(sh.get("fill", ""))
    width = max(0.2, float(sh.get("width", 2)) / PT)
    kind = sh.get("kind", "rect")
    shape = page.new_shape()
    if kind in ("line", "arrow"):
        p0, p1 = pymupdf.Point(x, y), pymupdf.Point(x + w, y + h)
        shape.draw_line(p0, p1)
        if kind == "arrow":
            ang = math.atan2(h, w)
            head = max(10 / PT, min(26 / PT, math.hypot(w, h) * 0.22))
            for spread in (-0.42, 0.42):
                shape.draw_line(pymupdf.Point(p1.x - math.cos(ang + spread) * head,
                                              p1.y - math.sin(ang + spread) * head), p1)
        shape.finish(color=stroke, width=width)
        shape.commit()
        return
    rect = pymupdf.Rect(x, y, x + abs(w), y + abs(h))
    if kind == "ellipse":
        shape.draw_oval(rect)
    elif kind == "triangle":
        shape.draw_polyline([pymupdf.Point(rect.x0 + rect.width / 2, rect.y0),
                             pymupdf.Point(rect.x1, rect.y1), pymupdf.Point(rect.x0, rect.y1),
                             pymupdf.Point(rect.x0 + rect.width / 2, rect.y0)])
    else:
        shape.draw_rect(rect)
    shape.finish(color=stroke, fill=fill, width=width, closePath=True)
    shape.commit()


def export(pages: list[dict], out: str, **_: object) -> dict:
    """pages: [{src, index, width, height, polys: [{points: [[x,y,w]...], color: [r,g,b], opacity, round}]}]"""
    doc = pymupdf.open()
    for pg in pages:
        if pg.get("src"):
            src = _doc(pg["src"])
            doc.insert_pdf(src, from_page=int(pg["index"]), to_page=int(pg["index"]))
            page = doc[-1]
        else:
            page = doc.new_page(width=float(pg["width"]) / PT, height=float(pg["height"]) / PT)
        for pic in pg.get("images", []):          # under the ink, as on screen
            try:
                page.insert_image(pymupdf.Rect(float(pic["x"]) / PT, float(pic["y"]) / PT,
                                               (float(pic["x"]) + float(pic["w"])) / PT,
                                               (float(pic["y"]) + float(pic["h"])) / PT),
                                  filename=pic["path"], keep_proportion=False)
            except Exception:
                pass                                  # a missing picture must not lose the whole export
        shape = page.new_shape()
        for poly in pg.get("polys", []):
            pts = _outline(poly["points"], bool(poly.get("round", True)))
            if len(pts) < 3:
                continue
            shape.draw_polyline(pts + [pts[0]])
            shape.finish(color=None, fill=tuple(poly["color"]), fill_opacity=float(poly.get("opacity", 1.0)), width=0, closePath=True)
        shape.commit()
        for sh in pg.get("shapes", []):          # shapes are objects, so they export as vectors
            _draw_shape(page, sh)
        for block in pg.get("blocks", []):       # typed notes, above the ink as on screen
            text = _plain_markdown(block.get("markdown", ""))
            if not text:
                continue
            rect = pymupdf.Rect(float(block["x"]) / PT, float(block["y"]) / PT,
                                (float(block["x"]) + float(block["w"])) / PT, float(block["y"]) / PT + 10000)
            page.insert_textbox(rect, text, fontsize=10.5, fontname="helv", color=(0, 0, 0), align=0)
    tmp = out + ".part"
    doc.save(tmp, garbage=3, deflate=True)
    doc.close()
    os.replace(tmp, out)
    return {"file": out, "pages": len(pages)}


def make_test_pdf(out: str, lines: list[str], **_: object) -> dict:
    doc = pymupdf.open()
    page = doc.new_page()
    y = 72
    for t in lines:
        page.insert_text((72, y), t, fontsize=14)
        y += 24
    doc.save(out)
    doc.close()
    return {"file": out, "pages": 1}


if __name__ == "__main__":
    sys.exit(serve("pdf", {"info": info, "render": render, "words": words, "text": text, "search": search,
                           "export": export, "make_test_pdf": make_test_pdf}))
