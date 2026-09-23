"""The Phase 0 worker: proves the app ⇄ Python path, and gives the tests a process to crash."""
from __future__ import annotations

import importlib
import os
import sys
import time

from .rpc import check_cancelled, progress, serve

# Test hooks for the supervisor: a worker whose import fails, and one that reports a missing package.
if os.environ.get("LUMEN_PING_IMPORT"):
    importlib.import_module(os.environ["LUMEN_PING_IMPORT"])


def ping(**_: object) -> dict:
    return {"pong": True, "t": time.time(), "python": sys.version.split()[0], "pid": os.getpid()}


def echo(**params: object) -> dict:
    return dict(params)


def slow(steps: int = 10, delay: float = 0.1, **_: object) -> dict:
    """A long request that reports progress and can be cancelled between steps."""
    for i in range(steps):
        check_cancelled()
        progress(i / steps, "working", f"step {i + 1} of {steps}")
        time.sleep(delay)
    return {"steps": steps}


def crash(**_: object) -> dict:
    os._exit(3)  # simulate a segfaulting model library: the supervisor must restart us


if __name__ == "__main__":
    sys.exit(serve("ping", {"ping": ping, "echo": echo, "slow": slow, "crash": crash},
                   requires=[m for m in os.environ.get("LUMEN_PING_REQUIRES", "").split(",") if m]))
