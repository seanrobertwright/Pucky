// LVGL screens: provider page, overview page, boot/offline overlay.
#pragma once
#include "model.h"

void uiInit();                                             // LVGL + display driver
void uiShowProvider(const Model &m, uint8_t idx, bool animate);
void uiShowOverview(const Model &m, uint8_t currentIdx);
void uiTick(const Model &m, uint8_t idx, bool overview);   // refresh clock/ago/wifi, ~2 Hz
void uiShowMessage(const char *line1, const char *line2); // boot / offline overlay
void uiHideMessage();
