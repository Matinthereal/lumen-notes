"""Cards worker (Phase 8): Anki .apkg export (genanki) and import (reads the package's SQLite)."""
from __future__ import annotations

import os
import re
import sqlite3
import sys
import tempfile
import zipfile

from .rpc import serve

BASIC_ID, CLOZE_ID, DECK_ID = 1607392319, 1607392320, 2059400110


def export_apkg(cards: list[dict], out: str, deck_name: str = "Lumen", **_: object) -> dict:
    import genanki
    basic = genanki.Model(BASIC_ID, "Lumen Basic",
                          fields=[{"name": "Front"}, {"name": "Back"}],
                          templates=[{"name": "Card 1", "qfmt": "{{Front}}", "afmt": "{{FrontSide}}<hr id=answer>{{Back}}"}])
    cloze = genanki.Model(CLOZE_ID, "Lumen Cloze", model_type=genanki.Model.CLOZE,
                          fields=[{"name": "Text"}, {"name": "Back Extra"}],
                          templates=[{"name": "Cloze", "qfmt": "{{cloze:Text}}", "afmt": "{{cloze:Text}}<br>{{Back Extra}}"}])
    deck = genanki.Deck(DECK_ID, deck_name)
    n = 0
    for c in cards:
        tags = [t.replace(" ", "_") for t in (c.get("tags") or []) if t]
        if c.get("kind") == "cloze":
            deck.add_note(genanki.Note(model=cloze, fields=[c.get("front", ""), c.get("back", "")], tags=tags, guid=genanki.guid_for("lumen", c.get("id", c.get("front", "")))))
        else:
            deck.add_note(genanki.Note(model=basic, fields=[c.get("front", ""), c.get("back", "")], tags=tags, guid=genanki.guid_for("lumen", c.get("id", c.get("front", "")))))
        n += 1
    genanki.Package(deck).write_to_file(out)
    return {"file": out, "cards": n}


def _strip_html(s: str) -> str:
    s = re.sub(r"<br\s*/?>", "\n", s)
    s = re.sub(r"<[^>]+>", "", s)
    return s.replace("&nbsp;", " ").replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").strip()


def import_apkg(path: str, **_: object) -> dict:
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(path) as z:
            name = "collection.anki21" if "collection.anki21" in z.namelist() else "collection.anki2"
            z.extract(name, tmp)
        db = sqlite3.connect(os.path.join(tmp, name))
        out = []
        for flds, tags in db.execute("SELECT flds, tags FROM notes"):
            fields = [_strip_html(f) for f in flds.split("\x1f")]
            front = fields[0] if fields else ""
            back = fields[1] if len(fields) > 1 else ""
            kind = "cloze" if "{{c1::" in front else "basic"
            out.append({"kind": kind, "front": front, "back": back, "tags": [t for t in tags.split() if t]})
        db.close()
    return {"cards": out}


if __name__ == "__main__":
    sys.exit(serve("cards", {"export_apkg": export_apkg, "import_apkg": import_apkg}))
