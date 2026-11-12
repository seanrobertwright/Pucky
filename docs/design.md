# Pucky — Design

Round-screen knob display (Elecrow CrowPanel 2.1" rotary, see `crowpanel-2.1-rotary.md`) showing
usage/credits for six AI providers (Claude Code, Codex, OpenRouter, Antigravity, z.AI, DeepSeek). Turn the knob to pick a provider; it auto-cycles when idle.

Status: design accepted 2026-09-12. Host: all six adapters implemented; Gemini CLI (retired June 2026) replaced by Antigravity. Firmware: MVP built.
Later additions: GitHub Copilot, Kiro, and an Archon workflow monitor (§2, §3, §4) — the first provider that is not a usage meter.

## 1. Decisions

| Topic | Decision | Why |
|---|---|---|
| Architecture | **Host agent on the PC + thin device.** Host (Python, stdlib only) polls all providers, computes status, serves one JSON over plain HTTP on the LAN. Device polls it every 60 s and only renders. | Claude's OAuth refresh tokens are single-use — a second refresher (the device) gets the credential disabled (oh-my-pi #5396). Codex uses the same pattern. Once a host exists for those, routing everything through it gives one code path, no TLS/keys on the device, and adding a provider is a host-only change. |
| Secrets | Device: `include/secrets.h` (gitignored) with WiFi SSID/password + host URL. Host: `host/config.toml` (gitignored) with API keys; Claude/Codex/Gemini reuse the CLIs' own login files, **read-only**. | Only WiFi lives on the device. Never refresh Claude/Codex tokens from outside the CLI. |
| Host runtime | Python 3.14, standard library only (`http.server`, `urllib`, `json`, `tomllib`, `threading`). Task Scheduler "at logon", hidden window. | Zero install steps; already on the PC. |
| Device runtime | PlatformIO (pioarduino), Arduino core 3.x, Arduino_GFX (ST7701 RGB, as Elecrow's example), LVGL 9, ArduinoJson. | Matches the vendor's known-good display stack. |
| Clock | 12-hour. Device sets its clock from the host's `now` field on every poll (no NTP). Reset times shown relative ("resets in 2h 14m"); if > 24 h, "resets Tue 3:45 PM". | |
| Total spend | "AI spend / 30 d" = pay-per-use spend over 30 days (OpenRouter + DeepSeek, from daily balance snapshots kept by the host) + optional fixed subscription fees from config (default 0). | Subscriptions are flat; putting the fee in config makes the number "what AI costs me per month". |
| Thresholds | Windows: warn >= 80 %, crit >= 95 %. Money: warn < $5, crit < $2. Per-provider override in config. **Host computes status**, device only maps status -> colour. | One place to tune. |
| Poll cadence | Device -> host: 60 s. Host -> providers: Claude 5 min, Codex 5 min, Gemini 5 min, z.AI 2 min, OpenRouter 2 min, DeepSeek 2 min. On HTTP 429: exponential backoff, keep last good value. | Claude's usage endpoint 429s at 30–60 s polling (claude-code #31637). |
| Stale rule | Device marks a provider stale if `updated_at` > 3x its poll interval; marks everything stale if the host hasn't answered for 5 min. Stale = grey ring + warning glyph, last values still shown. | PC asleep should degrade, not blank. |

## 2. Provider adapters (host)

| Provider | Kind | Source | Auth | Confidence |
|---|---|---|---|---|
| Claude Code (Max) | windows: 5 h, 7 d (+ per-model 7 d if present) | `GET https://api.anthropic.com/api/oauth/usage` -> `five_hour{utilization,resets_at}`, `seven_day{...}` | `Authorization: Bearer <accessToken>` from `~/.claude/.credentials.json` (`claudeAiOauth.accessToken`, `expiresAt`), header `anthropic-beta: oauth-2025-04-20`. **Never refresh.** If expired -> report stale "run claude to refresh". | High (widely used, undocumented) |
| Codex (ChatGPT sub) | windows: 5 h primary, weekly secondary | `GET https://chatgpt.com/backend-api/wham/usage` | `Authorization: Bearer tokens.access_token`, `ChatGPT-Account-ID: tokens.account_id` from `~/.codex/auth.json`. Read-only, same rule. | Medium (field names to confirm on first call) |
| Antigravity (agy CLI + IDE, Google AI Pro) | windows: one arc per quota pool (Gemini models; Claude + GPT-OSS), 5 h reset | `POST https://cloudcode-pa.googleapis.com/v1internal:loadCodeAssist` (metadata `ideType: ANTIGRAVITY`) -> `cloudaicompanionProject`, `paidTier.name`; `POST .../v1internal:fetchAvailableModels` -> `models{id: {displayName, quotaInfo{remainingFraction, resetTime}}}`; models with identical fraction+reset share a pool. | Bearer from agy's OS-keyring entry (Windows Credential Manager `gemini:antigravity`, JSON `{token:{access_token, refresh_token, expiry}}`), read-only; expired tokens refreshed in memory with Antigravity's public OAuth client. `User-Agent: antigravity`. | High (confirmed live 2026-09-12; endpoints undocumented) |
| GitHub Copilot (Pro) | windows: monthly premium requests | `GET https://api.github.com/copilot_internal/user` -> `quota_snapshots.premium_interactions{entitlement, credits_used, remaining}`, `quota_reset_date` | Bearer token from `gh auth token`, read-only. | High (confirmed live 2026-09-12) |
| Kiro | windows: monthly credits, bonus, overage | `GET https://codewhisperer.us-east-1.amazonaws.com/getUsageLimits?origin=AI_EDITOR&resourceType=AGENTIC_REQUEST&profileArn=...` -> `usageBreakdownList[CREDIT]`, `nextDateReset`, `subscriptionInfo` | Bearer `accessToken` from `~/.aws/sso/cache/kiro-auth-token.json`, read-only; expired -> stale until Kiro IDE refreshes it. | Medium (request format from Q Developer CLI client + community proxies) |
| Archon (workflow runs) | runs: running / completed / failed / cancelled (+ waiting = pending + paused) | `GET <base_url>/api/dashboard/runs?limit=1` -> `counts{all, running, completed, failed, cancelled, pending, paused}`. The counts are server-side totals that ignore `limit`, so one tiny request per poll; `?after=<ISO time>` would scope them to a window but running runs older than the window would drop out, so the totals are all-time (same as Archon's dashboard header). | None (local backend, default `http://localhost:3090`). Server down = `error` "Archon not reachable", not stale. | High (confirmed live 2026-11-12, OpenAPI at `/api/openapi.json`) |
| ~~Gemini CLI~~ | retired by Google 2026-06-18; adapter kept but disabled (its OAuth client is rejected: "unsupported client") | | | |
| z.AI GLM Coding Plan | windows: 5 h, 7 d (+ monthly MCP tools on higher plans); 30-day calls/tokens | `GET https://api.z.ai/api/monitor/usage/quota/limit` -> `data.limits[]{type, unit, number, percentage, nextResetTime}` (`unit` 3 = hours, 6 = weeks; `type` TIME_LIMIT = monthly MCP). `GET .../model-usage?startTime=&endTime=` (`yyyy-MM-dd HH:mm:ss`) -> `data.totalUsage{totalModelCallCount, totalTokensUsage}`. | `Authorization: <api key>` (**no** "Bearer"), `Accept-Language: en-US,en`. Key from config. | High (confirmed live 2026-09-12) |
| OpenRouter | credits (+ optional 30-day spend/requests/tokens) | `GET https://openrouter.ai/api/v1/credits` -> `data.total_credits`, `data.total_usage`; remaining = diff. 30-day spend from daily `total_usage` snapshots, **or** exact from `GET /api/v1/activity` (last 30 completed UTC days, rows `{usage, byok_usage_inference, requests, prompt_tokens, completion_tokens, reasoning_tokens}`) when a management key is configured. | `Authorization: Bearer <key>`; activity needs a *management* key (`management_key` in config) | High (documented, confirmed live) |
| DeepSeek | balance (+ optional 30-day cost/requests/tokens) | `GET https://api.deepseek.com/user/balance` -> `balance_infos[]{currency,total_balance,granted_balance,topped_up_balance}`. Dashboard's internal API (confirmed live 2026-09-12): `GET https://platform.deepseek.com/api/v0/usage/by_api_key/amount?start=&end=&tz=` -> `data.biz_data.series[]{model, api_key, buckets[]{time, usage{REQUEST, RESPONSE_TOKEN, PROMPT_CACHE_HIT_TOKEN, PROMPT_CACHE_MISS_TOKEN}}}` and `.../cost?...` -> `data.biz_data.data[]{currency, series[]{buckets[]{time, cost}}}`; errors are HTTP 200 with `code` 40002 (missing/invalid token). | Balance: `Authorization: Bearer <api key>`. Usage: `Authorization: Bearer <web session token>` copied from the browser (`session_token` in config); no refresh, re-copy when it expires. | High for balance; Medium for usage (undocumented web API, token lifetime unknown) |

Snapshots: host appends `{date, provider, total_usage_or_balance}` once per day to `host/state/snapshots.json`.

## 3. Data contract — `GET http://<pc>:8787/api/status`

```json
{
  "schema": 1,
  "now": 1789234567,
  "tz_offset_min": -240,
  "host_ok": true,
  "spend_30d_usd": 84.12,
  "providers": [
    { "id": "claude",     "name": "Claude Code", "kind": "windows", "status": "ok",
      "updated_at": 1789234500, "poll_s": 300, "plan": "Max",
      "windows": [ { "label": "5h", "used_pct": 42.0, "resets_at": 1789245000 },
                   { "label": "7d", "used_pct": 18.5, "resets_at": 1789700000 } ] },
    { "id": "codex",      "name": "Codex",       "kind": "windows", "status": "warn",  "...": "..." },
    { "id": "antigravity", "name": "Antigravity", "kind": "windows", "status": "ok", "plan": "AI Pro",
      "windows": [ { "label": "Gemini", "used_pct": 3.1, "resets_at": 1789252741 },
                   { "label": "Claude+GPT", "used_pct": 0.0, "resets_at": 1789256815 } ] },
    { "id": "zai",        "name": "z.AI GLM",    "kind": "windows", "status": "ok",    "plan": "Lite",
      "requests_30d": 1077, "tokens_30d": 196845097, "...": "..." },
    { "id": "openrouter", "name": "OpenRouter",  "kind": "money",   "status": "ok",
      "updated_at": 1789234560, "poll_s": 120,
      "money": { "remaining_usd": 12.34, "total_usd": 50.00, "spend_30d_usd": 21.10 } },
    { "id": "deepseek",   "name": "DeepSeek",    "kind": "money",   "status": "crit",
      "money": { "remaining_usd": 1.20, "total_usd": 10.00, "spend_30d_usd": 8.80,
                 "requests_30d": null, "tokens_30d": null } }
  ]
}
```

Archon is the one provider of `kind: "runs"` — no windows or money, just tallies:

```json
{ "id": "archon", "name": "Archon", "kind": "runs", "status": "ok", "updated_at": 1789234500, "poll_s": 30,
  "runs": { "running": 0, "completed": 33, "failed": 12, "cancelled": 14, "waiting": 0, "total": 59 } }
```

Its `status` is `ok` unless the data is stale/unreachable: the tallies are all-time, so a non-zero `failed`
is permanent and must not turn the LED amber.

`status` ∈ `ok | warn | crit | stale | error`. `error` carries `"message"`. Order of `providers`
is the dial order. Unknown fields are ignored by the device (forward compatible).

`requests_30d` / `tokens_30d` (30-day usage counters, `null` when the provider has no such API)
appear on every provider object regardless of kind; for `money` providers they are mirrored inside
`money` for schema-1 compatibility. The device shows them on the plan line (windows kind) or under
the 30-day spend (money kind). Currently populated by z.AI (always), OpenRouter (management key) and DeepSeek (session token).

## 4. Screens (480x480 round, dark theme)

**Provider page** (one per provider, dial order = list order):
- 12 o'clock: clock (12 h) and WiFi glyph; 6 o'clock: "updated 40 s ago" (grey; amber + ⚠ when stale)
- Icon (64 px) and provider name in the upper third
- Windows kind: two concentric arcs — outer = short window (5 h / daily), inner = long window (7 d). Centre: big `42%` of the short window, under it `resets in 2h 14m`; small line for the long window `7d 18% · resets Tue 3:45 PM`
- Money kind: one arc = remaining / total; centre `$12.34` big, `of $50.00` small, `$21.10 / 30d` under it
- Arc colour by status: ok = provider accent, warn = amber, crit = red, stale = grey, error = red outline + message
- Page change animates as a short rotate/slide in the knob direction

**Archon page** (`kind: "runs"`): the 470 px ring is split into up to four segments sized by share of all runs
(running blue, completed green, failed red, cancelled amber; 5° gaps, 6° minimum so a lone failure stays
visible); the centre is a 2×2 grid of the four counts (zero = grey) over their captions, "N workflow runs"
under the name, and "N waiting" when runs are queued or paused at an approval gate. Stale data dims the ring.

**Overview page** (double-click): mini arcs arranged on a ring with icons (nine now: they shrink to 96 px on r=165 so
neighbours keep ~17 px apart; Archon's arc is the success rate of finished runs, blue while anything is running); centre shows `AI spend / 30d $84`. Rotating from here jumps to that provider's page.

**Boot/offline page**: logo, "connecting to WiFi…", "host unreachable — retrying" with the host URL.

## 5. Interaction

| Input | Action |
|---|---|
| Rotate CW / CCW | Next / previous provider, wrapping; pauses auto-cycle |
| Press | Refresh now: device calls the host's `/api/refresh` (wakes every provider poller), re-reads `/api/status` after 3 s and again after 8 s, showing "refreshing…" in place of the age line meanwhile; pauses auto-cycle for 60 s |
| Double-click | Toggle overview page |
| Idle | Auto-cycle every 8 s, resuming 30 s after the last input |
| LED (GPIO 43) | Breathes in the colour of the **worst** provider status (ok = dim white, warn = amber, crit = red, all stale = slow grey pulse) |
| Touch (CST8xx, polled over I2C every 20 ms) | Only a tap is recognised (down and up within 0.7 s, under 40 px of travel). On the Archon page: open the run list. Run list: open the selected run's graph. Graph: refresh now. Ignored elsewhere. |
| Archon run list | Rotate = select (no wrap); tap = open graph; press or double-click = back to the Archon page. Refetched every 10 s. |
| Archon graph | Rotate = move the focus one node (row by row, left to right), the view pans so the focused row is centred; press = back to the list; double-click = back to the Archon page. Refetched every 8 s; starts on the first running (else first failed) node. Both views pause auto-cycle and close after 5 min without input. |
| Backlight | 80 % PWM; dims to 20 % after 10 min idle at night (11 PM–7 AM), any input restores |

## 6. Firmware layout (PlatformIO, `env:rotary21`)

```
src/main.cpp          setup/loop, task wiring
src/net.cpp/.h        WiFi connect/reconnect, HTTP poll of host, JSON -> model
src/model.h           Provider/Window/Money structs, status enum
src/ui/*.cpp/.h       LVGL screens, arcs, transitions, icons
src/ui_archon.cpp     Archon run list + workflow graph screens; src/archon.h = their data
src/input.cpp/.h      encoder ISR (GPIO 42/4), PCF8574 button, click/double-click, touch tap
src/board.cpp/.h      Arduino_GFX ST7701 bring-up, PCF8574 power sequence, backlight, LED
include/board_pins.h  pin map (from docs/crowpanel-2.1-rotary.md)
include/secrets.h     gitignored: WIFI_SSID, WIFI_PASS, HOST_URL
assets/icons/         PNG -> LVGL C arrays (RGB565A8) in src/ui/icons/
partitions: Elecrow elecrow_s3.csv (10 MB app + SPIFFS)
```

Flashing on Windows: set `PYTHONIOENCODING=utf-8` before `pio run -t upload` (the board is on the
ESP32-S3 native USB port, COM8 here). Without it PlatformIO's console crashes on esptool's Unicode
progress bar under the cp1252 code page and the upload hangs part-way.

## 7. Host layout (`host/`)

```
host/agent.py            entry: scheduler threads + HTTP server on :8787
host/providers/*.py      one adapter per provider: fetch() -> dict, raises on failure
host/status.py           threshold -> status, spend_30d, snapshot bookkeeping
host/config.example.toml keys, thresholds, subscription fees, poll intervals, port
host/config.toml         gitignored
host/state/              snapshots.json, last-good cache (gitignored)
host/install-task.ps1    registers the Task Scheduler job (at logon, hidden)
```

`GET /api/status` (device), `GET /api/refresh` (device on knob press: wakes all pollers, returns at once), `GET /` (tiny human-readable status page for debugging).

Archon drill-down, fetched by the device on demand (not part of the status poll); errors are HTTP 502 `{"error": "..."}`:

- `GET /api/archon/runs` -> `{"now", "active": N, "runs": [{"id", "name", "status", "started_at", "ended_at", "node", "project"}]}`.
  Active runs first (running, paused, pending), then `recent_finished` finished ones. At most `max_runs` (8).
- `GET /api/archon/graph?id=<run id>` -> `{"id", "name", "status", "layers", "width", "lanes_l", "lanes_r", "done", "total",
  "nodes": [{"id", "state", "l", "x", "at"?}], "edges": [[from, to, lane]]}`.

The host does the digest **and the layout**; the device only draws. Archon's run detail is 0.2-4 MB of events whatever the query, so it never goes near the ESP32 (answers are cached 4 s, definitions 60 s).
- Structure: the workflow definition (`GET /api/workflows/<name>` -> `nodes[].id`, `depends_on`).
- State: replay of the run's `node_started / node_completed / node_failed / node_skipped` events, overruled by
  `terminal_record` once the run has ended. A sub-workflow node never reports itself, its children do
  (`review__code`, `ship__triage__triage`, `await-checks.ci-probe`), so their states are rolled up into the parent and the
  active child is passed along as `at`. In a finished run, nodes that never ran are `skipped`.
- Layout: `l` = row (longest path from a root), `x` = column offset in column units (0 = centre), siblings ordered by
  the barycentre of their parents; `nodes` is sorted by `(l, x)`, which is the dial order. An edge to the next row is
  straight (`lane` 0). An edge that skips rows would cut through the nodes in between, so it runs down a vertical lane
  beside the graph: `lane` -k / +k = k-th lane on the left / right; all long edges into one node share a lane.
- Device (`src/ui_archon.cpp`): rows 62 px apart, focused row at y=236, columns `min(170, (420 - lane margins) / width)`,
  painted in a single LVGL draw callback (no per-node widgets); limits 40 nodes / 96 edges / 8 runs (`src/archon.h`).

## 8. Plan

1. **MVP**: host with OpenRouter + DeepSeek + Claude adapters; firmware with WiFi, host poll, all six icons compiled in, provider + overview pages, encoder, auto-cycle, LED, stale handling. Missing providers render as "not configured".
2. Codex, Gemini, z.AI adapters (host only; confirm response schemas on first live call).
3. Polish: night dimming, page transitions, config web page on the device (replace `secrets.h`) if wanted.

## 9. Risks / open items

- Claude/Codex data goes stale when their access tokens expire and the CLI hasn't run to refresh them. v1 shows "stale — run claude/codex". A careful refresh-and-write-back could come later if it bites.
- DeepSeek 30-day requests/tokens need the web session token (undocumented dashboard API); without it they are n/a and cost comes from balance snapshots. OpenRouter's are only available with a management key (`/api/v1/activity` returns 403 for normal keys).
- Undocumented endpoints (Claude, Codex, Gemini, z.AI) can change; adapters are isolated so fixes are one file each, host-side, no reflash.
- PC asleep = stale display. Acceptable per design; an always-on host is a later option.

## Sources

- Claude usage endpoint & rate limiting: https://github.com/anthropics/claude-code/issues/31637 ; single-use refresh tokens: https://github.com/can1357/oh-my-pi/issues/5396
- Codex `/wham/usage`: https://github.com/openai/codex/issues/10869 , https://gist.github.com/johnknott/946546f803686872f2ca4dc822bb0eeb
- Gemini `retrieveUserQuota` (retired): https://github.com/google-gemini/gemini-cli/issues/27363
- Antigravity quota endpoints + OAuth client: https://github.com/skainguyen1412/antigravity-usage ; `/usage` docs: https://antigravity.google/docs/cli/commands/usage/
- z.AI monitor endpoints: https://github.com/guyinwonder168/opencode-glm-quota (unit 3/number 5 = 5-hour, unit 6/number 1 = weekly)
- OpenRouter activity API: https://openrouter.ai/docs/api/api-reference/analytics/get-user-activity
- DeepSeek dashboard API (usage/export + get_user_summary, same token): https://github.com/shajanjp/deepseek-usage
- OpenRouter credits API: https://openrouter.ai/docs/api-reference/limits ; DeepSeek balance: https://api-docs.deepseek.com/api/get-user-balance
