"""The Phase 0 worker: proves the app ⇄ Python path, and gives the tests a process to crash."""
from __future__ import annotations

import os
import sys
import time

from .rpc import serve


def ping(**_: object) -> dict:
    return {"pong": True, "t": time.time(), "python": sys.version.split()[0], "pid": os.getpid()}


def echo(**params: object) -> dict:
    return dict(params)


def crash(**_: object) -> dict:
    os._exit(3)  # simulate a segfaulting model library: the supervisor must restart us


if __name__ == "__main__":
    sys.exit(serve("ping", {"ping": ping, "echo": echo, "crash": crash}))
