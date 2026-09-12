// Display, I/O expander, backlight and LED bring-up for the CrowPanel 2.1" rotary.
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

extern Arduino_RGB_Display *gfx;

void boardInit();
void boardBacklight(uint8_t pct);        // 0..100
void boardLed(uint8_t pct);              // 0..100
bool boardButtonPressed();               // encoder push, via PCF8574 P5
