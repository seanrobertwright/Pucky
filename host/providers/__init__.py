"""Provider adapters. Each module exposes fetch(cfg: dict) -> dict.

Return shape (host/status.py turns this into the device contract):
  windows kind: {"kind": "windows", "plan": str|None,
                 "windows": [{"label": str, "used_pct": float, "resets_at": int|None}, ...]}
  money kind:   {"kind": "money", "money": {"remaining_usd": float, "total_usd": float|None,
                 "spent_total_usd": float|None, "spend_30d_usd": float|None,
                 "requests_30d": int|None, "tokens_30d": int|None}}
Either kind may also carry top-level "requests_30d" / "tokens_30d" (30-day usage counters); for the
money kind the same keys inside "money" are equivalent. status.py exposes them on the provider
object as requests_30d / tokens_30d (null when unknown). If a money adapter supplies spend_30d_usd
itself it is used as-is; otherwise it is derived from daily snapshots of spent_total_usd/balance.
Raise ProviderError(message, stale=bool) on failure. stale=True means "credentials are fine but
the CLI must refresh them" (token expired); the message is shown on the device.
"""


class ProviderError(Exception):
    def __init__(self, message: str, stale: bool = False, retry_after: float | None = None):
        super().__init__(message)
        self.stale = stale
        self.retry_after = retry_after
