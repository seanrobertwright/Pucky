// Data model mirrored from the host's /api/status (docs/design.md §3).
#pragma once
#include <Arduino.h>
#include <time.h>

#define MAX_PROVIDERS 8
#define MAX_WINDOWS 3

enum class Status : uint8_t { None, Ok, Warn, Crit, Stale, Error };
enum class Kind : uint8_t { None, Windows, Money };

struct Window {
  char label[12];
  float usedPct;
  time_t resetsAt;   // 0 = unknown
};

struct Money {
  float remaining;
  float total;       // 0 = unknown
  float spend30d;
};

struct Provider {
  char id[16];
  char name[24];
  char plan[20];
  char message[64];
  Kind kind;
  Status status;
  time_t updatedAt;
  uint16_t pollS;
  int32_t requests30d;   // 30-day usage counters, either kind; -1 = n/a
  int64_t tokens30d;     // -1 = n/a
  uint8_t nWindows;
  Window windows[MAX_WINDOWS];
  Money money;
};

struct Model {
  bool valid;             // at least one successful poll
  bool hostReachable;     // last poll succeeded
  uint32_t lastOkMs;      // millis() of last successful poll
  time_t hostNow;
  int16_t tzOffsetMin;
  float spend30d;
  uint8_t nProviders;
  Provider providers[MAX_PROVIDERS];
};

inline time_t nowLocal(const Model &m) { return time(nullptr) + (time_t)m.tzOffsetMin * 60; }
