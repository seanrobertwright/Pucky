// Pucky — main loop: knob navigation, auto-cycle, LED, night dimming.
#include <Arduino.h>
#include <lvgl.h>
#include "board.h"
#include "input.h"
#include "net.h"
#include "ui.h"

static const uint32_t AUTOCYCLE_MS = 8000, IDLE_RESUME_MS = 30000, PRESS_PAUSE_MS = 60000,
                      HOST_STALE_MS = 5 * 60000, UI_TICK_MS = 500, DIM_AFTER_MS = 10 * 60000;

static Model model;
static uint8_t page = 0;
static bool overview = false;
static uint32_t lastInputMs = 0, lastCycleMs = 0, lastTickMs = 0, pauseUntilMs = 0;
static bool dimmed = false, messageShown = true;

// Archon drill-down, opened by touching the Archon page: run list -> workflow graph.
enum class Sub : uint8_t { None, Runs, Graph };
static const uint32_t RUNS_REFRESH_MS = 10000, GRAPH_REFRESH_MS = 8000, SUB_IDLE_MS = 5 * 60000;
static Sub sub = Sub::None;
static ArRuns arRuns;             // static: these are kilobytes, keep them off the loop stack
static ArGraph arGraph, arGraphNew;
static int runSel = 0, nodeFocus = 0;
static bool subLoading = false, focusPlaced = false;
static uint32_t runsSeqSeen = 0, graphSeqSeen = 0, subFetchMs = 0;

static Status worstStatus() {
  Status w = Status::None;
  for (int i = 0; i < model.nProviders; i++) {
    Status s = model.providers[i].status;
    // rank: crit > warn > stale/error > ok
    auto rank = [](Status x) { return x == Status::Crit ? 3 : x == Status::Warn ? 2 :
                                      (x == Status::Stale || x == Status::Error) ? 1 : 0; };
    if (rank(s) > rank(w)) w = s;
  }
  return w;
}

// Single-colour LED: encode state as a breathing pattern.
static void updateLed() {
  uint32_t t = millis();
  Status w = worstStatus();
  float period, lo, hi;
  switch (w) {
    case Status::Crit:  period = 600;  lo = 0;  hi = 100; break;
    case Status::Warn:  period = 1800; lo = 5;  hi = 80;  break;
    case Status::Stale:
    case Status::Error: period = 5000; lo = 0;  hi = 20;  break;
    default:            period = 4000; lo = 3;  hi = 25;  break;
  }
  if (!model.valid) { period = 1200; lo = 0; hi = 40; }
  float ph = fmodf((float)t, period) / period;             // 0..1
  float s = 0.5f - 0.5f * cosf(ph * 2.0f * PI);             // smooth 0..1..0
  boardLed((uint8_t)(lo + (hi - lo) * s));
}

static bool isNight() {
  if (!model.valid) return false;
  time_t l = nowLocal(model);
  struct tm t; gmtime_r(&l, &t);
  return t.tm_hour >= 23 || t.tm_hour < 7;
}

static void render(bool animate) {
  if (sub == Sub::Runs) { uiShowRunList(arRuns, runSel, subLoading); return; }
  if (sub == Sub::Graph) { uiShowRunGraph(arGraph, nodeFocus, subLoading); return; }
  if (overview) uiShowOverview(model, page);
  else uiShowProvider(model, page, animate);
}

static void gotoPage(int delta) {
  if (!model.nProviders) return;
  page = (page + delta + model.nProviders) % model.nProviders;
  overview = false;
  render(true);
}

static bool onArchonPage() {
  return model.valid && !overview && page < model.nProviders && model.providers[page].kind == Kind::Runs;
}

static void enterRuns() {
  sub = Sub::Runs;
  subLoading = true;
  runsSeqSeen = netArchonRunsSeq();
  netArchonRequestRuns();
  subFetchMs = millis();
  render(false);                  // shows the previous list at once if there is one, else "Loading"
}

static void enterGraph() {
  if (!arRuns.ok || runSel >= arRuns.n) return;
  const ArRun &r = arRuns.runs[runSel];
  memset(&arGraph, 0, sizeof arGraph);
  strlcpy(arGraph.id, r.id, sizeof arGraph.id);       // header shows the name while the graph loads
  strlcpy(arGraph.name, r.name, sizeof arGraph.name);
  strlcpy(arGraph.status, r.status, sizeof arGraph.status);
  sub = Sub::Graph;
  subLoading = true;
  focusPlaced = false;
  nodeFocus = 0;
  graphSeqSeen = netArchonGraphSeq();
  netArchonRequestGraph(r.id);
  subFetchMs = millis();
  render(false);
}

static void exitSub() {
  sub = Sub::None;
  subLoading = false;
  lastCycleMs = millis();         // give the Archon page a full dwell before auto-cycle moves on
  render(true);
}

// Pick up finished fetches and keep the open view fresh. Called at the UI tick rate.
static void subTick(uint32_t now) {
  if (sub == Sub::Runs) {
    if (netArchonRunsSeq() != runsSeqSeen) {
      char keep[40] = "";
      if (arRuns.ok && runSel < arRuns.n) strlcpy(keep, arRuns.runs[runSel].id, sizeof keep);
      netArchonGetRuns(arRuns);
      runsSeqSeen = arRuns.seq;
      subLoading = false;
      for (int i = 0; keep[0] && i < arRuns.n; i++)   // the list reorders as runs start and finish
        if (!strcmp(arRuns.runs[i].id, keep)) runSel = i;
      if (runSel >= arRuns.n) runSel = arRuns.n ? arRuns.n - 1 : 0;
      render(false);
    } else if (now - subFetchMs >= RUNS_REFRESH_MS) {
      subFetchMs = now;
      netArchonRequestRuns();
      render(false);              // also advances the "running 12m" clock
    }
  } else if (sub == Sub::Graph) {
    if (netArchonGraphSeq() != graphSeqSeen) {
      netArchonGetGraph(arGraphNew);
      graphSeqSeen = arGraphNew.seq;
      if (!strcmp(arGraphNew.id, arGraph.id)) {       // ignore an answer for a run we already left
        subLoading = false;
        if (arGraphNew.ok || !arGraph.ok) arGraph = arGraphNew;   // keep the last good graph on a failed refresh
        if (arGraph.ok && !focusPlaced) {             // start where the action is
          focusPlaced = true;
          nodeFocus = 0;
          for (int pass = 0; pass < 2 && !nodeFocus; pass++)
            for (int i = 0; i < arGraph.nNodes; i++)
              if (arGraph.nodes[i].state == (pass ? NodeState::Failed : NodeState::Running)) { nodeFocus = i; break; }
        }
        if (nodeFocus >= arGraph.nNodes) nodeFocus = arGraph.nNodes ? arGraph.nNodes - 1 : 0;
        render(false);
      }
    } else if (now - subFetchMs >= GRAPH_REFRESH_MS) {
      subFetchMs = now;
      netArchonRequestGraph(arGraph.id);
    }
    uiGraphTick();
  }
  if (now - lastInputMs > SUB_IDLE_MS) exitSub();     // left open: go back to being a usage dial
}

// Knob and touch while a drill-down view is open. Returns false if the event is not for it.
static bool subInput(InputEvent ev) {
  if (sub == Sub::None) return false;
  int dir = ev == InputEvent::CW ? 1 : ev == InputEvent::CCW ? -1 : 0;
  if (sub == Sub::Runs) {
    if (dir && arRuns.ok && arRuns.n) {
      runSel = constrain(runSel + dir, 0, arRuns.n - 1);
      render(false);
    } else if (ev == InputEvent::Tap) enterGraph();
    else if (ev == InputEvent::Click || ev == InputEvent::DoubleClick) exitSub();
  } else {
    if (dir && arGraph.ok && arGraph.nNodes) {
      nodeFocus = constrain(nodeFocus + dir, 0, arGraph.nNodes - 1);
      render(false);
    } else if (ev == InputEvent::Tap) {               // refresh now
      subFetchMs = millis();
      netArchonRequestGraph(arGraph.id);
    } else if (ev == InputEvent::Click) enterRuns();  // back one level
    else if (ev == InputEvent::DoubleClick) exitSub();
  }
  return true;
}

static void noteInput() {
  lastInputMs = millis();
  if (dimmed) { boardBacklight(80); dimmed = false; }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[pucky] boot");
  boardInit();
  uiInit();
  inputInit();
  uiShowMessage("Connecting", "joining WiFi…");
  netStart();
  lastInputMs = lastCycleMs = millis();
}

void loop() {
  lv_timer_handler();
  uint32_t now = millis();

  // --- input
  InputEvent ev = inputPoll();
  if (ev != InputEvent::None && sub != Sub::None) {
    noteInput();
    subInput(ev);
    ev = InputEvent::None;
  }
  switch (ev) {
    case InputEvent::Tap:
      noteInput();
      if (onArchonPage()) enterRuns();
      break;
    case InputEvent::CW:  noteInput(); gotoPage(+1); break;
    case InputEvent::CCW: noteInput(); gotoPage(-1); break;
    case InputEvent::Click:
      noteInput();
      pauseUntilMs = now + PRESS_PAUSE_MS;
      netRequestRefresh();
      break;
    case InputEvent::DoubleClick:
      noteInput();
      overview = !overview;
      render(false);
      break;
    case InputEvent::LongPress: noteInput(); break;
    default: break;
  }

  // --- periodic: model refresh, clock, LED, auto-cycle
  if (now - lastTickMs >= UI_TICK_MS) {
    lastTickMs = now;
    Model fresh;
    netGetModel(fresh);
    bool becameValid = fresh.valid && !model.valid;
    bool changed = fresh.valid && fresh.lastOkMs != model.lastOkMs;
    model = fresh;

    if (!netWifiConnected()) {
      if (!messageShown) { uiShowMessage("No WiFi", "reconnecting…"); messageShown = true; }
    } else if (!model.valid) {
      if (!messageShown || becameValid) { uiShowMessage("Connecting to host", "waiting for first poll…"); messageShown = true; }
    } else if (!model.hostReachable && now - model.lastOkMs > HOST_STALE_MS && !messageShown) {
      uiShowMessage("Host unreachable", "showing last known data"); messageShown = true;
    } else if (messageShown && model.valid && (model.hostReachable || now - model.lastOkMs <= HOST_STALE_MS)) {
      uiHideMessage(); messageShown = false;
      render(true);
    }

    if (sub != Sub::None) subTick(now);
    else if (model.valid && (becameValid || changed)) render(false);
    else uiTick(model, page, overview);

    if (model.valid && sub == Sub::None && !overview && now > pauseUntilMs && now - lastInputMs > IDLE_RESUME_MS &&
        now - lastCycleMs >= AUTOCYCLE_MS) {
      lastCycleMs = now;
      gotoPage(+1);
    }
    if (!dimmed && isNight() && now - lastInputMs > DIM_AFTER_MS) { boardBacklight(20); dimmed = true; }
  }

  updateLed();
  delay(5);
}
