"""Kiro (AWS) - monthly plan credits, bonus credits and overage.

Undocumented CodeWhisperer API, the same call Kiro IDE / kiro-cli make for their usage card:
  GET https://codewhisperer.us-east-1.amazonaws.com/getUsageLimits
      ?origin=AI_EDITOR&resourceType=AGENTIC_REQUEST&isEmailRequired=true&profileArn=<arn>
  Authorization: Bearer <accessToken>
  -> usageBreakdownList[]{resourceType ("CREDIT"), currentUsage, currentUsageWithPrecision,
                          usageLimit, usageLimitWithPrecision, unit, overageRate,
                          bonuses[]{displayName, currentUsage, usageLimit, expiresAt, status}}
     nextDateReset (epoch s), subscriptionInfo{subscriptionName}, userInfo{email}
  (Sources: Q Developer CLI's amzn-codewhisperer-client, kiro-reverse-api, CodexBar docs/kiro.md.)
Token: Kiro IDE's login in ~/.aws/sso/cache/kiro-auth-token.json {accessToken, refreshToken,
profileArn, expiresAt}. Read **only** - the IDE owns the refresh. When it has expired the
provider goes stale with "open Kiro to refresh" rather than refreshing behind the IDE's back.
Frankfurt-region profiles use https://q.eu-central-1.amazonaws.com (set base_url).
"""
import json
import os
import time
import urllib.parse

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch, to_float

DEFAULT_TOKEN = "~/.aws/sso/cache/kiro-auth-token.json"
DEFAULT_BASE = "https://codewhisperer.us-east-1.amazonaws.com"
_logged_shape = False


def _load_token(path: str) -> dict:
    path = os.path.expanduser(path)
    try:
        with open(path, encoding="utf-8") as f:
            tok = json.load(f)
    except FileNotFoundError:
        raise ProviderError("kiro-auth-token.json not found - sign in to Kiro", stale=True)
    except json.JSONDecodeError:
        raise ProviderError("kiro-auth-token.json unreadable", stale=True)
    exp = iso_to_epoch(tok.get("expiresAt"))
    if exp and exp < time.time():
        raise ProviderError("token expired - open Kiro to refresh", stale=True)
    if not tok.get("accessToken"):
        raise ProviderError("no accessToken in kiro-auth-token.json", stale=True)
    return tok


def _credit_breakdown(data: dict) -> dict | None:
    items = data.get("usageBreakdownList") or []
    for b in items:
        if str(b.get("resourceType", "")).upper() == "CREDIT":
            return b
    return items[0] if items else None


def fetch(cfg: dict) -> dict:
    global _logged_shape
    tok = _load_token(cfg.get("token_file", DEFAULT_TOKEN))
    base = cfg.get("base_url", DEFAULT_BASE).rstrip("/")
    q = {"origin": "AI_EDITOR", "resourceType": "AGENTIC_REQUEST", "isEmailRequired": "true"}
    arn = cfg.get("profile_arn") or tok.get("profileArn")
    if arn:
        q["profileArn"] = arn
    data = get_json(f"{base}/getUsageLimits?{urllib.parse.urlencode(q)}",
                    headers={"Authorization": f"Bearer {tok['accessToken']}", "Accept": "application/json"})
    if not _logged_shape:
        _logged_shape = True
        safe = {k: v for k, v in data.items() if k != "userInfo"}
        print("[kiro] response shape:", json.dumps(safe)[:700])

    credit = _credit_breakdown(data)
    if not credit:
        raise ProviderError(f"no usage breakdown in response: {sorted(data)[:8]}")
    used = to_float(credit.get("currentUsageWithPrecision"), to_float(credit.get("currentUsage"), 0.0))
    limit = to_float(credit.get("usageLimitWithPrecision"), to_float(credit.get("usageLimit"), 0.0))
    overage = to_float(credit.get("currentOverages"), 0.0)
    plan_used = max(0.0, used - overage)          # currentUsage includes overage credits
    resets_at = iso_to_epoch(data.get("nextDateReset"))
    windows = [{"label": "month",
                "used_pct": round(plan_used / limit * 100.0, 1) if limit else 0.0,
                "resets_at": resets_at}]
    for bonus in credit.get("bonuses") or []:
        b_used, b_limit = to_float(bonus.get("currentUsage"), 0.0), to_float(bonus.get("usageLimit"), 0.0)
        if not b_limit or str(bonus.get("status", "ACTIVE")).upper() not in ("ACTIVE", ""):
            continue
        windows.append({"label": "bonus", "used_pct": round(b_used / b_limit * 100.0, 1),
                        "resets_at": iso_to_epoch(bonus.get("expiresAt"))})
        break
    if overage:
        cap = to_float(credit.get("overageCapWithPrecision"), to_float(credit.get("overageCap"), 0.0))
        windows.append({"label": "overage", "used_pct": round(overage / cap * 100.0, 1) if cap else 100.0,
                        "resets_at": resets_at})

    sub = (data.get("subscriptionInfo") or {}).get("subscriptionName") or ""
    plan = sub.replace("KIRO", "").replace("_", " ").strip().title() or None
    if limit:
        plan = f"{plan} · {int(limit)} credits" if plan else f"{int(limit)} credits"
    return {"kind": "windows", "plan": plan, "windows": windows[:3]}
