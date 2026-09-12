"""Pucky host agent: polls providers on a schedule and serves /api/status for the dial.

    python agent.py            run the server (Ctrl-C to stop)
    python agent.py --once     fetch every enabled provider once, print the JSON, exit
"""
import importlib
import json
import os
import sys
import threading
import time
import tomllib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import status as st
from providers import ProviderError

HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = {"claude": "Claude Code", "codex": "Codex", "openrouter": "OpenRouter", "antigravity": "Antigravity",
         "gemini": "Gemini", "zai": "z.AI GLM", "deepseek": "DeepSeek"}
DEFAULT_ORDER = ["claude", "codex", "openrouter", "antigravity", "zai", "deepseek"]
MAX_BACKOFF = 1800


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


def load_config() -> dict:
    path = os.path.join(HERE, "config.toml")
    if not os.path.exists(path):
        sys.exit("host/config.toml missing — copy config.example.toml and fill it in")
    with open(path, "rb") as f:
        return tomllib.load(f)


class Poller(threading.Thread):
    def __init__(self, pid: str, cfg: dict):
        super().__init__(name=f"poll-{pid}", daemon=True)
        self.pid, self.cfg = pid, cfg
        self.poll_s = int(cfg.get("poll_s", 300))
        self.mod = importlib.import_module(f"providers.{pid}")
        self.rec = {"result": None, "updated_at": None, "error": None, "stale": False}
        self.lock = threading.Lock()
        self.wake = threading.Event()
        self.backoff = 0

    def poll_once(self) -> None:
        try:
            res = self.mod.fetch(self.cfg)
            with self.lock:
                self.rec.update(result=res, updated_at=int(time.time()), error=None, stale=False)
            self.backoff = 0
            log(f"[{self.pid}] ok")
        except ProviderError as e:
            with self.lock:
                self.rec.update(error=str(e), stale=e.stale)
            if e.retry_after:
                self.backoff = max(self.backoff, e.retry_after)
            else:
                self.backoff = min(MAX_BACKOFF, max(60, self.backoff * 2))
            log(f"[{self.pid}] {'stale' if e.stale else 'error'}: {e}  (backoff {int(self.backoff)}s)")
        except Exception as e:  # adapter bug — keep the thread alive
            with self.lock:
                self.rec.update(error=f"internal: {e!r}")
            self.backoff = min(MAX_BACKOFF, max(60, self.backoff * 2))
            log(f"[{self.pid}] internal error: {e!r}")

    def run(self) -> None:
        while True:
            self.poll_once()
            self.wake.wait(timeout=max(self.poll_s, self.backoff))
            self.wake.clear()

    def snapshot(self) -> dict:
        with self.lock:
            return dict(self.rec)


class Agent:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.th = cfg.get("thresholds", {})
        self.subs = cfg.get("subscriptions", {})
        order = cfg.get("order", DEFAULT_ORDER)
        self.pollers: list[Poller] = []
        for pid in order:
            pc = cfg.get("providers", {}).get(pid, {})
            if not pc.get("enabled", False):
                continue
            self.pollers.append(Poller(pid, pc))

    def start(self) -> None:
        for p in self.pollers:
            p.start()

    def refresh_all(self) -> None:
        for p in self.pollers:
            p.wake.set()

    def status(self) -> dict:
        now = time.time()
        providers = [st.build_provider(p.pid, NAMES.get(p.pid, p.pid), p.snapshot(), self.th, p.poll_s, now)
                     for p in self.pollers]
        spend = sum(float(v) for v in self.subs.values())
        spend += sum(p.get("money", {}).get("spend_30d_usd", 0) or 0 for p in providers)
        return {
            "schema": 1,
            "now": int(now),
            "tz_offset_min": int(time.localtime().tm_gmtoff // 60),
            "host_ok": True,
            "spend_30d_usd": round(spend, 2),
            "providers": providers,
        }


def make_handler(agent: Agent):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):  # quiet
            pass

        def _send(self, code: int, body: bytes, ctype: str):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/api/status":
                self._send(200, json.dumps(agent.status()).encode(), "application/json")
            elif path == "/api/refresh":
                agent.refresh_all()
                self._send(200, b'{"ok":true}', "application/json")
            elif path == "/":
                s = agent.status()
                rows = "".join(
                    f"<tr><td>{p['name']}</td><td class={p['status']}>{p['status']}</td>"
                    f"<td>{_fmt(p)}</td><td>{p.get('message','')}</td></tr>" for p in s["providers"])
                html = (f"<!doctype html><title>Pucky host</title><style>body{{font:14px system-ui;"
                        f"background:#111;color:#ddd;padding:1em}}td{{padding:.3em .8em}}.ok{{color:#6c6}}"
                        f".warn{{color:#fc6}}.crit{{color:#f66}}.stale{{color:#888}}.error{{color:#f66}}</style>"
                        f"<h3>Pucky host — spend/30d ${s['spend_30d_usd']}</h3><table>{rows}</table>"
                        f"<p><a href=/api/status>/api/status</a> · <a href=/api/refresh>/api/refresh</a></p>")
                self._send(200, html.encode(), "text/html; charset=utf-8")
            else:
                self._send(404, b"not found", "text/plain")

    return Handler


def _count(n) -> str:
    n = float(n)
    for div, suffix in ((1e9, "B"), (1e6, "M"), (1e3, "k")):
        if n >= div:
            return f"{n / div:.1f}{suffix}"
    return f"{n:.0f}"


def _fmt(p: dict) -> str:
    if p.get("kind") == "windows":
        s = " · ".join(f"{w['label']} {w['used_pct']:.0f}%" for w in p["windows"])
    elif p.get("kind") == "money":
        m = p["money"]
        s = f"${m['remaining_usd']:.2f} left · ${m.get('spend_30d_usd', 0):.2f}/30d"
    else:
        return "—"
    if p.get("requests_30d") is not None:
        s += f" · {_count(p['requests_30d'])} req"
    if p.get("tokens_30d") is not None:
        s += f" · {_count(p['tokens_30d'])} tok /30d"
    return s


def main() -> None:
    os.chdir(HERE)
    cfg = load_config()
    agent = Agent(cfg)
    if not agent.pollers:
        sys.exit("no providers enabled in config.toml")
    if "--once" in sys.argv:
        for p in agent.pollers:
            p.poll_once()
        print(json.dumps(agent.status(), indent=1))
        return
    agent.start()
    port = int(cfg.get("port", 8787))
    srv = ThreadingHTTPServer(("0.0.0.0", port), make_handler(agent))
    log(f"serving http://0.0.0.0:{port}/api/status  providers: {[p.pid for p in agent.pollers]}")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
