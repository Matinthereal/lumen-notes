"""Claude worker (Phase 6, D-010): every AI feature goes through `claude -p` on the maker's own
plan. Text only — audio never leaves the machine. The app shows the exact prompt before it is sent
and logs every exchange."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time

from .rpc import Cancelled, check_cancelled, progress, serve


def _cli() -> list[str] | None:
    """The claude command, or the package's node wrapper when the native binary is missing
    (an auto-update on 2026-09-04 left the CLI in exactly that state)."""
    exe = shutil.which("claude")
    if exe:
        try:
            r = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=15)
            if r.returncode == 0 and "native binary" not in (r.stdout + r.stderr):
                return [exe]
        except Exception:
            pass
        real = os.path.realpath(exe)
        wrapper = os.path.join(os.path.dirname(os.path.dirname(real)), "cli-wrapper.cjs")
        node = shutil.which("node")
        if node and os.path.exists(wrapper):
            return [node, wrapper]
    return None


def status(**_: object) -> dict:
    cli = _cli()
    if not cli:
        return {"available": False, "reason": "Claude Code CLI not found or broken (run: npm install -g @anthropic-ai/claude-code)"}
    try:
        r = subprocess.run([*cli, "--version"], capture_output=True, text=True, timeout=30)
        v = (r.stdout or r.stderr).strip().splitlines()[0] if (r.stdout or r.stderr).strip() else ""
        if r.returncode != 0 or "native binary" in v:
            return {"available": False, "reason": v[:120] or "claude --version failed"}
    except Exception as exc:
        return {"available": False, "reason": f"claude --version failed: {exc}"}
    return {"available": True, "version": v + ("" if len(cli) == 1 else " (node wrapper)"), "path": " ".join(cli)}


def run(prompt: str, feature: str = "", allow_web: bool = False, allow_read: bool = False, max_turns: int = 4,
        timeout_s: int = 300, log_dir: str = "", **_: object) -> dict:
    cli = _cli()
    if not cli:
        return {"ok": False, "error": "Claude Code CLI not found"}
    tools = []
    if allow_web:
        tools += ["WebSearch", "WebFetch"]
    if allow_read:
        tools += ["Read"]
    cmd = [*cli, "-p", "--output-format", "json", "--max-turns", str(max_turns)]
    if tools:
        cmd += ["--allowedTools", ",".join(tools)]
    else:
        cmd += ["--disallowedTools", "Bash,Edit,Write,WebSearch,WebFetch"]
    t0 = time.time()
    progress(None, "asking", "waiting for Claude")
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         env={**os.environ, "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC": "1"})
    # Wait in short slices so a cancel from the app can stop the CLI rather than wait it out.
    out, err_text = "", ""
    pending = prompt
    while True:
        try:
            out, err_text = p.communicate(input=pending, timeout=0.25)
            break
        except subprocess.TimeoutExpired:
            pending = None
            try:
                check_cancelled()
            except Cancelled:
                p.kill()
                p.communicate()
                raise
            if time.time() - t0 > timeout_s:
                p.kill()
                p.communicate()
                return {"ok": False, "error": f"Claude took longer than {timeout_s} s"}
    p = subprocess.CompletedProcess(cmd, p.returncode, out, err_text)
    elapsed = round(time.time() - t0, 1)
    text, meta = "", {}
    ok = p.returncode == 0
    if p.stdout.strip():
        try:
            meta = json.loads(p.stdout)
            text = meta.get("result") or ""
            if meta.get("is_error"):
                ok = False
        except json.JSONDecodeError:
            text = p.stdout
    err = p.stderr.strip()
    if not ok and not err and not text:
        err = f"claude exited with {p.returncode}"
    if "log" in err.lower() and "in" in err.lower() and "auth" in err.lower() or "not logged" in err.lower() or "Please run /login" in err:
        err = "Claude Code is not logged in — run `claude` in a terminal once and sign in."
    if log_dir:
        os.makedirs(log_dir, exist_ok=True)
        path = os.path.join(log_dir, f"{time.strftime('%Y-%m-%d_%H%M%S')}-{feature or 'call'}.json")
        with open(path, "w") as f:
            json.dump({"feature": feature, "prompt": prompt, "response": text, "ok": ok, "error": err, "elapsed_s": elapsed,
                       "tools": tools, "cost_usd": meta.get("total_cost_usd"), "session_id": meta.get("session_id")}, f, indent=1)
    else:
        path = ""
    return {"ok": ok, "text": text, "error": err if not ok else "", "elapsed_s": elapsed, "log": path,
            "cost_usd": meta.get("total_cost_usd"), "session_id": meta.get("session_id")}


if __name__ == "__main__":
    sys.exit(serve("claude", {"status": status, "run": run}))
