// WiFi + host polling on a background task; hands out copies of the Model.
#pragma once
#include "model.h"

void netStart();
void netGetModel(Model &out);        // thread-safe copy
void netRequestRefresh();            // poll now instead of waiting for the interval
bool netWifiConnected();
int  netRssi();
