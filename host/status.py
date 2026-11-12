"""Turn adapter results into the device contract: status colours, 30-day spend, snapshots."""
import json
import os
import threading
import time
from datetime import date, timedelta

STATE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "state")
SNAP_PATH = os.path.join(STATE_DIR, "snapshots.json")
_lock = threading.Lock()


# ---------------------------------------------------------------- snapshots
def _load() -> dict:
    try:
        with open(SNAP_PATH, encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def _save(snap: dict) -> None:
    os.makedirs(STATE_DIR, exist_ok=True)
    tmp = SNAP_PATH + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(snap, f, indent=1, sort_keys=True)
    os.replace(tmp, SNAP_PATH)


def record(provider_id: str, series: str, value: float) -> None:
    """Keep one value per day per (provider, series). Series: 'spent' (monotonic lifetime spend)
    or 'balance' (remaining money)."""
    today = date.today().isoformat()
    with _lock:
        snap = _load()
        s = snap.setdefault(provider_id, {}).setdefault(series, {})
        if today not in s:            # first reading of the day wins for 'balance' (pre-spend)
            s[today] = value
        if series == "spent":         # latest reading of the day for lifetime spend
            s[today] = value
        # prune > 60 days
        cutoff = (date.today() - timedelta(days=60)).isoformat()
        for k in [k for k in s if k < cutoff]:
            del s[k]
        _save(snap)


def spend_30d(provider_id: str, series: str, current: float) -> float:
    cutoff = (date.today() - timedelta(days=30)).isoformat()
    with _lock:
        s = _load().get(provider_id, {}).get(series, {})
    pts = sorted((k, v) for k, v in s.items() if k >= cutoff)
    if series == "spent":
        if not pts:
            return 0.0
        return max(0.0, current - pts[0][1])
    # balance: sum of decreases between consecutive readings (top-ups ignored)
    vals = [v for _, v in pts] + [current]
    return sum(max(0.0, a - b) for a, b in zip(vals, vals[1:]))


def peak_balance(provider_id: str, current: float) -> float:
    with _lock:
        s = _load().get(provider_id, {}).get("balance", {})
    return max([current] + list(s.values()))


# ---------------------------------------------------------------- status
def window_status(windows: list, th: dict) -> str:
    worst = max((w["used_pct"] for w in windows), default=0.0)
    if worst >= th.get("window_crit_pct", 95):
        return "crit"
    if worst >= th.get("window_warn_pct", 80):
        return "warn"
    return "ok"


def money_status(remaining: float, th: dict) -> str:
    if remaining < th.get("money_crit_usd", 2):
        return "crit"
    if remaining < th.get("money_warn_usd", 5):
        return "warn"
    return "ok"


def build_provider(pid: str, name: str, rec: dict, th: dict, poll_s: int, now: float) -> dict:
    """rec = {"result", "updated_at", "error", "stale"} maintained by the agent."""
    out = {"id": pid, "name": name, "poll_s": poll_s, "updated_at": rec.get("updated_at")}
    res = rec.get("result")
    if res is None:
        out["kind"] = "none"
        out["status"] = "stale" if rec.get("stale") else "error"
        out["message"] = rec.get("error") or "no data yet"
        return out

    out["kind"] = res["kind"]
    out["plan"] = res.get("plan")
    if res["kind"] == "windows":
        out["windows"] = res["windows"]
        status = window_status(res["windows"], th)
    elif res["kind"] == "runs":
        # run tallies are all-time, so a non-zero "failed" is permanent and must not colour the
        # status or the LED; only freshness (below) can change it from "ok"
        out["runs"] = res["runs"]
        status = "ok"
    else:
        m = dict(res["money"])
        remaining = m["remaining_usd"]
        # keep snapshots even when the adapter reports spend itself, so a later loss of the
        # detailed endpoint degrades to the derived figure instead of nothing
        if m.get("spent_total_usd") is not None:
            record(pid, "spent", m["spent_total_usd"])
            derived = spend_30d(pid, "spent", m["spent_total_usd"])
        else:
            record(pid, "balance", remaining)
            derived = spend_30d(pid, "balance", remaining)
        if m.get("spend_30d_usd") is None:
            m["spend_30d_usd"] = round(derived, 2)
        else:
            m["spend_30d_usd"] = round(float(m["spend_30d_usd"]), 2)
        if m.get("total_usd") is None:
            m["total_usd"] = round(peak_balance(pid, remaining), 2)
        m.pop("spent_total_usd", None)
        out["money"] = m
        status = money_status(remaining, th)
    # 30-day usage counters (either kind); money adapters may put them inside "money"
    src = res.get("money", {}) if res["kind"] == "money" else {}
    for k in ("requests_30d", "tokens_30d"):
        v = res.get(k, src.get(k))
        out[k] = int(v) if v is not None else None
        if res["kind"] == "money":
            out["money"][k] = out[k]

    # freshness overrides colour: data older than 3 polls, or a stale/error flag from the adapter
    age = now - (rec.get("updated_at") or 0)
    if rec.get("stale") or age > 3 * poll_s:
        status = "stale"
        out["message"] = rec.get("error") or "no fresh data"
    elif rec.get("error"):
        out["message"] = rec["error"]      # last poll failed but cached data is still fresh enough
    out["status"] = status
    return out
