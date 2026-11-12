<p align="center">
  <img src="assets/pucky-banner.svg" width="560" alt="PUCKY">
</p>

# Pucky

A desk knob that shows how much of your AI subscriptions and credits you've burned through.
Turn the dial to switch between **Claude Code, Codex, GitHub Copilot, Kiro, OpenRouter,
Antigravity (Gemini), z.AI GLM and DeepSeek**; each page shows usage rings, reset times, balances and 30-day spend, with a
breathing LED that changes rhythm when something is running low. One extra page, **Archon**, isn't a
usage meter: it counts your [Archon](https://github.com/coleam00/Archon) workflow runs by state
(running / completed / failed / cancelled). **Touch that page** to list the active runs, turn the dial
to pick one and touch again to open its workflow graph: nodes coloured by state, the dial walks from
node to node and the view pans to follow. Press the knob to step back out.

It runs on an [Elecrow CrowPanel 2.1" rotary display](docs/crowpanel-2.1-rotary.md) (ESP32-S3,
480×480 round IPS, rotary knob with push) and is fed by a tiny Python agent on your PC. A
3D-printable desk dock is included, modelled and verified with the `3d-skills` toolkit's
intent → model → measure loop (the `lril3d-*` skills in `.claude/skills/`).

## How it works

```
 PC (host/agent.py, Python stdlib)                  Display (ESP32-S3, LVGL 9)
 ┌──────────────────────────────────┐   HTTP/JSON   ┌──────────────────────────┐
 │ Claude Code  ─ ~/.claude token   │  every 60 s   │ rotate  → next provider  │
 │ Codex        ─ ~/.codex token    │ ────────────▶ │ press   → refresh now    │
 │ Antigravity  ─ OS keyring token  │               │ 2×press → overview page  │
 │ z.AI / OpenRouter / DeepSeek     │               │ idle    → auto-cycle 8 s │
 │   ─ API keys in config.toml      │               │ LED     → worst status   │
 └──────────────────────────────────┘               └──────────────────────────┘
```

The host polls each provider on its own schedule, computes an `ok / warn / crit / stale`
status against your thresholds, and serves one JSON document on `http://<pc>:8787/api/status`.
The display only renders it. Adding or fixing a provider is a Python change — no reflash.

> [!IMPORTANT]
> Claude Code and Codex use single-use OAuth refresh tokens. The host reads the access token
> the CLIs keep fresh and **never refreshes it itself** — a second refresher would disable your
> login. This is why the data lives on the PC rather than on the device.

## What each page shows

| Provider | Data | Source |
|---|---|---|
| Claude Code (Pro/Max) | 5-hour and 7-day window %, reset times | Claude Code's login, read-only |
| Codex (ChatGPT plan) | 5-hour / weekly window %, reset times | Codex's login, read-only |
| GitHub Copilot (Pro/Pro+/Business) | premium requests used of the monthly allowance, reset date | `gh` CLI login, read-only |
| Kiro (Free/Pro/Pro+/Power) | monthly plan credits %, bonus credits, overage, reset date | Kiro IDE login, read-only |
| Antigravity (Google AI Pro) | quota pools with 5-hour windows | `agy` login in the OS keyring, read-only |
| z.AI GLM Coding Plan | 5-hour and weekly quota %, monthly MCP quota, 30-day calls/tokens | API key |
| OpenRouter | credits left, 30-day spend (+ requests/tokens with a management key) | API key |
| DeepSeek | balance, 30-day cost/requests/tokens | API key (+ optional dashboard session token) |
| Archon | workflow runs: how many are running, completed, failed, cancelled (all-time, as on Archon's dashboard) | local Archon backend on `localhost:3090`, no auth |

Every page also shows the clock, Wi-Fi state and "updated N s ago"; the overview page adds
total AI spend over 30 days (pay-per-use spend plus any subscription fees you enter).

## Hardware

- **Elecrow CrowPanel 2.1" HMI Rotary Display** (DHE03921D) — ESP32-S3R8, 16 MB flash,
  8 MB PSRAM, ST7701 480×480 RGB panel, CST8xx touch, hollow-shaft encoder, native USB.
  Sold on Amazon under several names (e.g. "IoTeikXgo 2.1 Inch ESP32 Rotary Display").
- A PC on the same Wi-Fi that stays on while you want live data (Windows; the agent itself
  is portable Python).
- Optional: a Bambu P1S (or any FDM printer) for the dock.

Pin map, module geometry and vendor references are in [`docs/`](docs/).

## Getting started

### 1. Host agent

Requires Python 3.11+ (stdlib only, nothing to install).

```powershell
cd host
Copy-Item config.example.toml config.toml   # then edit: API keys, thresholds, subscription fees
python agent.py --once                       # fetch everything once and print the JSON
python agent.py                              # serve http://0.0.0.0:8787/
.\install-task.ps1                           # run at logon as the hidden "Pucky Host" Task Scheduler job
```

Open `http://localhost:8787/` for a human-readable status table. Allow inbound TCP 8787 in
Windows Firewall so the display can reach it:

```powershell
New-NetFirewallRule -DisplayName "Pucky Host (8787)" -Direction Inbound -Protocol TCP -LocalPort 8787 -Action Allow
```

> [!TIP]
> Give the PC a DHCP reservation on your router; the display polls a fixed address.

### 2. Firmware

Requires [PlatformIO](https://platformio.org/) with the `espressif32` (pioarduino) platform;
libraries (Arduino_GFX, LVGL 9, ArduinoJson) are pulled in automatically.

```powershell
Copy-Item include\secrets.example.h include\secrets.h   # Wi-Fi SSID/password + host URL
pio run -t upload                                       # port is auto-detected
pio device monitor                                      # 115200 baud over native USB
```

The display shows *Connecting → joining Wi-Fi* and then the first provider page. The ESP32-S3
is 2.4 GHz only.

> [!NOTE]
> The first upload replaces whatever firmware the board shipped with. Back it up first if you
> care about it: `esptool --port COMx --baud 921600 read-flash 0 0x1000000 backup.bin`.

### 3. Configuration reference

`host/config.toml`:

| Key | Purpose |
|---|---|
| `order` | Dial order of providers (clockwise) |
| `[thresholds]` | `window_warn_pct` / `window_crit_pct` (default 80 / 95), `money_warn_usd` / `money_crit_usd` (5 / 2) |
| `[subscriptions]` | Fixed monthly fees added to the 30-day spend figure |
| `[providers.<id>]` | `enabled`, `poll_s`, and the credentials each adapter needs (see the comments in `config.example.toml`) |

`recent_finished = N` under `[providers.archon]` also lists the N most recent finished runs in the
touch view, so there is a graph to open while nothing is running.

Archon needs no key either: the host reads its local backend (`base_url`, default `http://localhost:3090`),
and shows "Archon not reachable" when it isn't running. `max_backoff_s` caps how long the host waits
before retrying a failing provider (default 1800 s; the example config uses 120 s for Archon).

Claude, Codex, Copilot, Kiro and Antigravity need no keys — they reuse the CLI/IDE logins on
the PC (Kiro's token is only refreshed by the Kiro IDE itself, so open Kiro now and then).
Restart the agent (or the scheduled task) after editing.

## The dock

`enclosure/` holds a two-part, support-free PLA dock: a flat **cradle** that holds the module by
its three brass standoffs (the whole 80 mm body is the knob, so nothing may touch it) and a
**base** puck with a 15° saddle, a trough the knob dips into, two M3 heat-set inserts and a cable
groove out the back.

Both parts are parametric [build123d](https://build123d.readthedocs.io/) models with an
`intent.json` that is measured, not eyeballed — every dimension cites a caliper reading or the
parts database, and the exported STEP/STL/3MF only ship after the intent check and DFM report
pass. Sliced for a Bambu P1S: cradle 35 min / 25 g, base 1 h 24 min / 72 g.

```powershell
cd enclosure
uv run --project <path-to-3d-skills> python run.py     # export, measure, DFM, render both parts
```

Change a dimension in `params.json` and rerun; never edit the geometry by hand.

## Project layout

```
host/            Python agent: agent.py, status.py, providers/<id>.py, config.example.toml
src/             ESP32 firmware: board bring-up, encoder/button, network task, LVGL UI, icons
include/         board_pins.h, lv_conf.h, secrets.example.h
partitions/      Elecrow 16 MB partition table
assets/icons/    provider logos (lobehub icon set, MIT) as SVG/PNG
enclosure/       cradle/ and base/ models, intents, params, renders; run.py
docs/            design notes, board reference, module geometry, measurement guides
firmware/        vendor factory images for the boards used during development
PROMPTS.md       every prompt used to build this with Claude Code
```

## Documentation

- [`docs/design.md`](docs/design.md) — decisions, data contract, screens, interaction model
- [`docs/crowpanel-2.1-rotary.md`](docs/crowpanel-2.1-rotary.md) — board identification and pin map
- [`docs/elecrow21-module-geometry.md`](docs/elecrow21-module-geometry.md) — measured module dimensions
- [`assets/icons/README.md`](assets/icons/README.md) — icon sources and provider mapping
