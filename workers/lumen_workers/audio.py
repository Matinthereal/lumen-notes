"""Audio worker (Phase 5, D-007/D-019): records from PipeWire through ffmpeg straight to Opus while
feeding 16 kHz PCM to faster-whisper for live transcription; re-transcribes the finished file with a
larger model; plays back through mpv's JSON IPC so seek/pause are exact. Nothing here blocks the app.

Notifications to the app (no id): level {rms}, segments {recording, pass, segments:[{t0,t1,text}]},
status {text}, playback {position_ms, playing}.
"""
from __future__ import annotations

import collections
import json
import math
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Optional

import numpy as np

from .rpc import serve

SAMPLE_RATE = 16000
_send = None          # set by serve_with_notify
_state: dict = {"rec": None, "player": None, "monitor": None, "models": {}, "backend": "cpu", "speed": 1.0}
_model_lock = threading.Lock()


# ---------------------------------------------------------------- models

def _model(name: str):
    with _model_lock:
        m = _state["models"].get(name)
        if m is None:
            from faster_whisper import WhisperModel
            cpu = max(2, (os.cpu_count() or 4) - 2)
            m = WhisperModel(name, device="cpu", compute_type="int8", cpu_threads=cpu, download_root=_state.get("models_dir"))
            _state["models"][name] = m
        return m


def prepare(models_dir: str, live_model: str = "small.en", **_: object) -> dict:
    _state["models_dir"] = models_dir
    _state["live_model"] = live_model
    t = time.time()
    _model(live_model)
    return {"ok": True, "seconds": round(time.time() - t, 1), "model": live_model}


def sources(**_: object) -> dict:
    """Microphones, best first. A source whose every port is 'not available' (an empty jack) only
    delivers a DC pop — that is how the maker's first recordings ended up silent."""
    out, default = [], ""
    try:
        system_default = subprocess.run(["pactl", "get-default-source"], capture_output=True, text=True, timeout=5).stdout.strip()
        j = json.loads(subprocess.run(["pactl", "-f", "json", "list", "sources"], capture_output=True, text=True, timeout=5).stdout)
        for s in j:
            if s.get("name", "").endswith(".monitor"):
                continue
            ports = s.get("ports") or []
            available = (not ports) or any(p.get("availability") != "not available" for p in ports)
            desc = s.get("description") or s["name"]
            internal = "digital" in desc.lower() or "internal" in desc.lower() or "built-in" in desc.lower()
            score = (2 if available else 0) + (1 if internal else 0) + (0.5 if s["name"] == system_default else 0)
            out.append({"name": s["name"], "description": desc + ("" if available else " (nothing plugged in)"), "available": available,
                        "internal": internal, "default": False, "score": score})
        out.sort(key=lambda x: -x["score"])
        if out:
            out[0]["default"] = True
            default = out[0]["name"]
    except Exception as exc:  # pactl missing or odd output: fall back to the default source
        out.append({"name": "default", "description": f"Default microphone ({exc.__class__.__name__})", "available": True, "internal": True, "default": True, "score": 1})
        default = "default"
    return {"sources": out, "default": default}


def check(source: str = "default", seconds: float = 1.2, input_args: Optional[list] = None, **_: object) -> dict:
    """Capture briefly and report the level after DC removal: is this microphone alive?"""
    inp = input_args or ["-f", "pulse", "-i", source]
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", *inp, "-t", str(seconds), "-af", "highpass=f=100",
           "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "pipe:1"]
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=seconds + 8)
    except subprocess.TimeoutExpired:
        return {"ok": False, "verdict": "no data from this microphone (timed out)", "rms_db": -99.0}
    pcm = np.frombuffer(p.stdout, dtype=np.int16).astype(np.float32) / 32768.0
    if len(pcm) < SAMPLE_RATE // 4:
        return {"ok": False, "verdict": "no data from this microphone" + (": " + p.stderr.decode(errors="replace").strip()[:100] if p.stderr else ""), "rms_db": -99.0}
    tail = pcm[len(pcm) // 3:]                       # skip the opening pop
    rms = float(np.sqrt(np.mean(tail * tail))) or 1e-9
    db = 20 * math.log10(rms)
    if db < -70:
        verdict = "silent — this input carries no sound"
    elif db < -55:
        verdict = "very quiet — check the microphone or speak closer"
    else:
        verdict = "alive"
    return {"ok": db >= -70, "verdict": verdict, "rms_db": round(db, 1)}


class Monitor:
    """Mic check: levels from a source without recording anything (stops when a recording starts)."""
    def __init__(self, source: str, notify):
        self.proc = subprocess.Popen(["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-f", "pulse", "-i", source,
                                      "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "pipe:1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.notify = notify
        self.stop_flag = threading.Event()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        chunk = SAMPLE_RATE // 10 * 2
        last = -1.0
        while not self.stop_flag.is_set():
            data = self.proc.stdout.read(chunk)
            if not data:
                break
            pcm = np.frombuffer(data, dtype=np.int16).astype(np.float32) / 32768.0
            rms = float(np.sqrt(np.mean(pcm * pcm))) if len(pcm) else 0.0
            level = min(1.0, rms * 6.0)
            if abs(level - last) > 0.01:
                last = level
                self.notify("level", {"rms": level, "monitor": True})
        if not self.stop_flag.is_set():
            self.notify("status", {"text": "microphone check ended — is the input still there?"})

    def stop(self):
        self.stop_flag.set()
        try:
            self.proc.kill()
        except Exception:
            pass


def monitor(source: str = "default", **_: object) -> dict:
    monitor_stop()
    if _state["rec"] is not None:
        return {"ok": False, "error": "recording"}
    _state["monitor"] = Monitor(source, _send)
    return {"ok": True}


def monitor_stop(**_: object) -> dict:
    m = _state.get("monitor")
    if m is not None:
        m.stop()
        _state["monitor"] = None
    return {"ok": True}


# ---------------------------------------------------------------- recording

class Recording:
    def __init__(self, out_path: str, source: str, model: str, live: bool, notify, input_args: Optional[list] = None):
        self.out = out_path
        self.model = model
        self.live = live
        self.notify = notify
        self.t0 = time.time()
        self.samples = 0
        self.buf = np.zeros(0, dtype=np.float32)
        self.buf_start = 0.0          # seconds into the recording of buf[0]
        self.done = threading.Event()
        self.segments: list[dict] = []
        self.lock = threading.Lock()          # before any thread starts: the transcriber takes it on its first line
        inp = input_args or ["-f", "pulse", "-i", source]
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", *inp,
               "-map", "0:a", "-ac", "1", "-ar", "48000", "-c:a", "libopus", "-b:a", "24k", "-f", "ogg", "-y", out_path,
               "-map", "0:a", "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "pipe:1"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.err_tail = collections.deque(maxlen=20)
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.worker = threading.Thread(target=self._transcribe_loop, daemon=True)
        self.worker.start()

    def _drain_stderr(self):
        for line in self.proc.stderr:
            self.err_tail.append(line.decode(errors="replace").rstrip())

    def _read(self):
        chunk = SAMPLE_RATE // 10 * 2   # 100 ms of s16
        last_level = 0.0
        got_any = False
        while True:
            data = self.proc.stdout.read(chunk)
            if not data:
                if not got_any and not self.done.is_set():
                    err = " | ".join(list(self.err_tail)[-3:])
                    self.notify("status", {"text": "no audio from the microphone" + (f": {err[:120]}" if err else "")})
                elif not self.done.is_set():
                    self.notify("status", {"text": "the microphone stream ended unexpectedly" + (": " + list(self.err_tail)[-1][:100] if self.err_tail else "")})
                break
            got_any = True
            pcm = np.frombuffer(data, dtype=np.int16).astype(np.float32) / 32768.0
            with self.lock:
                self.buf = np.concatenate([self.buf, pcm])
                self.samples += len(pcm)
            rms = float(np.sqrt(np.mean(pcm * pcm))) if len(pcm) else 0.0
            level = min(1.0, rms * 6.0)
            if abs(level - last_level) > 0.02:
                last_level = level
                self.notify("level", {"rms": level})
        self.done.set()

    def _transcribe_loop(self):
        if not self.live:
            return
        try:
            self._transcribe_loop_inner()
        except Exception as exc:               # never a silent death: the app shows it
            self.notify("status", {"text": f"live transcription stopped: {exc.__class__.__name__}: {exc}"})

    def _transcribe_loop_inner(self):
        try:
            model = _model(self.model)
        except Exception as exc:
            self.notify("status", {"text": f"live transcription off: {exc}"})
            return
        self.notify("status", {"text": "listening"})
        window = 6.0
        while not self.done.is_set() or len(self.buf) > SAMPLE_RATE:
            with self.lock:
                have = len(self.buf) / SAMPLE_RATE
            if have < window and not self.done.is_set():
                time.sleep(0.25)
                continue
            with self.lock:
                audio = self.buf.copy()
                start = self.buf_start
            try:
                segs, _info = model.transcribe(audio, language="en", beam_size=1, vad_filter=True, condition_on_previous_text=False)
                segs = list(segs)
            except Exception as exc:
                self.notify("status", {"text": f"transcribe error: {exc}"})
                time.sleep(1)
                continue
            end_of_audio = len(audio) / SAMPLE_RATE
            commit_before = end_of_audio - (0.0 if self.done.is_set() else 1.0)
            out = []
            consumed = 0.0
            for s in segs:
                if s.end <= commit_before:
                    out.append({"t0": int((start + s.start) * 1000), "t1": int((start + s.end) * 1000), "text": s.text.strip()})
                    consumed = max(consumed, s.end)
            if out:
                self.segments += out
                self.notify("segments", {"pass": "live", "segments": out})
            if consumed <= 0 and end_of_audio > 2 * window:
                consumed = end_of_audio - window   # nothing said: slide the window
            if consumed > 0:
                n = int(consumed * SAMPLE_RATE)
                with self.lock:
                    self.buf = self.buf[n:]
                    self.buf_start += consumed
            if self.done.is_set() and len(self.buf) <= SAMPLE_RATE:
                break
            time.sleep(0.05)

    def stop(self, quick: bool = False) -> dict:
        try:
            self.proc.send_signal(subprocess.signal.SIGINT)
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()
        self.done.set()
        self.reader.join(timeout=5)
        self.worker.join(timeout=2 if quick else 30)     # quick: the app is quitting; the re-pass covers the tail
        return {"file": self.out, "duration_ms": int(self.samples / SAMPLE_RATE * 1000), "segments": len(self.segments)}


def start(out_path: str, source: str = "default", model: str = "small.en", live: bool = True, input_args: Optional[list] = None, **_: object) -> dict:
    if _state["rec"] is not None:
        return {"ok": False, "error": "already recording"}
    monitor_stop()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    _state["rec"] = Recording(out_path, source, model, live, _send, input_args)
    return {"ok": True}


def stop(**_: object) -> dict:
    rec = _state.get("rec")
    if rec is None:
        return {"ok": False, "error": "not recording"}
    _state["rec"] = None
    r = rec.stop()
    r["ok"] = True
    return r


def transcribe_file(path: str, model: str = "large-v3-turbo", **_: object) -> dict:
    # Runs while a lecture may be recording: lower priority so the live loop keeps up.
    try:
        os.setpriority(os.PRIO_PROCESS, threading.get_native_id(), 10)
    except Exception:
        pass
    m = _model(model)
    segs, info = m.transcribe(path, language="en", beam_size=3, vad_filter=True)
    out = [{"t0": int(s.start * 1000), "t1": int(s.end * 1000), "text": s.text.strip()} for s in segs]
    if model not in (_state.get("live_model") or "small.en",) and "large" in model:
        with _model_lock:                       # ~1 GB of RAM back until the next re-pass
            _state["models"].pop(model, None)
    return {"segments": out, "duration_ms": int((info.duration or 0) * 1000), "model": model}


def benchmark(path: str, **_: object) -> dict:
    """CPU (CTranslate2 int8) vs OpenVINO GPU/NPU (openvino-genai, needs an exported model dir)."""
    results = []
    t = time.time()
    ref = transcribe_file(path, model="small.en")
    cpu_s = time.time() - t
    ref_text = " ".join(s["text"] for s in ref["segments"])
    results.append({"backend": "cpu", "seconds": round(cpu_s, 2), "realtime_factor": round((ref["duration_ms"] / 1000) / max(cpu_s, 1e-3), 2), "available": True, "agreement": 1.0})
    ov_dir = os.path.join(_state.get("models_dir", "."), "whisper-ov")
    for device in ("GPU", "NPU"):
        entry = {"backend": f"openvino:{device}", "available": False}
        try:
            import openvino_genai as ov  # type: ignore
            if not os.path.isdir(ov_dir):
                entry["reason"] = f"no exported model at {ov_dir} (optimum-cli export openvino --model openai/whisper-small.en {ov_dir})"
            else:
                pipe = ov.WhisperPipeline(ov_dir, device)
                import soundfile  # noqa
                t = time.time()
                audio, _sr = _load_16k(path)
                res = pipe.generate(audio)
                s = time.time() - t
                text = str(res)
                agree = _similarity(ref_text, text)
                entry.update({"available": True, "seconds": round(s, 2), "realtime_factor": round((ref["duration_ms"] / 1000) / max(s, 1e-3), 2), "agreement": round(agree, 3)})
        except Exception as exc:
            entry["reason"] = f"{exc.__class__.__name__}: {exc}"[:200]
        results.append(entry)
    # pick: fastest available with agreement >= 0.9 to the CPU reference
    best = max((r for r in results if r.get("available") and r.get("agreement", 0) >= 0.9), key=lambda r: r["realtime_factor"])
    _state["backend"] = best["backend"]
    return {"results": results, "chosen": best["backend"]}


def _load_16k(path: str):
    data = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", "16000", "-f", "f32le", "pipe:1"], capture_output=True).stdout
    return np.frombuffer(data, dtype=np.float32), 16000


def _similarity(a: str, b: str) -> float:
    import difflib
    return difflib.SequenceMatcher(None, a.lower().split(), b.lower().split()).ratio()


# ---------------------------------------------------------------- playback (mpv IPC)

class Player:
    def __init__(self, notify):
        # mpv's JSON IPC is a Unix socket on Linux/macOS; this worker doesn't yet speak the
        # Windows named-pipe form of --input-ipc-server, so playback stays Linux/macOS-only.
        self.sock_path = os.path.join(tempfile.gettempdir(), f"lumen-mpv-{os.getpid()}.sock")
        ao = os.environ.get("LUMEN_MPV_AO")
        extra = [f"--ao={ao}"] if ao else []
        self.proc = subprocess.Popen(["mpv", "--no-video", "--really-quiet", "--idle=yes", "--keep-open=yes", *extra, f"--input-ipc-server={self.sock_path}"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(self.sock_path):
                break
            time.sleep(0.05)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.sock_path)
        self.sock.settimeout(2.0)
        self.buf = b""
        self.notify = notify
        self.file = None

    def cmd(self, *args):
        try:
            self.sock.sendall((json.dumps({"command": list(args), "request_id": 7}) + "\n").encode())
        except OSError:
            return None
        while True:
            try:
                data = self.sock.recv(65536)
            except (socket.timeout, OSError):
                return None
            if not data:                       # mpv went away: EOF, not a timeout
                return None
            self.buf += data
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                try:
                    msg = json.loads(line)
                except Exception:
                    continue
                if msg.get("request_id") == 7:
                    return msg.get("data")

    def load(self, path: str, start_ms: int):
        # mpv ≥ 0.38 takes an index as loadfile's third argument, so options go through properties.
        self.file = path
        self.cmd("set_property", "pause", False)
        self.cmd("loadfile", path, "replace")
        for _ in range(40):                       # wait for the file to be playable, then seek
            if isinstance(self.cmd("get_property", "time-pos"), (int, float)):
                break
            time.sleep(0.05)
        if start_ms > 0:
            self.cmd("seek", start_ms / 1000, "absolute")

    def position_ms(self) -> int:
        p = self.cmd("get_property", "time-pos")
        return int(float(p) * 1000) if isinstance(p, (int, float)) else 0

    def playing(self) -> bool:
        p = self.cmd("get_property", "pause")
        eof = self.cmd("get_property", "eof-reached")
        return p is False and not eof

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.proc.kill()


def _player() -> Player:
    p = _state["player"]
    if p is not None and p.proc.poll() is not None:     # mpv exited: start a fresh one
        try:
            p.close()
        except Exception:
            pass
        _state["player"] = None
    if _state["player"] is None:
        _state["player"] = Player(_send)
    return _state["player"]


def play(path: str, start_ms: int = 0, **_: object) -> dict:
    p = _player()
    if p.file != path:
        p.load(path, start_ms)
    else:
        p.cmd("seek", start_ms / 1000, "absolute")
        p.cmd("set_property", "pause", False)
    if float(_state.get("speed", 1.0)) != 1.0:          # a new mpv starts at 1x
        p.cmd("set_property", "speed", float(_state["speed"]))
    return {"ok": True}


def pause(**_: object) -> dict:
    p = _player()
    p.cmd("set_property", "pause", True)
    return {"ok": True, "position_ms": p.position_ms()}


def seek(ms: int, **_: object) -> dict:
    p = _player()
    p.cmd("seek", ms / 1000, "absolute")
    return {"ok": True}


def position(**_: object) -> dict:
    if _state["player"] is None:
        return {"position_ms": 0, "playing": False}
    p = _player()
    return {"position_ms": p.position_ms(), "playing": p.playing()}


def playback_speed(speed: float = 1.0, **_: object) -> dict:
    """mpv keeps playing at the new rate; pitch stays sane because scaletempo is on by default."""
    p = _state["player"]
    if p is not None and p.proc.poll() is None:
        p.cmd("set_property", "speed", max(0.5, min(3.0, float(speed))))
    _state["speed"] = float(speed)
    return {"speed": float(speed)}


def stop_playback(**_: object) -> dict:
    if _state["player"] is not None:
        _state["player"].close()
        _state["player"] = None
    return {"ok": True}


def probe_duration(path: str, **_: object) -> dict:
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
    try:
        return {"duration_ms": int(float(out) * 1000)}
    except ValueError:
        return {"duration_ms": 0}


def serve_with_notify():
    global _send
    from . import rpc
    # This worker needs to push unsolicited notifications (level, segments, playback), which
    # rpc.serve()'s request/response loop doesn't support, so it drives the same connection itself.
    import argparse
    ap = argparse.ArgumentParser(); ap.add_argument("--socket", required=True); args = ap.parse_args()
    sock = rpc.connect(args.socket)
    lock = threading.Lock()

    def send(obj):
        with lock:
            sock.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode())

    def notify(method, params):
        send({"method": method, "params": params})
    _send = notify
    handlers = {"prepare": prepare, "sources": sources, "check": check, "monitor": monitor, "monitor_stop": monitor_stop, "start": start, "stop": stop, "transcribe_file": transcribe_file,
                "benchmark": benchmark, "play": play, "pause": pause, "seek": seek, "position": position,
                "stop_playback": stop_playback, "playback_speed": playback_speed, "probe_duration": probe_duration}
    send({"method": "hello", "params": {"worker": "audio", "python": sys.version.split()[0]}})
    buf = b""
    while True:
        chunk = sock.recv(1 << 16)
        if not chunk:
            return 0
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if not line.strip():
                continue
            try:
                req = json.loads(line)
            except json.JSONDecodeError:
                continue
            rid, method, params = req.get("id"), req.get("method"), req.get("params") or {}
            if method == "shutdown":
                stop_playback()
                rec = _state.get("rec")
                if rec is not None:
                    _state["rec"] = None
                    rec.stop(quick=True)
                if rid is not None:
                    send({"id": rid, "result": {"ok": True}})
                return 0
            h = handlers.get(method)
            if h is None:
                if rid is not None:
                    send({"id": rid, "error": {"code": -32601, "message": f"unknown method {method!r}"}})
                continue
            # long calls (transcribe_file, benchmark, prepare) run on a thread so playback/level keep flowing
            def run(h=h, rid=rid, params=params):
                try:
                    result = h(**params)
                except Exception as exc:
                    if rid is not None:
                        send({"id": rid, "error": {"code": -32000, "message": str(exc)}})
                    return
                if rid is not None:
                    send({"id": rid, "result": result if isinstance(result, dict) else {"value": result}})
            if method in ("transcribe_file", "benchmark", "prepare"):
                threading.Thread(target=run, daemon=True).start()
            else:
                run()


if __name__ == "__main__":
    sys.exit(serve_with_notify())
