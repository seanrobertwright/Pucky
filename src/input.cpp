#include "input.h"
#include "board.h"
#include "board_pins.h"

// ---- encoder: full quadrature decode, one event per detent (idle state = both high) ----
static volatile int8_t encDelta = 0;      // accumulated ±1 transitions since last idle
static volatile int8_t encPending = 0;    // events waiting: +n CW, -n CCW
static volatile uint8_t encPrev = 3;

static const int8_t QDEC[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR encIsr() {
  uint8_t cur = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encDelta += QDEC[(encPrev << 2) | cur];
  encPrev = cur;
  if (cur == 3) {                          // back at a detent
    if (encDelta > 0) encPending++;
    else if (encDelta < 0) encPending--;
    encDelta = 0;
  }
}

// ---- button: debounce + click / double-click / long-press ----
static const uint32_t DEBOUNCE_MS = 30, DOUBLE_MS = 300, LONG_MS = 800, POLL_MS = 10;
static bool btnRaw = false, btnStable = false;
static uint32_t btnChangeMs = 0, btnDownMs = 0, btnUpMs = 0, lastPollMs = 0;
static uint8_t clicks = 0;
static bool longFired = false;

// ---- touch: a tap is finger down then up within TAP_MAX_MS; held or dragged touches are ignored ----
static const uint32_t TAP_MAX_MS = 700, TAP_GAP_MS = 250, TOUCH_POLL_MS = 20;
static const int TAP_MOVE_PX = 40;
static bool touchDown = false;
static uint32_t touchDownMs = 0, lastTapMs = 0, lastTouchPollMs = 0, touchSeenMs = 0;
static uint16_t touchX0 = 0, touchY0 = 0, touchX = 0, touchY = 0;

static bool touchPoll(uint32_t now) {
  if (now - lastTouchPollMs < TOUCH_POLL_MS) return false;
  lastTouchPollMs = now;
  uint16_t x, y;
  if (boardTouch(x, y)) {
    if (!touchDown) { touchDown = true; touchDownMs = now; touchX0 = x; touchY0 = y; }
    touchX = x; touchY = y; touchSeenMs = now;
    return false;
  }
  if (!touchDown || now - touchSeenMs < 40) return false;      // ride out a single missed read
  touchDown = false;
  bool still = abs((int)touchX - touchX0) <= TAP_MOVE_PX && abs((int)touchY - touchY0) <= TAP_MOVE_PX;
  bool tap = touchSeenMs - touchDownMs <= TAP_MAX_MS && still && now - lastTapMs >= TAP_GAP_MS;
  Serial.printf("[touch] %s at %u,%u after %lu ms\n", tap ? "tap" : "ignored", touchX, touchY,
                (unsigned long)(touchSeenMs - touchDownMs));
  if (tap) lastTapMs = now;
  return tap;
}

void inputInit() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  encPrev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encIsr, CHANGE);
}

InputEvent inputPoll() {
  noInterrupts();
  int8_t pend = encPending;
  if (pend > 0) encPending--; else if (pend < 0) encPending++;
  interrupts();
  if (pend > 0) return InputEvent::CW;
  if (pend < 0) return InputEvent::CCW;

  uint32_t now = millis();
  if (touchPoll(now)) return InputEvent::Tap;
  if (now - lastPollMs < POLL_MS) return InputEvent::None;
  lastPollMs = now;

  bool raw = boardButtonPressed();
  if (raw != btnRaw) { btnRaw = raw; btnChangeMs = now; }
  if (now - btnChangeMs >= DEBOUNCE_MS && btnStable != btnRaw) {
    btnStable = btnRaw;
    if (btnStable) { btnDownMs = now; longFired = false; }
    else if (!longFired) { clicks++; btnUpMs = now; }
  }
  if (btnStable && !longFired && now - btnDownMs >= LONG_MS) {
    longFired = true; clicks = 0;
    return InputEvent::LongPress;
  }
  if (clicks >= 2) { clicks = 0; return InputEvent::DoubleClick; }
  if (clicks == 1 && !btnStable && now - btnUpMs >= DOUBLE_MS) {
    clicks = 0; return InputEvent::Click;
  }
  return InputEvent::None;
}
