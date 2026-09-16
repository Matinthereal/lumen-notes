"""Newline-delimited JSON RPC over a Unix socket. The app listens; the worker connects.

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


def serve(name: str, handlers: dict[str, Callable[..., object]], argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog=f"lumen_workers.{name}")
    ap.add_argument("--socket", required=True, help="Unix socket path the app is listening on")
    args = ap.parse_args(argv)

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(args.socket)

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
