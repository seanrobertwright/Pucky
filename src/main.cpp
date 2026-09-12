// Pucky — main loop: knob navigation, auto-cycle, LED, night dimming.
#include <Arduino.h>
#include <lvgl.h>
#include "board.h"
#include "input.h"
#include "net.h"
#include "ui.h"

static const uint32_t AUTOCYCLE_MS = 8000, IDLE_RESUME_MS = 30000, PRESS_PAUSE_MS = 60000,
                      HOST_STALE_MS = 5 * 60000, UI_TICK_MS = 500, DIM_AFTER_MS = 10 * 60000;

static Model model;
static uint8_t page = 0;
static bool overview = false;
static uint32_t lastInputMs = 0, lastCycleMs = 0, lastTickMs = 0, pauseUntilMs = 0;
static bool dimmed = false, messageShown = true;

static Status worstStatus() {
  Status w = Status::None;
  for (int i = 0; i < model.nProviders; i++) {
    Status s = model.providers[i].status;
    // rank: crit > warn > stale/error > ok
    auto rank = [](Status x) { return x == Status::Crit ? 3 : x == Status::Warn ? 2 :
                                      (x == Status::Stale || x == Status::Error) ? 1 : 0; };
    if (rank(s) > rank(w)) w = s;
  }
  return w;
}

// Single-colour LED: encode state as a breathing pattern.
static void updateLed() {
  uint32_t t = millis();
  Status w = worstStatus();
  float period, lo, hi;
  switch (w) {
    case Status::Crit:  period = 600;  lo = 0;  hi = 100; break;
    case Status::Warn:  period = 1800; lo = 5;  hi = 80;  break;
    case Status::Stale:
    case Status::Error: period = 5000; lo = 0;  hi = 20;  break;
    default:            period = 4000; lo = 3;  hi = 25;  break;
  }
  if (!model.valid) { period = 1200; lo = 0; hi = 40; }
  float ph = fmodf((float)t, period) / period;             // 0..1
  float s = 0.5f - 0.5f * cosf(ph * 2.0f * PI);             // smooth 0..1..0
  boardLed((uint8_t)(lo + (hi - lo) * s));
}

static bool isNight() {
  if (!model.valid) return false;
  time_t l = nowLocal(model);
  struct tm t; gmtime_r(&l, &t);
  return t.tm_hour >= 23 || t.tm_hour < 7;
}

static void render(bool animate) {
  if (overview) uiShowOverview(model, page);
  else uiShowProvider(model, page, animate);
}

static void gotoPage(int delta) {
  if (!model.nProviders) return;
  page = (page + delta + model.nProviders) % model.nProviders;
  overview = false;
  render(true);
}

static void noteInput() {
  lastInputMs = millis();
  if (dimmed) { boardBacklight(80); dimmed = false; }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[pucky] boot");
  boardInit();
  uiInit();
  inputInit();
  uiShowMessage("Connecting", "joining WiFi…");
  netStart();
  lastInputMs = lastCycleMs = millis();
}

void loop() {
  lv_timer_handler();
  uint32_t now = millis();

  // --- input
  switch (inputPoll()) {
    case InputEvent::CW:  noteInput(); gotoPage(+1); break;
    case InputEvent::CCW: noteInput(); gotoPage(-1); break;
    case InputEvent::Click:
      noteInput();
      pauseUntilMs = now + PRESS_PAUSE_MS;
      netRequestRefresh();
      break;
    case InputEvent::DoubleClick:
      noteInput();
      overview = !overview;
      render(false);
      break;
    case InputEvent::LongPress: noteInput(); break;
    default: break;
  }

  // --- periodic: model refresh, clock, LED, auto-cycle
  if (now - lastTickMs >= UI_TICK_MS) {
    lastTickMs = now;
    Model fresh;
    netGetModel(fresh);
    bool becameValid = fresh.valid && !model.valid;
    bool changed = fresh.valid && fresh.lastOkMs != model.lastOkMs;
    model = fresh;

    if (!netWifiConnected()) {
      if (!messageShown) { uiShowMessage("No WiFi", "reconnecting…"); messageShown = true; }
    } else if (!model.valid) {
      if (!messageShown || becameValid) { uiShowMessage("Connecting to host", "waiting for first poll…"); messageShown = true; }
    } else if (!model.hostReachable && now - model.lastOkMs > HOST_STALE_MS && !messageShown) {
      uiShowMessage("Host unreachable", "showing last known data"); messageShown = true;
    } else if (messageShown && model.valid && (model.hostReachable || now - model.lastOkMs <= HOST_STALE_MS)) {
      uiHideMessage(); messageShown = false;
      render(true);
    }

    if (model.valid && (becameValid || changed)) render(false);
    else uiTick(model, page, overview);

    if (model.valid && !overview && now > pauseUntilMs && now - lastInputMs > IDLE_RESUME_MS &&
        now - lastCycleMs >= AUTOCYCLE_MS) {
      lastCycleMs = now;
      gotoPage(+1);
    }
    if (!dimmed && isNight() && now - lastInputMs > DIM_AFTER_MS) { boardBacklight(20); dimmed = true; }
  }

  updateLed();
  delay(5);
}
