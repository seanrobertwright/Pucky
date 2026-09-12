"""GitHub Copilot (Pro / Pro+ / Business seat) - monthly premium-request allowance.

Undocumented: GET https://api.github.com/copilot_internal/user, the endpoint Copilot's own
editor extensions poll. Confirmed 2026-09-12 with a plain `gh` OAuth token:
  quota_snapshots.premium_interactions {entitlement, remaining, credits_used, percent_remaining,
                                        unlimited, overage_count, overage_permitted}
  quota_snapshots.chat / .completions   (unlimited on paid plans)
  copilot_plan ("individual", "business", ...), quota_reset_date "YYYY-MM-DD" (start of next month)
Token: the GitHub CLI's login (`gh auth token`) unless config.toml gives a `token`. Read-only.
"""
import subprocess
from datetime import datetime, timezone

from . import ProviderError
from .http import get_json
from .util import to_float

USER_URL = "https://api.github.com/copilot_internal/user"
PLAN_LABELS = {"individual": "Pro", "individual_pro_plus": "Pro+", "business": "Business",
               "enterprise": "Enterprise", "free": "Free"}
_token_cache = {"token": None}


def _token(cfg: dict) -> str:
    if cfg.get("token"):
        return cfg["token"]
    if _token_cache["token"]:
        return _token_cache["token"]
    gh = cfg.get("gh", "gh")
    try:
        out = subprocess.run([gh, "auth", "token"], capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise ProviderError(f"gh not runnable ({e}); set token in config.toml", stale=True)
    tok = out.stdout.strip()
    if out.returncode != 0 or not tok:
        raise ProviderError("not logged in - run `gh auth login`", stale=True)
    _token_cache["token"] = tok
    return tok


def fetch(cfg: dict) -> dict:
    token = _token(cfg)
    try:
        data = get_json(USER_URL, headers={"Authorization": f"Bearer {token}",
                                           "Accept": "application/json",
                                           "X-GitHub-Api-Version": "2022-11-28"})
    except ProviderError as e:
        if e.stale:                       # 401/403: token expired or revoked -> re-read from gh next time
            _token_cache["token"] = None
        raise
    snaps = data.get("quota_snapshots") or {}
    prem = snaps.get("premium_interactions")
    if not isinstance(prem, dict):
        raise ProviderError(f"no premium_interactions in response: {sorted(data)[:8]}")

    ent = to_float(prem.get("entitlement"), 0.0)
    used = to_float(prem.get("credits_used"))
    remaining = to_float(prem.get("remaining"))
    if used is None and remaining is not None:
        used = ent - remaining
    if prem.get("unlimited"):
        pct = 0.0
    elif ent > 0 and used is not None:
        pct = used / ent * 100.0
    else:
        pct = 100.0 - to_float(prem.get("percent_remaining"), 100.0)
    if prem.get("overage_count"):
        pct = max(pct, 100.0)

    resets_at = None
    reset = data.get("quota_reset_date_utc") or data.get("quota_reset_date")
    if reset:
        try:
            dt = datetime.fromisoformat(str(reset).replace("Z", "+00:00"))
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            resets_at = int(dt.timestamp())
        except ValueError:
            resets_at = None

    plan = str(data.get("copilot_plan") or "")
    plan_label = PLAN_LABELS.get(plan, plan.replace("_", " ").title() or None)
    if ent:
        plan_label = f"{plan_label} · {int(ent)} premium/mo" if plan_label else f"{int(ent)} premium/mo"
    return {"kind": "windows", "plan": plan_label,
            "windows": [{"label": "month", "used_pct": round(max(0.0, min(pct, 100.0)), 1), "resets_at": resets_at}],
            "requests_30d": int(used) if used is not None else None}
