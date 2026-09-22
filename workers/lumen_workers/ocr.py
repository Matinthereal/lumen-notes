"""OCR worker (Phase 7): TrOCR (handwritten) on line images for search, pix2tex for handwritten
maths → LaTeX. Runs niced: it is background work that must never compete with ink."""
from __future__ import annotations

import os
import sys
import time

if hasattr(os, "nice"):   # no niceness concept on Windows
    os.nice(10)

from .rpc import serve  # noqa: E402

_state: dict = {"ocr": None, "latex": None, "model_name": None}


def prepare(models_dir: str, model: str = "microsoft/trocr-small-handwritten", **_: object) -> dict:
    os.environ.setdefault("HF_HOME", os.path.join(models_dir, "hf"))
    os.environ.setdefault("TRANSFORMERS_OFFLINE", "0")
    t = time.time()
    import torch
    from transformers import TrOCRProcessor, VisionEncoderDecoderModel
    torch.set_num_threads(max(2, (os.cpu_count() or 4) // 2))
    cache = os.path.join(models_dir, "hf")
    try:    # cached: no network round-trip (HF Hub timeouts stalled the worker for a minute at start)
        proc = TrOCRProcessor.from_pretrained(model, cache_dir=cache, local_files_only=True)
        mdl = VisionEncoderDecoderModel.from_pretrained(model, cache_dir=cache, local_files_only=True).eval()
    except Exception:
        proc = TrOCRProcessor.from_pretrained(model, cache_dir=cache)
        mdl = VisionEncoderDecoderModel.from_pretrained(model, cache_dir=cache).eval()
    _state["ocr"] = (proc, mdl)
    _state["model_name"] = model
    return {"ok": True, "seconds": round(time.time() - t, 1), "model": model}


def recognize_lines(lines: list[dict], **_: object) -> dict:
    """lines: [{id, png}] → [{id, text, confidence, ms}]"""
    if _state["ocr"] is None:
        raise RuntimeError("call prepare first")
    import torch
    from PIL import Image
    proc, mdl = _state["ocr"]
    out = []
    for ln in lines:
        t = time.time()
        img = Image.open(ln["png"]).convert("RGB")
        pixel_values = proc(images=img, return_tensors="pt").pixel_values
        with torch.no_grad():
            gen = mdl.generate(pixel_values, max_new_tokens=64, output_scores=True, return_dict_in_generate=True, num_beams=1)
        text = proc.batch_decode(gen.sequences, skip_special_tokens=True)[0].strip()
        conf = 0.0
        if gen.scores:
            probs = [float(torch.softmax(s[0], dim=-1).max()) for s in gen.scores]
            conf = sum(probs) / len(probs)
        out.append({"id": ln["id"], "text": text, "confidence": round(conf, 3), "ms": int((time.time() - t) * 1000)})
    return {"lines": out, "model": _state["model_name"]}


def unload(**_: object) -> dict:
    """Free ~1 GB when the page has been quiet for a while; prepare() brings it back in a second."""
    _state["ocr"] = None
    _state["latex"] = None
    import gc
    gc.collect()
    return {"ok": True}


def latex(png: str, **_: object) -> dict:
    if _state["latex"] is None:
        from pix2tex.cli import LatexOCR
        _state["latex"] = LatexOCR()
    from PIL import Image
    t = time.time()
    img = Image.open(png).convert("RGB")
    tex = _state["latex"](img)
    return {"latex": tex.strip(), "ms": int((time.time() - t) * 1000)}


if __name__ == "__main__":
    sys.exit(serve("ocr", {"prepare": prepare, "recognize_lines": recognize_lines, "latex": latex, "unload": unload}))
