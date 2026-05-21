#!/usr/bin/env python3
"""Claude Code PreToolUse hook → Clawdmeter.

Pushes the pending tool call to the Clawdmeter as a permission prompt,
then waits for an Allow / Deny tap and returns it as the hook's decision.
If the daemon isn't running or the device is out of range, falls back to
"ask" so Claude Code's normal terminal prompt fires — never blocks.

Hook input (stdin, per Claude Code spec):
    {"session_id", "tool_name", "tool_input", ...}

Hook output (stdout):
    {"hookSpecificOutput": {
        "hookEventName": "PreToolUse",
        "permissionDecision": "allow" | "deny" | "ask",
        "permissionDecisionReason": "..."}}
"""
from __future__ import annotations

import fnmatch
import json
import os
import socket
import sys
import time
from urllib.parse import urlparse

DAEMON_SOCK = os.environ.get("CLAWD_SOCK", "/tmp/clawdmeter.sock")
TAP_TIMEOUT = 130.0  # match the daemon's 120s + a little margin

SETTINGS_PATHS = [
    os.path.expanduser("~/.claude/settings.json"),
    os.path.expanduser("~/.claude/settings.local.json"),
    ".claude/settings.json",
    ".claude/settings.local.json",
]

ACCEPT_EDITS_TOOLS = {"Edit", "Write", "MultiEdit", "NotebookEdit", "Read"}


def _load_settings_field(getter):
    """Merge a field across every settings.json we can find. Project-local
    overrides global, matching Claude Code's own precedence."""
    out = None
    for p in SETTINGS_PATHS:
        try:
            with open(p) as f:
                doc = json.load(f)
            val = getter(doc)
            if val is not None:
                out = val
        except (FileNotFoundError, json.JSONDecodeError):
            pass
    return out


def load_allowlist() -> list[str]:
    acc: list[str] = []
    for p in SETTINGS_PATHS:
        try:
            with open(p) as f:
                doc = json.load(f)
            acc.extend(doc.get("permissions", {}).get("allow", []) or [])
        except (FileNotFoundError, json.JSONDecodeError):
            pass
    return acc


def load_default_mode() -> str | None:
    return _load_settings_field(lambda d: d.get("permissions", {}).get("defaultMode"))


def is_preapproved(tool: str, ti: dict, allowlist: list[str]) -> bool:
    """True if this call already matches a `permissions.allow` entry."""
    for pat in allowlist:
        if pat == tool:
            return True
        if not (pat.startswith(tool + "(") and pat.endswith(")")):
            continue
        inner = pat[len(tool) + 1 : -1]
        if tool == "Bash":
            cmd = str(ti.get("command", "")).strip()
            # Claude Code's `prefix:*` = prefix match, not glob.
            if inner.endswith(":*"):
                prefix = inner[:-2].strip()
                if cmd == prefix or cmd.startswith(prefix + " "):
                    return True
            elif cmd == inner:
                return True
        elif tool == "WebFetch":
            url = str(ti.get("url", ""))
            host = urlparse(url).hostname or ""
            if inner.startswith("domain:"):
                want = inner.split(":", 1)[1].strip()
                if host == want or host.endswith("." + want):
                    return True
            elif fnmatch.fnmatchcase(url, inner):
                return True
        else:
            for v in ti.values():
                if isinstance(v, str) and fnmatch.fnmatchcase(v, inner):
                    return True
    return False


def hint_for(tool: str, ti: dict) -> str:
    """ASCII-only single-line description, capped for the panel font."""
    if tool == "Bash":
        return str(ti.get("command", ""))[:140]
    if tool in ("Write", "Edit", "MultiEdit", "NotebookEdit"):
        return str(ti.get("file_path") or ti.get("notebook_path") or "")[:140]
    if tool == "WebFetch":
        return str(ti.get("url", ""))[:140]
    try:
        return json.dumps(ti, separators=(",", ":"))[:140]
    except Exception:
        return ""


def emit(decision: str, reason: str = "") -> None:
    json.dump({
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,
            "permissionDecisionReason": reason,
        }
    }, sys.stdout)
    sys.stdout.write("\n")
    sys.stdout.flush()


def ask_via_daemon(prompt_id: str, tool: str, hint: str) -> tuple[str | None, str]:
    """Returns (decision_or_None, reason_string). decision is 'allow' /
    'deny' on a tap; None on anything else (daemon down, BLE not linked,
    timeout, write fail)."""
    if not os.path.exists(DAEMON_SOCK):
        return None, "daemon socket missing"
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(TAP_TIMEOUT + 10)
        s.connect(DAEMON_SOCK)
        req = json.dumps({"prompt": {"id": prompt_id, "tool": tool, "hint": hint}}) + "\n"
        s.sendall(req.encode())
        buf = bytearray()
        while b"\n" not in buf:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf.extend(chunk)
        s.close()
        if not buf:
            return None, "daemon closed without reply"
        resp = json.loads(buf.split(b"\n", 1)[0].decode("utf-8", "replace"))
    except Exception as e:
        return None, f"daemon error: {e}"
    d = resp.get("decision")
    if d in ("allow", "deny"):
        return d, f"tapped on Clawdmeter ({d})"
    return None, f"daemon: {d or resp.get('err') or 'unknown'}"


def main() -> int:
    try:
        ev = json.load(sys.stdin)
    except Exception as e:
        print(f"[clawd hook] bad stdin: {e}", file=sys.stderr)
        emit("ask", "clawd hook: bad input")
        return 0

    tool = ev.get("tool_name", "?")
    ti = ev.get("tool_input", {}) or {}
    sid = (ev.get("session_id") or "")[:8]
    pid = f"cc-{sid}-{int(time.time() * 1000)}"
    hint = hint_for(tool, ti)

    if is_preapproved(tool, ti, load_allowlist()):
        emit("allow", "pre-approved in settings.json")
        return 0

    if load_default_mode() == "acceptEdits" and tool in ACCEPT_EDITS_TOOLS:
        emit("allow", "defaultMode=acceptEdits")
        return 0

    decision, reason = ask_via_daemon(pid, tool, hint)
    if decision == "allow":
        emit("allow", reason)
    elif decision == "deny":
        emit("deny", reason)
    else:
        emit("ask", f"clawd unreachable, falling back: {reason}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
