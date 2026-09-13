// WiFi + host polling on a background task; hands out copies of the Model.
#pragma once
#include "model.h"

void netStart();
void netGetModel(Model &out);        // thread-safe copy
void netRequestRefresh();            // knob press: make the host re-poll every provider, then re-read it
bool netRefreshing();                // true from the press until the follow-up read is done
bool netWifiConnected();
int  netRssi();
