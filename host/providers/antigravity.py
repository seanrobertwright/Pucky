"""Antigravity CLI (agy) / Antigravity IDE - Google AI Pro quota pools (5-hour windows per pool).

Google retired Gemini CLI in June 2026; agy replaced it and uses the same Cloud Code backend, so
this adapter supersedes gemini.py. Confirmed live 2026-09-12 against agy 1.2.2:
  token   agy keeps its OAuth token in the OS keyring. Windows: Credential Manager generic entry
          "gemini:antigravity", blob = JSON {"token": {access_token, refresh_token, expiry}, ...}.
          Read-only. Google refresh tokens are reusable, so an expired access token is refreshed
          in memory with Antigravity's public OAuth client (never written back).
  POST https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist
          body {"metadata": {"ideType": "ANTIGRAVITY", "platform": "PLATFORM_UNSPECIFIED", "pluginType": "GEMINI"}}
          -> cloudaicompanionProject, currentTier{id,name}, paidTier{id,name}
  POST .../v1internal:fetchAvailableModels  body {"project": <that>}
          -> models{<id>: {displayName, quotaInfo{remainingFraction, resetTime, isExhausted}}}
Models sharing the same (remainingFraction, resetTime) are one quota pool; observed pools are
"all Gemini models" and "Claude + GPT-OSS". Each pool becomes one arc.
Sources: agy cli.log, github.com/skainguyen1412/antigravity-usage (endpoints + OAuth client).
"""
import json
import os
import sys
import time
import urllib.parse
import urllib.request

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch, to_float

BASES = ("https://cloudcode-pa.googleapis.com", "https://daily-cloudcode-pa.googleapis.com")
TOKEN_URL = "https://oauth2.googleapis.com/token"
KEYRING_TARGET = "gemini:antigravity"
# OAuth client for token refresh: Antigravity's own, see antigravity-usage/src/google/oauth.ts
DEFAULT_CLIENT_ID = ""       # set client_id / client_secret in config.toml (kept out of the repo)
DEFAULT_CLIENT_SECRET = ""
HEADERS = {"Content-Type": "application/json", "User-Agent": "antigravity"}
SKIP = ("chat_", "tab_", "image", "mquery", "lite")      # internal / non-agent models
_cache = {"token": None, "expires": 0.0}
_logged_shape = False


# ---------------------------------------------------------------- credentials
def _read_windows_keyring(target: str) -> bytes:
    import ctypes
    import ctypes.wintypes as w

    class CREDENTIAL(ctypes.Structure):
        _fields_ = [("Flags", w.DWORD), ("Type", w.DWORD), ("TargetName", w.LPWSTR), ("Comment", w.LPWSTR),
                    ("LastWritten", w.FILETIME), ("CredentialBlobSize", w.DWORD),
                    ("CredentialBlob", ctypes.POINTER(ctypes.c_byte)), ("Persist", w.DWORD),
                    ("AttributeCount", w.DWORD), ("Attributes", ctypes.c_void_p),
                    ("TargetAlias", w.LPWSTR), ("UserName", w.LPWSTR)]
    adv = ctypes.windll.advapi32
    p = ctypes.POINTER(CREDENTIAL)()
    if not adv.CredReadW(target, 1, 0, ctypes.byref(p)):          # 1 = CRED_TYPE_GENERIC
        raise ProviderError("not logged in - run agy and sign in", stale=True)
    try:
        return ctypes.string_at(p.contents.CredentialBlob, p.contents.CredentialBlobSize)
    finally:
        adv.CredFree(p)


def _load_token(cfg: dict) -> dict:
    """Returns agy's token dict {access_token, refresh_token, expiry}."""
    path = cfg.get("token_file")
    if path:                                   # non-Windows or manual export: same JSON as the keyring blob
        try:
            with open(os.path.expanduser(path), encoding="utf-8") as f:
                raw = f.read().encode()
        except FileNotFoundError:
            raise ProviderError("token_file not found - run agy and sign in", stale=True)
    elif sys.platform == "win32":
        raw = _read_windows_keyring(cfg.get("keyring_target", KEYRING_TARGET))
    else:
        raise ProviderError("set token_file in config.toml (keyring read is Windows-only)", stale=True)
    try:
        blob = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise ProviderError("stored token unreadable - run agy and sign in", stale=True)
    tok = blob.get("token") or blob
    if not tok.get("access_token"):
        raise ProviderError("stored token has no access_token - run agy and sign in", stale=True)
    return tok


def _access_token(cfg: dict) -> str:
    tok = _load_token(cfg)
    expiry = iso_to_epoch(tok.get("expiry")) or 0
    if expiry > time.time() + 60:
        return tok["access_token"]
    if _cache["token"] and _cache["expires"] > time.time() + 60:
        return _cache["token"]
    refresh = tok.get("refresh_token")
    if not refresh:
        raise ProviderError("token expired and no refresh token - run agy and sign in", stale=True)
    cid = cfg.get("client_id") or DEFAULT_CLIENT_ID
    secret = cfg.get("client_secret") or DEFAULT_CLIENT_SECRET
    if not cid or not secret:
        raise ProviderError("token expired - set client_id/client_secret in config.toml or run agy", stale=True)
    form = urllib.parse.urlencode({"client_id": cid, "client_secret": secret,
                                   "refresh_token": refresh, "grant_type": "refresh_token"}).encode()
    req = urllib.request.Request(TOKEN_URL, data=form, headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            new = json.load(resp)
    except Exception as e:
        raise ProviderError(f"token refresh failed: {e} - run agy and sign in", stale=True)
    _cache["token"] = new["access_token"]
    _cache["expires"] = time.time() + float(new.get("expires_in", 3600))
    return _cache["token"]


# ---------------------------------------------------------------- api
def _post(path: str, body: dict, token: str) -> dict:
    last = None
    for base in BASES:
        try:
            return get_json(f"{base}/v1internal:{path}", method="POST", body=body,
                            headers={**HEADERS, "Authorization": f"Bearer {token}"})
        except ProviderError as e:
            last = e
            if e.stale or e.retry_after:        # auth / rate-limit problems are not host-specific
                raise
    raise last


def _pool_label(model_ids: list) -> str:
    fams = set()
    for mid in model_ids:
        m = mid.lower()
        fams.add("Gemini" if m.startswith("gemini") else "Claude" if m.startswith("claude") else
                 "GPT" if "gpt" in m else mid.split("-")[0].title())
    order = ["Gemini", "Claude", "GPT"]
    fams = sorted(fams, key=lambda f: (order.index(f) if f in order else 9, f))
    return "+".join(fams[:2]) + ("+" if len(fams) > 2 else "")


def fetch(cfg: dict) -> dict:
    global _logged_shape
    token = _access_token(cfg)
    lca = _post("loadCodeAssist", {"metadata": {"ideType": "ANTIGRAVITY", "platform": "PLATFORM_UNSPECIFIED",
                                                "pluginType": "GEMINI"}}, token)
    project = lca.get("cloudaicompanionProject")
    if isinstance(project, dict):
        project = project.get("id")
    tier = lca.get("paidTier") or lca.get("currentTier") or {}
    plan = cfg.get("plan_label") or (tier.get("name") or "").replace("Google ", "") or None

    fam = _post("fetchAvailableModels", {"project": project} if project else {}, token)
    models = fam.get("models") or {}
    if not _logged_shape:
        _logged_shape = True
        print("[antigravity] tier:", json.dumps({k: v for k, v in tier.items() if k in ("id", "name")}),
              "models:", len(models))
    pools = {}
    for mid, m in models.items():
        if not isinstance(m, dict) or any(s in mid.lower() for s in SKIP):
            continue
        q = m.get("quotaInfo")
        if not isinstance(q, dict) or q.get("resetTime") is None:
            continue                            # no reset time = unmetered internal model
        frac = to_float(q.get("remainingFraction"), 0.0 if q.get("isExhausted") else None)
        if frac is None:
            continue
        pools.setdefault((round(frac, 4), q["resetTime"]), []).append(mid)
    if not pools:
        raise ProviderError(f"no quota info in response keys: {list(fam)[:6]}")
    windows = [{"label": _pool_label(ids), "used_pct": round((1.0 - frac) * 100.0, 1),
                "resets_at": iso_to_epoch(reset)} for (frac, reset), ids in pools.items()]
    # Gemini pool first, then by most-used; keep at most 3 arcs
    windows.sort(key=lambda w: (0 if w["label"].startswith("Gemini") else 1, -w["used_pct"]))
    return {"kind": "windows", "plan": plan, "windows": windows[:3]}
