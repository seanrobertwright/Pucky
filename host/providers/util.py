"""Small parsing helpers shared by adapters."""
from datetime import datetime, timezone


def iso_to_epoch(value) -> int | None:
    """Accepts ISO-8601 strings, epoch seconds, or epoch milliseconds. Returns epoch seconds."""
    if value is None or value == "":
        return None
    if isinstance(value, (int, float)):
        v = float(value)
        return int(v / 1000) if v > 1e11 else int(v)
    s = str(value).strip()
    if s.isdigit():
        return iso_to_epoch(int(s))
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        dt = datetime.fromisoformat(s)
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp())


def to_float(value, default=None):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default
