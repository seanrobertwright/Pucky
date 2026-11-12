"""Archon workflow monitor - how many workflow runs are running / completed / failed / cancelled.

Archon's local backend (default http://localhost:3090) needs no auth. Its dashboard endpoint
returns server-computed totals for every status alongside a page of runs:
  GET /api/dashboard/runs?limit=1 -> {"runs": [...], "total": N,
        "counts": {"all", "running", "completed", "failed", "cancelled", "pending", "paused"}}
`counts` ignores `limit`, so asking for one run keeps the payload to a few KB. The totals are
all-time (the same numbers as Archon's own dashboard header), not a rolling window.

"waiting" = pending + paused (queued, or parked at an approval gate): live work that is neither
running nor finished, reported so the four headline numbers don't silently under-add to the total.

An unreachable server is the normal state whenever Archon isn't started, so it is reported as an
error (not stale) with a plain message.

Drill-down for the dial's touch UI (served by agent.py, not part of /api/status):
  list_runs(cfg)          -> /api/archon/runs          active runs (+ optional recent finished ones)
  run_graph(cfg, run_id)  -> /api/archon/graph?id=...  one run's workflow graph, laid out for drawing
Sources: GET /api/dashboard/runs?status=<s>, GET /api/workflows/runs/<id> (run + all events, 0.2-4 MB
whatever the query, hence the cache and the host-side digest) and GET /api/workflows/<name>
(definition: nodes[].id / depends_on).
"""
import threading
import time
import urllib.parse

from . import ProviderError
from .http import get_json
from .util import iso_to_epoch


def fetch(cfg: dict) -> dict:
    base = _base(cfg)
    try:
        data = get_json(f"{base}/api/dashboard/runs?limit=1", timeout=float(cfg.get("timeout_s", 5)))
    except ProviderError as e:
        if str(e).startswith("network:"):
            raise ProviderError(f"Archon not reachable at {base}") from e
        raise
    counts = data.get("counts") if isinstance(data, dict) else None
    if not isinstance(counts, dict):
        raise ProviderError(f"unexpected response: {str(data)[:120]}")

    def n(key: str) -> int:
        try:
            return max(0, int(counts.get(key) or 0))
        except (TypeError, ValueError):
            return 0

    runs = {"running": n("running"), "completed": n("completed"), "failed": n("failed"),
            "cancelled": n("cancelled"), "waiting": n("pending") + n("paused"), "total": n("all")}
    return {"kind": "runs", "runs": runs}


# ------------------------------------------------------------------ drill-down (touch UI on the dial)
ACTIVE = ("running", "paused", "pending")
_EVENT_STATE = {"node_started": "running", "node_completed": "completed", "node_failed": "failed",
                "node_skipped": "skipped", "node_skipped_prior_success": "completed"}
_cache: dict = {}
_cache_lock = threading.Lock()


def _base(cfg: dict) -> str:
    # "localhost" resolves to ::1 first on Windows and Archon listens on IPv4 only: every request
    # then waits ~2 s for the IPv6 attempt to fail. Go straight to the IPv4 loopback.
    base = cfg.get("base_url", "http://localhost:3090").rstrip("/")
    return base.replace("://localhost", "://127.0.0.1", 1)


def _get(cfg: dict, path: str, ttl: float, timeout: float = 20.0):
    """GET with a small TTL cache: the dial re-asks every few seconds and Archon is slow."""
    url = _base(cfg) + path
    now = time.time()
    with _cache_lock:
        hit = _cache.get(url)
        if hit and now - hit[0] < ttl:
            return hit[1]
    try:
        data = get_json(url, timeout=timeout)
    except ProviderError as e:
        if str(e).startswith("network:"):
            raise ProviderError(f"Archon not reachable at {_base(cfg)}") from e
        raise
    with _cache_lock:
        _cache[url] = (now, data)
        for k in [k for k, v in _cache.items() if now - v[0] > 600]:
            del _cache[k]
    return data


def _short_name(name) -> str:
    """owner/pack:implement -> pack:implement (the owner rarely fits on a 480 px screen)."""
    return str(name or "?").rsplit("/", 1)[-1]


def _node_name(v):
    if isinstance(v, dict):
        v = v.get("id") or v.get("node_id") or v.get("nodeId") or v.get("name")
    return str(v) if v else None


def _run_row(r: dict) -> dict:
    active = r.get("active_nodes") or []
    node = _node_name(active[0]) if active else _node_name(r.get("current_step_name"))
    if node:                                # ship__triage__triage -> triage/triage: the innermost two levels
        node = "/".join(node.split("__")[-2:])
    return {"id": r["id"], "name": _short_name(r.get("workflow_name")), "status": r.get("status") or "?",
            "started_at": iso_to_epoch(r.get("started_at")),
            "ended_at": iso_to_epoch(r.get("completed_at")),
            "node": node, "project": _short_name(r.get("codebase_name") or "")}


def list_runs(cfg: dict) -> dict:
    """Active runs (running, then paused, then pending; newest first within each), optionally followed
    by the most recent finished ones (config `recent_finished`, default 0) so the graph view has
    something to open while nothing is running."""
    limit = int(cfg.get("max_runs", 8))
    rows = []
    for status in ACTIVE:                   # the endpoint's status filter takes a single value
        if len(rows) >= limit:
            break
        data = _get(cfg, f"/api/dashboard/runs?status={status}&limit={limit}", ttl=3)
        rows += [_run_row(r) for r in data.get("runs", []) if r.get("status") == status]
    rows = rows[:limit]
    n_active = len(rows)
    extra = min(int(cfg.get("recent_finished", 0)), limit - n_active)
    if extra > 0:
        data = _get(cfg, f"/api/dashboard/runs?limit={extra + n_active + 4}", ttl=3)
        done = [_run_row(r) for r in data.get("runs", []) if r.get("status") not in ACTIVE]
        rows += done[:extra]
    return {"now": int(time.time()), "active": n_active, "runs": rows}


def _layout(node_ids: list, deps: dict) -> tuple:
    """Layered top-to-bottom DAG layout. Returns (layer{id}, x{id}, edges[(from, to, lane)], lanes_l, lanes_r).

    layer = longest path from a root; order within a layer by the barycentre of the parents; x is in
    column units centred on 0. Edges between neighbouring layers are straight (lane 0). An edge that
    skips layers would cut through the nodes in between, so it runs down a vertical lane beside the
    graph instead: lane -k = k-th lane on the left, +k on the right. All long edges into one node share
    a lane; lanes whose vertical spans overlap get different indices.
    """
    layer: dict = {}

    def depth(n, seen=()):
        if n in layer:
            return layer[n]
        if n in seen:                       # cycle guard: should not happen in a DAG
            return 0
        layer[n] = 1 + max((depth(d, seen + (n,)) for d in deps[n]), default=-1)
        return layer[n]

    for n in node_ids:
        depth(n)
    rows: dict = {}
    for n in node_ids:                      # definition order is the initial order
        rows.setdefault(layer[n], []).append(n)
    x: dict = {}
    for l in sorted(rows):
        if l:
            def bary(n):
                ps = [x[d] for d in deps[n] if layer[d] == l - 1] or [x[d] for d in deps[n]]
                return sum(ps) / len(ps) if ps else 0.0
            rows[l].sort(key=lambda n: (bary(n), node_ids.index(n)))
        for i, n in enumerate(rows[l]):
            x[n] = i - (len(rows[l]) - 1) / 2

    idx = {n: i for i, n in enumerate(node_ids)}
    edges, long_by_target = [], {}
    for n in node_ids:
        for d in deps[n]:
            if layer[n] - layer[d] <= 1:
                edges.append((idx[d], idx[n], 0))
            else:
                long_by_target.setdefault(n, []).append(d)
    used = {-1: [], 1: []}                  # side -> [(top_layer, bottom_layer, lane_index)]
    for n in sorted(long_by_target, key=lambda t: (layer[t], idx[t])):
        srcs = long_by_target[n]
        top, bot = min(layer[d] for d in srcs), layer[n]
        best = None
        for side in ((1, -1) if x[n] > 0 else (-1, 1)):
            taken = {k for (a, b, k) in used[side] if a < bot and top < b}
            k = next(i for i in range(1, 99) if i not in taken)
            if best is None or k < best[1]:
                best = (side, k)
        side, k = best
        used[side].append((top, bot, k))
        edges += [(idx[d], idx[n], side * k) for d in srcs]
    lanes = {s: max((k for _, _, k in used[s]), default=0) for s in used}
    return layer, x, edges, lanes[-1], lanes[1]


def _rollup(states) -> str:
    """State of a sub-workflow node from its children's states."""
    states = list(states)
    if not states:
        return "pending"
    if "failed" in states:
        return "failed"
    if "running" in states:
        return "running"
    if "completed" in states:               # some done, none active: between two children
        return "completed" if all(s in ("completed", "skipped") for s in states) else "running"
    return "skipped" if all(s == "skipped" for s in states) else "pending"


def run_graph(cfg: dict, run_id: str) -> dict:
    """One run as a drawable graph: nodes with state + layout, edges with routing lanes."""
    if not run_id or not all(c.isalnum() or c == "-" for c in run_id):
        raise ProviderError("bad run id")
    detail = _get(cfg, f"/api/workflows/runs/{run_id}", ttl=4)
    run = detail.get("run") or {}
    wf_name = run.get("workflow_name") or ""
    status = run.get("status") or "?"

    # structure: the workflow definition (cached longer; it rarely changes mid-run)
    nodes_def = []
    try:
        wf = _get(cfg, "/api/workflows/" + urllib.parse.quote(wf_name, safe=""), ttl=60)
        nodes_def = (wf.get("workflow") or {}).get("nodes") or []
    except ProviderError:
        pass                                # deleted/renamed workflow: fall back to the recorded node list
    if nodes_def:
        ids = [n["id"] for n in nodes_def]
        deps = {n["id"]: [d for d in n.get("depends_on") or [] if d in ids] for n in nodes_def}
    else:                                   # no definition: show the recorded nodes as a plain chain
        ids = list(((run.get("metadata") or {}).get("terminal_graph") or {}).get("node_ids") or [])
        deps = {n: ([ids[i - 1]] if i else []) for i, n in enumerate(ids)}

    # state: replay the node events, then let the terminal record (ended runs) overrule.
    # `seen` is keyed by the ids Archon records, which are not only the definition's: a sub-workflow
    # node "review" never reports itself, its children do, as "review__code", "ship__triage__triage"
    # (nested) or "await-checks.ci-probe".
    seen: dict = {}
    for e in detail.get("events") or []:
        s = _EVENT_STATE.get(e.get("event_type"))
        nid = e.get("step_name") or _node_name((e.get("data") or {}).get("node"))
        if s and nid:
            seen.pop(nid, None)             # re-insert so dict order = most recent activity last
            seen[nid] = s
    for n in (run.get("terminal_record") or {}).get("nodes") or []:
        if n.get("node_id") and n.get("state"):
            seen[n["node_id"]] = n["state"]
    finished = status not in ACTIVE
    halted = "failed" if status == "failed" else "cancelled" if status == "cancelled" else "completed"
    state, at = {}, {}
    for n in ids:
        kids = {k: v for k, v in seen.items() if k.startswith(n + "__") or k.startswith(n + ".")}
        s = seen.get(n) or _rollup(kids.values())
        if finished:                        # nothing is still running, and what never ran never will
            s = halted if s == "running" else "skipped" if s == "pending" else s
        state[n] = s
        live = [k for k, v in kids.items() if v == "running"]
        if live and s == "running":         # where inside the sub-workflow it is right now
            at[n] = live[-1][len(n):].lstrip("_.").replace("__", "/")

    ids = ids[:int(cfg.get("max_nodes", 40))]
    deps = {n: [d for d in deps[n] if d in ids] for n in ids}
    layer, x, edges, lanes_l, lanes_r = _layout(ids, deps)
    order = sorted(ids, key=lambda n: (layer[n], x[n]))         # dial order: row by row, left to right
    pos = {n: i for i, n in enumerate(order)}
    remap = {i: pos[n] for i, n in enumerate(ids)}
    per_layer: dict = {}
    for l in layer.values():
        per_layer[l] = per_layer.get(l, 0) + 1
    return {
        "id": run_id, "name": _short_name(wf_name), "status": status,
        "started_at": iso_to_epoch(run.get("started_at")), "ended_at": iso_to_epoch(run.get("completed_at")),
        "layers": max(layer.values(), default=-1) + 1, "width": max(per_layer.values(), default=1),
        "lanes_l": lanes_l, "lanes_r": lanes_r,
        "done": sum(1 for n in ids if state[n] in ("completed", "skipped")), "total": len(ids),
        "nodes": [{"id": n.replace("__", "/"), "state": state[n], "l": layer[n], "x": x[n],
                   **({"at": at[n]} if n in at else {})} for n in order],
        "edges": sorted([remap[a], remap[b], k] for a, b, k in edges),
    }
