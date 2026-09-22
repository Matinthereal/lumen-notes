"""Newline-delimited JSON RPC over the app's QLocalServer endpoint. The app listens; the worker
connects. That's a Unix socket on Linux/macOS and a named pipe on Windows — connect() below picks
the one QLocalServer actually opened, so the rest of this module never has to know which.

Request:  {"id": 1, "method": "ping", "params": {...}}      (id absent = notification)
Response: {"id": 1, "result": {...}}  or  {"id": 1, "error": {"code": -32000, "message": "..."}}
Worker → app notifications: {"method": "hello", "params": {...}} (no id).
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import traceback
from typing import Callable


class _PipeTransport:
    """Adapts a Windows named-pipe handle to the .sendall()/.recv() shape used below, so callers
    don't need to special-case the transport."""

    def __init__(self, path: str) -> None:
        self._f = open(path, "r+b", buffering=0)

    def sendall(self, data: bytes) -> None:
        self._f.write(data)

    def recv(self, n: int) -> bytes:
        return self._f.read(n) or b""

    def close(self) -> None:
        self._f.close()

    def settimeout(self, _timeout: float) -> None:
        # A synchronous named-pipe handle has no per-call read timeout without overlapped I/O;
        # callers that set one (e.g. audio.py's mpv IPC) just get a blocking read instead.
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


def serve(name: str, handlers: dict[str, Callable[..., object]], argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog=f"lumen_workers.{name}")
    ap.add_argument("--socket", required=True, help="IPC endpoint the app is listening on")
    args = ap.parse_args(argv)

    sock = connect(args.socket)

    def send(obj: dict) -> None:
        sock.sendall((json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8"))

    send({"method": "hello", "params": {"worker": name, "python": sys.version.split()[0]}})

    buf = b""
    while True:
        chunk = sock.recv(1 << 16)
        if not chunk:
            return 0
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
            rid = req.get("id")
            method = req.get("method")
            params = req.get("params") or {}
            if method == "shutdown":
                if rid is not None:
                    send({"id": rid, "result": {"ok": True}})
                return 0
            handler = handlers.get(method)
            if handler is None:
                if rid is not None:
                    send({"id": rid, "error": {"code": -32601, "message": f"unknown method {method!r}"}})
                continue
            try:
                result = handler(**params)
            except Exception as exc:  # a bad request must never kill the worker
                if rid is not None:
                    send({"id": rid, "error": {"code": -32000, "message": str(exc), "trace": traceback.format_exc()}})
                continue
            if rid is not None:
                send({"id": rid, "result": result if isinstance(result, dict) else {"value": result}})
