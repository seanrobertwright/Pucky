"""DeepSeek - prepaid balance, plus optional 30-day usage from the platform dashboard API.

Documented: GET https://api.deepseek.com/user/balance (API key).

The public API has no usage/history endpoint, but the usage page at platform.deepseek.com is
driven by an internal API that does (confirmed 2026-09-12 by watching the page's own calls):
  GET https://platform.deepseek.com/api/v0/usage/by_api_key/amount?start=&end=&tz=
      -> data.biz_data.series[]{api_key, model, buckets[]{time,
           usage{REQUEST, RESPONSE_TOKEN, PROMPT_CACHE_HIT_TOKEN, PROMPT_CACHE_MISS_TOKEN}}}
  GET https://platform.deepseek.com/api/v0/usage/by_api_key/cost?start=&end=&tz=
      -> data.biz_data.data[]{currency, series[]{api_key, model, buckets[]{time, cost: "0.12"}}}
  start/end are epoch seconds on local-midnight boundaries (anything else -> biz_code 1
  INVALID_PARAM), tz is the local UTC offset in seconds. Auth errors come back as HTTP 200
  with code 40002 ("Missing Token" / "invalid token").
It needs the *web session* token (not the API key): log in to platform.deepseek.com, open
DevTools -> Network, click any request to /api/v0/usage, copy the value after "Bearer " from
the Authorization request header into config.toml as session_token. When it expires the
counters drop back to null and the log says so; the balance keeps working regardless.
"""
import time
import urllib.parse

from . import ProviderError
from .http import get_json
from .util import to_float

BALANCE_URL = "https://api.deepseek.com/user/balance"
USAGE_BASE = "https://platform.deepseek.com/api/v0/usage/by_api_key"
TOKEN_KEYS = ("RESPONSE_TOKEN", "PROMPT_CACHE_HIT_TOKEN", "PROMPT_CACHE_MISS_TOKEN")
_warned = False


def _platform(path: str, token: str, params: dict) -> dict:
    url = f"{USAGE_BASE}/{path}?{urllib.parse.urlencode(params)}"
    data = get_json(url, headers={"Authorization": f"Bearer {token}"})
    if data.get("code") not in (0, None):
        raise ProviderError(f"platform api: {data.get('msg') or data.get('code')}", stale=True)
    biz = (data.get("data") or {})
    if biz.get("biz_code") not in (0, None):
        raise ProviderError(f"platform api: {biz.get('biz_msg') or biz.get('biz_code')}")
    return biz.get("biz_data") or {}


def _usage_30d(token: str, currency: str) -> dict:
    """Sum requests, tokens and cost over the last 30 days across all keys and models."""
    global _warned
    # start/end must sit on local-midnight boundaries or the API answers INVALID_PARAM;
    # end = tomorrow 00:00 local so today's partial day is included (30 daily buckets total)
    lt = time.localtime()
    end = int(time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, 0, 0, 0, 0, 0, -1))) + 86400
    params = {"start": end - 30 * 86400, "end": end, "tz": lt.tm_gmtoff}
    try:
        amount = _platform("amount", token, params)
        cost = _platform("cost", token, params)
    except ProviderError as e:
        if not _warned:
            _warned = True
            print(f"[deepseek] usage unavailable ({e}) - copy a fresh session_token from platform.deepseek.com")
        return {}
    _warned = False
    requests = tokens = 0.0
    for s in amount.get("series") or []:
        for b in s.get("buckets") or []:
            u = b.get("usage") or {}
            requests += to_float(u.get("REQUEST"), 0.0)
            tokens += sum(to_float(u.get(k), 0.0) for k in TOKEN_KEYS)
    spend = 0.0
    entries = cost.get("data") or []
    entry = next((e for e in entries if e.get("currency") == currency), entries[0] if entries else {})
    for s in entry.get("series") or []:
        for b in s.get("buckets") or []:
            spend += to_float(b.get("cost"), 0.0)
    return {"requests_30d": int(requests), "tokens_30d": int(tokens), "spend_30d_usd": round(spend, 4)}


def fetch(cfg: dict) -> dict:
    key = cfg.get("api_key", "")
    if not key:
        raise ProviderError("api_key not set in config.toml")
    data = get_json(BALANCE_URL, headers={"Authorization": f"Bearer {key}"})
    infos = data.get("balance_infos") or []
    if not infos:
        raise ProviderError(f"unexpected response: {data}")
    info = next((i for i in infos if i.get("currency") == "USD"), infos[0])
    balance = to_float(info.get("total_balance"))
    if balance is None:
        raise ProviderError(f"unexpected balance entry: {info}")
    currency = info.get("currency", "USD")
    money = {
        "remaining_usd": round(balance, 4),
        "currency": currency,
        "total_usd": None,          # status.py fills in the peak balance seen
        "spent_total_usd": None,    # 30-day spend derived from balance snapshots unless supplied below
        "requests_30d": None,
        "tokens_30d": None,
    }
    token = cfg.get("session_token", "")
    if token:
        money.update(_usage_30d(token, currency))
    return {"kind": "money", "money": money}
