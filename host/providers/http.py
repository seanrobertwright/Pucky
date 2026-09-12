"""Tiny stdlib HTTP helper shared by the adapters."""
import json
import urllib.error
import urllib.request

from . import ProviderError

USER_AGENT = "pucky/1.0"


def get_json(url: str, headers: dict | None = None, method: str = "GET",
             body: dict | None = None, timeout: float = 15.0) -> dict:
    hdrs = {"User-Agent": USER_AGENT, "Accept": "application/json"}
    if headers:
        hdrs.update(headers)
    data = None
    if body is not None:
        data = json.dumps(body).encode()
        hdrs.setdefault("Content-Type", "application/json")
    req = urllib.request.Request(url, data=data, headers=hdrs, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
    except urllib.error.HTTPError as e:
        detail = ""
        try:
            detail = e.read(300).decode(errors="replace")
        except Exception:
            pass
        retry_after = None
        if e.code == 429:
            try:
                retry_after = float(e.headers.get("retry-after", "0")) or None
            except ValueError:
                retry_after = None
        stale = e.code in (401, 403)
        raise ProviderError(f"HTTP {e.code}: {detail[:120]}", stale=stale, retry_after=retry_after)
    except urllib.error.URLError as e:
        raise ProviderError(f"network: {e.reason}")
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        raise ProviderError(f"bad JSON: {raw[:80]!r}")
