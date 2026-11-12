// Rotary encoder (GPIO interrupts) + push button (PCF8574, polled) + touch panel (polled) -> discrete events.
#pragma once
#include <Arduino.h>

enum class InputEvent : uint8_t { None, CW, CCW, Click, DoubleClick, LongPress, Tap };   // Tap = screen touched and released

void inputInit();
InputEvent inputPoll();   // call often from loop(); returns at most one event per call
