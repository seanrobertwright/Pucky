#include "net.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "secrets.h"

static const uint32_t POLL_MS = 60000, HTTP_TIMEOUT_MS = 5000;
// After a press: hit /api/refresh, then read status once the providers have had time to answer
// (~1-3 s each, in parallel), and once more for stragglers.
static const uint32_t REFRESH_FIRST_MS = 3000, REFRESH_SECOND_MS = 8000;

static Model shared = {};
static SemaphoreHandle_t mtx;
static volatile bool refreshNow = false;
static volatile bool refreshing = false;

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
    } else if (!strcmp(kind, "runs")) {
      pr.kind = Kind::Runs;
      JsonObject r = p["runs"];
      pr.runs.running = r["running"] | 0u;
      pr.runs.completed = r["completed"] | 0u;
      pr.runs.failed = r["failed"] | 0u;
      pr.runs.cancelled = r["cancelled"] | 0u;
      pr.runs.waiting = r["waiting"] | 0u;
      pr.runs.total = r["total"] | 0u;
    } else {
      pr.kind = Kind::None;
    }
    if (!p["requests_30d"].isNull()) pr.requests30d = p["requests_30d"];
    if (!p["tokens_30d"].isNull()) pr.tokens30d = p["tokens_30d"];
  }
  return true;
}

// HOST_URL points at /api/status; the refresh endpoint lives next to it.
static String refreshUrl() {
  String u = HOST_URL;
  int i = u.lastIndexOf("/api/status");
  if (i >= 0) u = u.substring(0, i) + "/api/refresh";
  return u;
}

// ------------------------------------------------------------------ Archon drill-down
static String apiUrl(const char *path) {
  String u = HOST_URL;
  int i = u.lastIndexOf("/api/status");
  if (i >= 0) u = u.substring(0, i);
  return u + path;
}

static ArRuns arRuns = {};        // guarded by mtx; too big for a task stack, hence static
static ArGraph arGraph = {};
static ArRuns arRunsTmp;
static ArGraph arGraphTmp;
static volatile bool wantRuns = false, wantGraph = false;
static volatile uint32_t arRunsSeq = 0, arGraphSeq = 0;
static char wantGraphId[40] = "";

static NodeState parseNodeState(const char *s) {
  if (!s) return NodeState::Pending;
  if (!strcmp(s, "running")) return NodeState::Running;
  if (!strcmp(s, "completed")) return NodeState::Completed;
  if (!strcmp(s, "failed")) return NodeState::Failed;
  if (!strcmp(s, "skipped")) return NodeState::Skipped;
  if (!strcmp(s, "cancelled")) return NodeState::Cancelled;
  return NodeState::Pending;
}

// GET a host JSON endpoint. On failure `err` gets the host's {"error": ...} text or the HTTP code.
static bool getJson(const String &url, uint32_t timeoutMs, JsonDocument &doc, char *err, size_t errLen) {
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  err[0] = 0;
  if (!http.begin(url)) { copyStr(err, errLen, "bad host URL"); return false; }
  int code = http.GET();
  bool ok = false;
  if (code > 0) {
    DeserializationError de = deserializeJson(doc, http.getString());
    if (code == 200 && !de) ok = true;
    else if (!de && doc["error"].is<const char *>()) copyStr(err, errLen, doc["error"]);
    else snprintf(err, errLen, de ? "bad answer from host (HTTP %d)" : "host said HTTP %d", code);
  } else {
    copyStr(err, errLen, "host unreachable");
  }
  http.end();
  Serial.printf("[net] GET %s -> %d%s%s\n", url.c_str(), code, ok ? "" : "  ", ok ? "" : err);
  return ok;
}

static void fetchRuns() {
  ArRuns &r = arRunsTmp;
  memset(&r, 0, sizeof r);
  JsonDocument doc;
  r.ok = getJson(apiUrl("/api/archon/runs"), 8000, doc, r.error, sizeof r.error);
  if (r.ok) {
    r.nActive = doc["active"] | 0;
    for (JsonObject o : doc["runs"].as<JsonArray>()) {
      if (r.n >= AR_MAX_RUNS) break;
      ArRun &run = r.runs[r.n++];
      copyStr(run.id, sizeof run.id, o["id"]);
      copyStr(run.name, sizeof run.name, o["name"]);
      copyStr(run.status, sizeof run.status, o["status"]);
      copyStr(run.node, sizeof run.node, o["node"]);
      copyStr(run.project, sizeof run.project, o["project"]);
      run.startedAt = o["started_at"] | 0L;
      run.endedAt = o["ended_at"] | 0L;
    }
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  arRunsSeq = arRunsSeq + 1;   // only this task writes it
  r.seq = arRunsSeq;
  arRuns = r;
  xSemaphoreGive(mtx);
}

static void fetchGraph() {
  ArGraph &gr = arGraphTmp;
  memset(&gr, 0, sizeof gr);
  char id[40];
  xSemaphoreTake(mtx, portMAX_DELAY);
  copyStr(id, sizeof id, wantGraphId);
  xSemaphoreGive(mtx);
  JsonDocument doc;
  // Archon needs ~2 s to produce a run's detail and the host adds the layout: allow well over that
  gr.ok = getJson(apiUrl("/api/archon/graph?id=") + id, 15000, doc, gr.error, sizeof gr.error);
  copyStr(gr.id, sizeof gr.id, id);
  if (gr.ok) {
    copyStr(gr.name, sizeof gr.name, doc["name"]);
    copyStr(gr.status, sizeof gr.status, doc["status"]);
    gr.startedAt = doc["started_at"] | 0L;
    gr.endedAt = doc["ended_at"] | 0L;
    gr.layers = doc["layers"] | 0;
    gr.width = doc["width"] | 1;
    gr.lanesL = doc["lanes_l"] | 0;
    gr.lanesR = doc["lanes_r"] | 0;
    gr.done = doc["done"] | 0;
    gr.total = doc["total"] | 0;
    for (JsonObject o : doc["nodes"].as<JsonArray>()) {
      if (gr.nNodes >= AR_MAX_NODES) break;
      ArNode &n = gr.nodes[gr.nNodes++];
      copyStr(n.label, sizeof n.label, o["id"]);
      copyStr(n.at, sizeof n.at, o["at"]);
      n.state = parseNodeState(o["state"]);
      n.layer = o["l"] | 0;
      n.x = o["x"] | 0.0f;
    }
    for (JsonArray a : doc["edges"].as<JsonArray>()) {
      if (gr.nEdges >= AR_MAX_EDGES) break;
      int from = a[0] | -1, to = a[1] | -1;
      if (from < 0 || to < 0 || from >= gr.nNodes || to >= gr.nNodes) continue;   // truncated node list
      ArEdge &ed = gr.edges[gr.nEdges++];
      ed.from = from;
      ed.to = to;
      ed.lane = a[2] | 0;
    }
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  arGraphSeq = arGraphSeq + 1;
  gr.seq = arGraphSeq;
  arGraph = gr;
  xSemaphoreGive(mtx);
}

static void askHostToRefresh() {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  String u = refreshUrl();
  if (!http.begin(u)) return;
  int code = http.GET();
  Serial.printf("[net] GET %s -> %d\n", u.c_str(), code);
  http.end();
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
  uint32_t nextPoll = 0;            // millis() at which to read status next
  uint32_t followUp = 0;            // second read after a refresh (0 = none pending)
  bool first = true;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (first) { Serial.printf("[net] wifi up, ip %s\n", WiFi.localIP().toString().c_str()); first = false; }
    uint32_t now = millis();
    if (refreshNow) {
      refreshNow = false;
      refreshing = true;
      askHostToRefresh();
      now = millis();
      nextPoll = now + REFRESH_FIRST_MS;
      followUp = now + REFRESH_SECOND_MS;
    }
    if (wantRuns) { wantRuns = false; fetchRuns(); now = millis(); }
    if (wantGraph) { wantGraph = false; fetchGraph(); now = millis(); }
    if (nextPoll == 0 || (int32_t)(now - nextPoll) >= 0) {
      if (followUp && (int32_t)(now - followUp) >= 0) { followUp = 0; refreshing = false; }
      nextPoll = followUp ? followUp : now + POLL_MS;
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

void netArchonRequestRuns() { wantRuns = true; }

void netArchonRequestGraph(const char *runId) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  copyStr(wantGraphId, sizeof wantGraphId, runId);
  xSemaphoreGive(mtx);
  wantGraph = true;
}

uint32_t netArchonRunsSeq() { return arRunsSeq; }
uint32_t netArchonGraphSeq() { return arGraphSeq; }

void netArchonGetRuns(ArRuns &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  out = arRuns;
  xSemaphoreGive(mtx);
}

void netArchonGetGraph(ArGraph &out) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  out = arGraph;
  xSemaphoreGive(mtx);
}

void netRequestRefresh() { refreshNow = true; }
bool netRefreshing() { return refreshing; }
bool netWifiConnected() { return WiFi.status() == WL_CONNECTED; }
int netRssi() { return WiFi.RSSI(); }
