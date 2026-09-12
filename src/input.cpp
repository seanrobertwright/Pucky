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
