"""Gemini CLI (Google account / AI Pro) — daily per-model request quota.

Uses the OAuth token from ~/.gemini/oauth_creds.json. Google refresh tokens are reusable, so if
[providers.gemini] client_id / client_secret are set in config.toml (gemini-cli's public OAuth
client) an expired access token is refreshed in memory — the file is never written.
"""
import json
import os
import time
import urllib.parse
import urllib.request

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch, to_float

QUOTA_URL = "https://cloudcode-pa.googleapis.com/v1internal:retrieveUserQuota"
LOAD_URL = "https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist"
TOKEN_URL = "https://oauth2.googleapis.com/token"
# OAuth client for token refresh: gemini-cli's own, see packages/core/src/code_assist/oauth2.ts
DEFAULT_CLIENT_ID = ""       # set client_id / client_secret in config.toml (kept out of the repo)
DEFAULT_CLIENT_SECRET = ""
_cache = {"token": None, "expires": 0.0}
_logged_shape = False


def _load_creds(path: str) -> dict:
    path = os.path.expanduser(path)
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        raise ProviderError("oauth_creds.json not found — log in with `gemini`", stale=True)
    except json.JSONDecodeError:
        raise ProviderError("oauth_creds.json unreadable", stale=True)


def _access_token(cfg: dict) -> str:
    creds = _load_creds(cfg.get("creds", "~/.gemini/oauth_creds.json"))
    expiry = float(creds.get("expiry_date", 0)) / 1000
    if expiry > time.time() + 60:
        return creds["access_token"]
    if _cache["token"] and _cache["expires"] > time.time() + 60:
        return _cache["token"]
    cid = cfg.get("client_id") or DEFAULT_CLIENT_ID
    secret = cfg.get("client_secret") or DEFAULT_CLIENT_SECRET
    if not cid or not secret:
        raise ProviderError("token expired - set client_id/client_secret in config.toml or run gemini", stale=True)
    refresh = creds.get("refresh_token")
    if not refresh:
        raise ProviderError("no refresh token — run gemini to log in", stale=True)
    form = urllib.parse.urlencode({"client_id": cid, "client_secret": secret,
                                   "refresh_token": refresh, "grant_type": "refresh_token"}).encode()
    req = urllib.request.Request(TOKEN_URL, data=form,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            tok = json.load(resp)
    except Exception as e:
        raise ProviderError(f"token refresh failed: {e}", stale=True)
    _cache["token"] = tok["access_token"]
    _cache["expires"] = time.time() + float(tok.get("expires_in", 3600))
    return _cache["token"]


def _project(cfg: dict, headers: dict) -> str:
    """gemini-cli calls loadCodeAssist first to learn the tier and its managed project id."""
    if cfg.get("project"):
        return cfg["project"]
    lca = get_json(LOAD_URL, method="POST", headers=headers, body={
        "metadata": {"ideType": "IDE_UNSPECIFIED", "platform": "PLATFORM_UNSPECIFIED", "pluginType": "GEMINI"}})
    proj = lca.get("cloudaicompanionProject")
    if proj:
        return proj
    reason = ""
    for t in lca.get("ineligibleTiers", []):
        if t.get("reasonCode"):
            reason = t["reasonCode"].replace("_", " ").lower()
            break
    raise ProviderError(f"no Gemini tier ({reason or 'no project'}) — update gemini CLI and log in", stale=True)


def fetch(cfg: dict) -> dict:
    global _logged_shape
    token = _access_token(cfg)
    headers = {"Authorization": f"Bearer {token}"}
    body = {"project": _project(cfg, headers)}
    data = get_json(QUOTA_URL, method="POST", body=body, headers=headers)
    if not _logged_shape:
        _logged_shape = True
        print("[gemini] response shape:", json.dumps(data)[:600])
    windows = []
    for b in data.get("buckets", []):
        frac = to_float(b.get("remainingFraction"), 1.0 if b.get("remainingAmount") is None else None)
        if frac is None:
            continue
        model = str(b.get("modelId", "daily")).replace("gemini-", "")
        windows.append({"label": model, "used_pct": round((1.0 - frac) * 100.0, 1),
                        "resets_at": iso_to_epoch(b.get("resetTime"))})
    if not windows:
        raise ProviderError(f"unexpected response keys: {list(data)[:6]}")
    # Prefer pro first, then flash, then the rest; keep at most 3 arcs.
    windows.sort(key=lambda w: (0 if "pro" in w["label"] else 1 if "flash" in w["label"] else 2, w["label"]))
    return {"kind": "windows", "plan": cfg.get("plan_label") or None, "windows": windows[:3]}
