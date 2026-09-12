"""OpenRouter - prepaid credits. Documented: GET /api/v1/credits (account-wide totals).

Optional: a *management* key (openrouter.ai/settings -> Management keys) in config.toml unlocks
GET /api/v1/activity - per-model daily rows for the last 30 completed UTC days - which gives an
exact 30-day spend plus request/token counts. Without it, 30-day spend is derived by status.py
from daily total_usage snapshots and requests/tokens are reported as null.
"""
from . import ProviderError
from .http import get_json
from .util import to_float

CREDITS_URL = "https://openrouter.ai/api/v1/credits"
ACTIVITY_URL = "https://openrouter.ai/api/v1/activity"


def _activity_30d(mgmt_key: str) -> dict:
    try:
        rows = get_json(ACTIVITY_URL, headers={"Authorization": f"Bearer {mgmt_key}"}).get("data") or []
    except ProviderError as e:
        print(f"[openrouter] activity failed: {e}")
        return {}
    spend = requests = tokens = 0.0
    for r in rows:
        if not isinstance(r, dict):
            continue
        spend += to_float(r.get("usage"), 0.0) + to_float(r.get("byok_usage_inference"), 0.0)
        requests += to_float(r.get("requests"), 0.0)
        tokens += sum(to_float(r.get(k), 0.0) for k in ("prompt_tokens", "completion_tokens", "reasoning_tokens"))
    return {"spend_30d_usd": round(spend, 4), "requests_30d": int(requests), "tokens_30d": int(tokens)}


def fetch(cfg: dict) -> dict:
    key = cfg.get("api_key", "")
    if not key:
        raise ProviderError("api_key not set in config.toml")
    data = get_json(CREDITS_URL, headers={"Authorization": f"Bearer {key}"}).get("data", {})
    total = to_float(data.get("total_credits"))
    used = to_float(data.get("total_usage"))
    if total is None or used is None:
        raise ProviderError(f"unexpected response: {data}")
    money = {
        "remaining_usd": round(total - used, 4),
        "total_usd": round(total, 2),
        "spent_total_usd": round(used, 4),   # lifetime; status.py derives 30-day spend from snapshots
        "requests_30d": None,
        "tokens_30d": None,
    }
    mgmt = cfg.get("management_key", "")
    if mgmt:
        money.update(_activity_30d(mgmt))
    return {"kind": "money", "money": money}
