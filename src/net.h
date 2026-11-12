// WiFi + host polling on a background task; hands out copies of the Model.
#pragma once
#include "model.h"
#include "archon.h"

void netStart();
void netGetModel(Model &out);        // thread-safe copy
void netRequestRefresh();            // knob press: make the host re-poll every provider, then re-read it
bool netRefreshing();                // true from the press until the follow-up read is done
bool netWifiConnected();
int  netRssi();

// Archon drill-down: ask, then watch the sequence number; it moves when the answer (or the failure) is in.
void netArchonRequestRuns();
void netArchonRequestGraph(const char *runId);
uint32_t netArchonRunsSeq();
uint32_t netArchonGraphSeq();
void netArchonGetRuns(ArRuns &out);      // thread-safe copies
void netArchonGetGraph(ArGraph &out);
