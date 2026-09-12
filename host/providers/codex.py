"""Codex (ChatGPT subscription) — 5-hour primary and weekly secondary windows.

Uses the token from ~/.codex/auth.json read-only (same single-use-refresh caution as Claude)
against the endpoint the CLI itself polls. Response field names are parsed defensively because
the endpoint is undocumented; the raw shape is logged once so it can be pinned down.
"""
import json
import os
import time

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch, to_float

USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
PLAN_LABELS = {"plus": "Plus", "pro": "Pro", "prolite": "Pro Lite", "team": "Team",
               "business": "Business", "enterprise": "Enterprise", "free": "Free"}
_logged_shape = False


def _load_auth(path: str) -> tuple[str, str]:
    path = os.path.expanduser(path)
    try:
        with open(path, encoding="utf-8") as f:
            tokens = json.load(f)["tokens"]
    except FileNotFoundError:
        raise ProviderError("auth file not found — log in with `codex`", stale=True)
    except (KeyError, json.JSONDecodeError):
        raise ProviderError("auth file unreadable", stale=True)
    return tokens["access_token"], tokens.get("account_id", "")


def _window(obj: dict, label: str) -> dict | None:
    if not isinstance(obj, dict):
        return None
    pct = to_float(obj.get("used_percent", obj.get("utilization")))
    if pct is None:
        return None
    resets = iso_to_epoch(obj.get("reset_at", obj.get("resets_at")))
    if resets is None and obj.get("reset_after_seconds") is not None:
        resets = int(time.time() + float(obj["reset_after_seconds"]))
    secs = obj.get("limit_window_seconds") or obj.get("window_seconds") or obj.get("window_minutes", 0) * 60
    if secs:
        hours = int(secs) // 3600
        label = f"{hours}h" if hours < 48 else f"{hours // 24}d"
    return {"label": label, "used_pct": pct, "resets_at": resets}


def fetch(cfg: dict) -> dict:
    global _logged_shape
    token, account = _load_auth(cfg.get("auth", "~/.codex/auth.json"))
    data = get_json(USAGE_URL, headers={
        "Authorization": f"Bearer {token}",
        "ChatGPT-Account-ID": account,
    })
    if not _logged_shape:
        _logged_shape = True
        print("[codex] response keys:", sorted(data)[:12])
    rl = data.get("rate_limit") or data.get("rate_limits") or data
    windows = []
    for key, label in (("primary_window", "5h"), ("primary", "5h"),
                       ("secondary_window", "7d"), ("secondary", "7d")):
        w = _window(rl.get(key), label)
        if w and w["label"] not in {x["label"] for x in windows}:
            windows.append(w)
    if not windows:
        raise ProviderError(f"unexpected response keys: {list(data)[:6]}")
    plan = data.get("plan_type") or data.get("plan")
    plan = PLAN_LABELS.get(str(plan).lower(), str(plan).capitalize()) if plan else None
    return {"kind": "windows", "plan": plan, "windows": windows}
