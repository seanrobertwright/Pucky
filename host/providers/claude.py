"""Claude Code (Max/Pro subscription) — 5-hour and 7-day rate-limit windows.

Reads the access token Claude Code keeps in ~/.claude/.credentials.json and calls the same
usage endpoint the CLI uses. The token is NEVER refreshed here: Anthropic refresh tokens are
single-use, and refreshing from a second client disables the credential.
"""
import json
import os
import time

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"

# response key -> label shown on the dial (order matters: short window first)
WINDOWS = [
    ("five_hour", "5h"),
    ("seven_day", "7d"),
    ("seven_day_opus", "7d Opus"),
    ("seven_day_sonnet", "7d Sonnet"),
]


def _load_token(path: str) -> tuple[str, str | None]:
    path = os.path.expanduser(path)
    try:
        with open(path, encoding="utf-8") as f:
            oauth = json.load(f)["claudeAiOauth"]
    except FileNotFoundError:
        raise ProviderError("credentials file not found — log in with `claude`", stale=True)
    except (KeyError, json.JSONDecodeError):
        raise ProviderError("credentials file unreadable", stale=True)
    expires_ms = oauth.get("expiresAt", 0)
    if expires_ms and expires_ms / 1000 < time.time():
        raise ProviderError("token expired — run claude to refresh", stale=True)
    return oauth["accessToken"], oauth.get("subscriptionType")


def fetch(cfg: dict) -> dict:
    token, sub = _load_token(cfg.get("credentials", "~/.claude/.credentials.json"))
    data = get_json(USAGE_URL, headers={
        "Authorization": f"Bearer {token}",
        "anthropic-beta": "oauth-2025-04-20",
    })
    windows = []
    for key, label in WINDOWS:
        w = data.get(key)
        if not isinstance(w, dict) or w.get("utilization") is None:
            continue
        windows.append({
            "label": label,
            "used_pct": float(w["utilization"]),
            "resets_at": iso_to_epoch(w.get("resets_at")),
        })
    if not windows:
        raise ProviderError(f"unexpected response keys: {list(data)[:6]}")
    return {"kind": "windows", "plan": (sub or "").capitalize() or None, "windows": windows}
