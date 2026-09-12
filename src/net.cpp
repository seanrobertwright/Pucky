#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "secrets.h"

static const uint32_t POLL_MS = 60000, HTTP_TIMEOUT_MS = 5000;

static Model shared = {};
static SemaphoreHandle_t mtx;
static volatile bool refreshNow = false;

static Status parseStatus(const char *s) {
  if (!s) return Status::Error;
  if (!strcmp(s, "ok")) return Status::Ok;
  if (!strcmp(s, "warn")) return Status::Warn;
  if (!strcmp(s, "crit")) return Status::Crit;
  if (!strcmp(s, "stale")) return Status::Stale;
  return Status::Error;
}

static void copyStr(char *dst, size_t n, const char *src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
}

static bool parseInto(Model &m, const String &body) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { Serial.printf("[net] json: %s\n", err.c_str()); return false; }

  m.hostNow = doc["now"] | 0L;
  m.tzOffsetMin = doc["tz_offset_min"] | 0;
  m.spend30d = doc["spend_30d_usd"] | 0.0f;
  m.nProviders = 0;
  for (JsonObject p : doc["providers"].as<JsonArray>()) {
    if (m.nProviders >= MAX_PROVIDERS) break;
    Provider &pr = m.providers[m.nProviders++];
    memset(&pr, 0, sizeof pr);
    copyStr(pr.id, sizeof pr.id, p["id"]);
    copyStr(pr.name, sizeof pr.name, p["name"]);
    copyStr(pr.plan, sizeof pr.plan, p["plan"]);
    copyStr(pr.message, sizeof pr.message, p["message"]);
    pr.status = parseStatus(p["status"]);
    pr.updatedAt = p["updated_at"] | 0L;
    pr.pollS = p["poll_s"] | 300;
    pr.requests30d = -1;
    pr.tokens30d = -1;
    const char *kind = p["kind"] | "none";
    if (!strcmp(kind, "windows")) {
      pr.kind = Kind::Windows;
      for (JsonObject w : p["windows"].as<JsonArray>()) {
        if (pr.nWindows >= MAX_WINDOWS) break;
        Window &win = pr.windows[pr.nWindows++];
        copyStr(win.label, sizeof win.label, w["label"]);
        win.usedPct = w["used_pct"] | 0.0f;
        win.resetsAt = w["resets_at"] | 0L;
      }
    } else if (!strcmp(kind, "money")) {
      pr.kind = Kind::Money;
      JsonObject mo = p["money"];
      pr.money.remaining = mo["remaining_usd"] | 0.0f;
      pr.money.total = mo["total_usd"] | 0.0f;
      pr.money.spend30d = mo["spend_30d_usd"] | 0.0f;
      // schema 1 carried the counters inside "money"; newer hosts also put them on the provider
      if (!mo["requests_30d"].isNull()) pr.requests30d = mo["requests_30d"];
      if (!mo["tokens_30d"].isNull()) pr.tokens30d = mo["tokens_30d"];
    } else {
      pr.kind = Kind::None;
    }
    if (!p["requests_30d"].isNull()) pr.requests30d = p["requests_30d"];
    if (!p["tokens_30d"].isNull()) pr.tokens30d = p["tokens_30d"];
  }
  return true;
}

static bool pollOnce(Model &m) {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(HOST_URL)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    ok = parseInto(m, http.getString());
  } else {
    Serial.printf("[net] GET %s -> %d\n", HOST_URL, code);
  }
  http.end();
  return ok;
}

static void netTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t lastPoll = 0;
  bool first = true;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (first) { Serial.printf("[net] wifi up, ip %s\n", WiFi.localIP().toString().c_str()); first = false; }
    uint32_t now = millis();
    if (refreshNow || lastPoll == 0 || now - lastPoll >= POLL_MS) {
      refreshNow = false;
      lastPoll = now;
      Model fresh = {};
      bool ok = pollOnce(fresh);
      xSemaphoreTake(mtx, portMAX_DELAY);
      if (ok) {
        fresh.valid = true;
        fresh.hostReachable = true;
        fresh.lastOkMs = millis();
        shared = fresh;
        struct timeval tv = {.tv_sec = fresh.hostNow, .tv_usec = 0};
        settimeofday(&tv, nullptr);
      } else {
        shared.hostReachable = false;
      }
      xSemaphoreGive(mtx);
      Serial.printf("[net] poll %s (%u providers)\n", ok ? "ok" : "FAILED", fresh.nProviders);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void netStart() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}

void netGetModel(Model &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  out = shared;
  xSemaphoreGive(mtx);
}

void netRequestRefresh() { refreshNow = true; }
bool netWifiConnected() { return WiFi.status() == WL_CONNECTED; }
int netRssi() { return WiFi.RSSI(); }
