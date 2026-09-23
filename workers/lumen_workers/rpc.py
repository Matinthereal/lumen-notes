"""Newline-delimited JSON RPC over the app's QLocalServer endpoint. The app listens; the worker
connects. That's a Unix socket on Linux/macOS and a named pipe on Windows — connect() below picks
the one QLocalServer actually opened, so the rest of this module never has to know which.

Request:  {"id": 1, "method": "ping", "params": {...}}      (id absent = notification)
Response: {"id": 1, "result": {...}}  or  {"id": 1, "error": {"code": -32000, "message": "..."}}
Worker → app notifications: {"method": "hello", "params": {...}} (no id),
                            {"method": "progress", "params": {"id", "stage", "fraction", "text"}}.
App → worker notification:  {"method": "cancel", "params": {"id": 1}} — honoured between steps by
                            handlers that call check_cancelled(); answered with code -32800.

Requests still run one at a time on the main thread, as before. The socket is read on a thread of
its own so that a cancel can arrive while a long request is running.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import queue
import socket
import sys
import threading
import traceback
from typing import Callable, Iterable, Optional

CANCELLED = -32800

_send_lock = threading.Lock()
_sender: Optional[Callable[[dict], None]] = None
_cancelled: set = set()
_local = threading.local()          # .rid: the request the current thread is serving


class _PipeTransport:
    """A Windows named pipe with the .sendall()/.recv() shape of a socket, so callers don't need to
    special-case the transport. It is opened for overlapped I/O: a synchronous pipe handle serialises
    its calls, so a thread blocked in recv() would hold every send until the app next wrote."""

    def __init__(self, path: str) -> None:
        import _winapi
        self._winapi = _winapi
        self._h = _winapi.CreateFile(path, _winapi.GENERIC_READ | _winapi.GENERIC_WRITE, 0, _winapi.NULL,
                                     _winapi.OPEN_EXISTING, _winapi.FILE_FLAG_OVERLAPPED, _winapi.NULL)

    def sendall(self, data: bytes) -> None:
        ov, _ = self._winapi.WriteFile(self._h, data, overlapped=True)
        written, err = ov.GetOverlappedResult(True)
        if err or written != len(data):
            raise BrokenPipeError(f"pipe write failed ({err})")

    def recv(self, n: int) -> bytes:
        try:
            ov, _ = self._winapi.ReadFile(self._h, n, overlapped=True)
            _, err = ov.GetOverlappedResult(True)
        except OSError:
            return b""
        if err not in (0, self._winapi.ERROR_MORE_DATA):   # ERROR_BROKEN_PIPE: the app went away
            return b""
        return bytes(ov.getbuffer())

    def close(self) -> None:
        self._winapi.CloseHandle(self._h)

    def settimeout(self, _timeout: float) -> None:
        # No per-call read timeout here; callers that set one (e.g. audio.py's mpv IPC) just get a
        # blocking read instead.
        pass


def connect(path: str):
    """Connect to the app's IPC endpoint. QLocalServer::listen(name) opens a Unix socket at `name`
    on Linux/macOS and a named pipe at \\\\.\\pipe\\<name> on Windows."""
    if sys.platform == "win32":
        pipe = path if path.startswith("\\\\.\\pipe\\") else "\\\\.\\pipe\\" + path
        return _PipeTransport(pipe)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(path)
    return sock


class Cancelled(Exception):
    """Raised by check_cancelled(); the serve loop turns it into a CANCELLED error."""


def set_sender(send: Callable[[dict], None]) -> None:
    """For workers that run their own loop (audio): where progress() writes to."""
    global _sender
    _sender = send


def bind(rid) -> None:
    """Mark the current thread as serving request `rid`, for progress() and check_cancelled()."""
    _local.rid = rid


def cancel(rid) -> None:
    if rid is not None:
        _cancelled.add(rid)


def progress(fraction: Optional[float] = None, stage: str = "", text: str = "") -> None:
    """Tell the app how the current request is going. fraction None = no idea how far along."""
    rid = getattr(_local, "rid", None)
    if rid is None or _sender is None:
        return
    params = {"id": rid, "stage": stage, "text": text}
    if fraction is not None:
        params["fraction"] = max(0.0, min(1.0, float(fraction)))
    _sender({"method": "progress", "params": params})


def check_cancelled() -> None:
    rid = getattr(_local, "rid", None)
    if rid is not None and rid in _cancelled:
        _cancelled.discard(rid)
        raise Cancelled()


def missing(modules: Iterable[str]) -> list[str]:
    """The modules that are not installed, without importing any of them."""
    out = []
    for m in modules:
        try:
            if importlib.util.find_spec(m) is None:
                out.append(m)
        except (ImportError, ValueError):
            out.append(m)
    return out


def hello(name: str, requires: Iterable[str] = (), optional: Iterable[str] = ()) -> dict:
    return {"method": "hello", "params": {"worker": name, "python": sys.version.split()[0],
                                          "missing": missing(requires), "missing_optional": missing(optional)}}


def answer(rid, handler: Callable[..., object], params: dict) -> Optional[dict]:
    """Run one request and build its response (None for a notification)."""
    bind(rid)
    try:
        result = handler(**params)
    except Cancelled:
        return None if rid is None else {"id": rid, "error": {"code": CANCELLED, "message": "cancelled"}}
    except Exception as exc:  # a bad request must never kill the worker
        return None if rid is None else {"id": rid, "error": {"code": -32000, "message": str(exc), "trace": traceback.format_exc()}}
    finally:
        _cancelled.discard(rid)
        bind(None)
    return None if rid is None else {"id": rid, "result": result if isinstance(result, dict) else {"value": result}}


def serve(name: str, handlers: dict[str, Callable[..., object]], argv: list[str] | None = None,
          requires: Iterable[str] = (), optional: Iterable[str] = ()) -> int:
    ap = argparse.ArgumentParser(prog=f"lumen_workers.{name}")
    ap.add_argument("--socket", required=True, help="IPC endpoint the app is listening on")
    args = ap.parse_args(argv)

    sock = connect(args.socket)

    def send(obj: dict) -> None:
        with _send_lock:
            sock.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))

    set_sender(send)
    send(hello(name, requires, optional))

    inbox: queue.Queue = queue.Queue()

    def read() -> None:
        buf = b""
        while True:
            try:
                chunk = sock.recv(1 << 16)
            except OSError:
                chunk = b""
            if not chunk:
                inbox.put(None)
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    req = json.loads(line)
                except json.JSONDecodeError:
                    print(f"{name}: dropped unparseable line", file=sys.stderr)
                    continue
                if req.get("method") == "cancel":
                    cancel((req.get("params") or {}).get("id"))
                    continue
                inbox.put(req)

    threading.Thread(target=read, daemon=True).start()

    while True:
        req = inbox.get()
        if req is None:
            return 0
        rid = req.get("id")
        method = req.get("method")
        params = req.get("params") or {}
        if method == "shutdown":
            if rid is not None:
                send({"id": rid, "result": {"ok": True}})
            return 0
        if rid is not None and rid in _cancelled:         # cancelled before it started
            _cancelled.discard(rid)
            send({"id": rid, "error": {"code": CANCELLED, "message": "cancelled"}})
            continue
        handler = handlers.get(method)
        if handler is None:
            if rid is not None:
                send({"id": rid, "error": {"code": -32601, "message": f"unknown method {method!r}"}})
            continue
        reply = answer(rid, handler, params)
        if reply is not None:
            send(reply)
