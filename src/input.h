// Rotary encoder (GPIO interrupts) + push button (PCF8574, polled) -> discrete events.
#pragma once
#include <Arduino.h>

enum class InputEvent : uint8_t { None, CW, CCW, Click, DoubleClick, LongPress };

void inputInit();
InputEvent inputPoll();   // call often from loop(); returns at most one event per call
