"""z.AI GLM Coding Plan - 5-hour and weekly quota windows (+ monthly MCP tool quota).

Undocumented monitor API, the same one the z.ai usage page calls. Auth is the raw API key in
Authorization (no "Bearer" prefix). Endpoints (all under <base_url>/api/monitor/usage/):
  quota/limit                      -> data.limits[]{type, unit, number, percentage, nextResetTime}
                                      type CREDIT_LIMIT / TOKENS_LIMIT: unit 3 = hours, 6 = weeks
                                      (observed: 5h window and 1-week window); TIME_LIMIT = monthly MCP
  model-usage?startTime=&endTime=  -> data.totalUsage{totalModelCallCount, totalTokensUsage}
                                      times are "yyyy-MM-dd HH:mm:ss" (local); daily buckets over 30d
The usage call is best-effort: if it fails the quota windows are still reported.
"""
import json
import urllib.parse
from datetime import datetime, timedelta

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch, to_float

_logged_shape = False
TIME_FMT = "%Y-%m-%d %H:%M:%S"


def _label(limit: dict) -> str:
    t = str(limit.get("type", "")).upper()
    if "TIME" in t:                       # monthly MCP/tool-call allowance
        return "MCP/mo"
    unit, number = limit.get("unit"), to_float(limit.get("number"))
    if number:
        n = int(number)
        if unit in (3, "HOUR"):
            return f"{n}h"
        if unit in (5, "DAY"):
            return f"{n}d"
        if unit in (6, "WEEK"):
            return f"{n * 7}d"
        if unit in (7, "MONTH"):
            return f"{n}mo"
    return "5h" if "TOKEN" in t or "CREDIT" in t else (t.title() or "quota")


def _hours(label: str) -> float:
    n = float("".join(c for c in label if c.isdigit()) or 0)
    return n * (24 if label.endswith("d") else 720 if label.endswith("mo") else 1)


def _windows(payload: dict) -> list:
    limits = payload.get("limits") if isinstance(payload, dict) else None
    if not limits:
        raise ProviderError(f"unexpected response: {str(payload)[:120]}")
    windows = []
    for lim in limits:
        pct = to_float(lim.get("percentage"))
        if pct is None:
            used, total = to_float(lim.get("currentValue")), to_float(lim.get("usage"))
            if used is None or not total:
                continue
            pct = used / total * 100.0
        windows.append({"label": _label(lim), "used_pct": round(max(0.0, min(100.0, pct)), 1),
                        "resets_at": iso_to_epoch(lim.get("nextResetTime"))})
    if not windows:
        raise ProviderError("no usable limits in response")
    # short window first, then longer ones; MCP last
    windows.sort(key=lambda w: (w["label"].startswith("MCP"), _hours(w["label"])))
    return windows[:3]


def _usage_30d(base: str, headers: dict) -> tuple:
    end = datetime.now()
    start = end - timedelta(days=30)
    q = urllib.parse.urlencode({"startTime": start.strftime(TIME_FMT), "endTime": end.strftime(TIME_FMT)})
    try:
        data = get_json(f"{base}/api/monitor/usage/model-usage?{q}", headers=headers)
    except ProviderError as e:
        print(f"[zai] model-usage failed: {e}")
        return None, None
    total = (data.get("data") or {}).get("totalUsage") or {}
    calls = to_float(total.get("totalModelCallCount"))
    tokens = to_float(total.get("totalTokensUsage"))
    return (int(calls) if calls is not None else None, int(tokens) if tokens is not None else None)


def fetch(cfg: dict) -> dict:
    global _logged_shape
    key = cfg.get("api_key", "")
    if not key:
        raise ProviderError("api_key not set in config.toml")
    base = cfg.get("base_url", "https://api.z.ai").rstrip("/")
    headers = {"Authorization": key, "Accept-Language": "en-US,en"}
    data = get_json(f"{base}/api/monitor/usage/quota/limit", headers=headers)
    if not _logged_shape:
        _logged_shape = True
        print("[zai] response shape:", json.dumps(data)[:600])
    if data.get("success") is False or data.get("code") not in (None, 200):
        raise ProviderError(f"z.ai: {data.get('msg') or data.get('code')}")
    payload = data.get("data", data)
    windows = _windows(payload)
    plan = payload.get("planName") or payload.get("level")
    requests_30d, tokens_30d = _usage_30d(base, headers)
    return {"kind": "windows", "plan": str(plan).title() if plan else None, "windows": windows,
            "requests_30d": requests_30d, "tokens_30d": tokens_30d}
