// Archon drill-down screens: the run list (dial picks a run) and the workflow graph (dial walks the
// nodes, the view pans to follow). The graph is painted in one draw callback rather than built from
// widgets: 40 nodes + 96 edges would be ~140 objects to create and tear down on every refresh.
#include "ui.h"
#include <lvgl.h>
#include "board_pins.h"

static const uint32_t C_BG = 0x000000, C_TEXT = 0xF2F2F2, C_DIM = 0x8A8F98, C_PANEL = 0x12141A,
                      C_EDGE = 0x3A4048, C_EDGE_LIT = 0x8A8F98;

static uint32_t stateColor(NodeState s) {
  switch (s) {
    case NodeState::Running:   return 0x42A5F5;
    case NodeState::Completed: return 0x3DDC84;
    case NodeState::Failed:    return 0xE53935;
    case NodeState::Cancelled: return 0xFFB300;
    case NodeState::Skipped:   return 0x5A5F66;
    default:                   return 0x8A8F98;
  }
}

static const char *stateName(NodeState s) {
  switch (s) {
    case NodeState::Running:   return "running";
    case NodeState::Completed: return "completed";
    case NodeState::Failed:    return "failed";
    case NodeState::Cancelled: return "cancelled";
    case NodeState::Skipped:   return "skipped";
    default:                   return "pending";
  }
}

static uint32_t runStatusColor(const char *s) {
  if (!strcmp(s, "running")) return 0x42A5F5;
  if (!strcmp(s, "completed")) return 0x3DDC84;
  if (!strcmp(s, "failed")) return 0xE53935;
  if (!strcmp(s, "cancelled") || !strcmp(s, "paused")) return 0xFFB300;
  return C_DIM;
}

static void fmtDuration(char *out, size_t n, long secs) {
  if (secs < 0) secs = 0;
  if (secs < 60) snprintf(out, n, "%lds", secs);
  else if (secs < 3600) snprintf(out, n, "%ldm", secs / 60);
  else if (secs < 86400) snprintf(out, n, "%ldh %02ldm", secs / 3600, (secs % 3600) / 60);
  else snprintf(out, n, "%ldd %ldh", secs / 86400, (secs % 86400) / 3600);
}

// "running 12m" / "completed, took 37m" / "failed 3h 05m ago"
static void fmtRunStatus(char *out, size_t n, const char *status, time_t startedAt, time_t endedAt) {
  char d[16];
  time_t now = time(nullptr);
  if (!startedAt) { snprintf(out, n, "%s", status); return; }
  if (!endedAt) { fmtDuration(d, sizeof d, (long)(now - startedAt)); snprintf(out, n, "%s %s", status, d); return; }
  fmtDuration(d, sizeof d, (long)(endedAt - startedAt));
  snprintf(out, n, "%s, took %s", status, d);
}

static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_align_t align, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "");
  lv_obj_align(l, align, x, y);
  return l;
}

static lv_obj_t *mkScreen() {
  lv_obj_t *s = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// ------------------------------------------------------------------ run list
// A three-slot carousel: the selected run in a panel at the centre, its neighbours dimmed above and below.
static lv_obj_t *scrRuns, *rlTitle, *rlCount, *rlPrev, *rlNext, *rlPanel, *rlName, *rlStatus, *rlDetail, *rlPos, *rlHint;

static void buildRunList() {
  scrRuns = mkScreen();
  rlTitle = mkLabel(scrRuns, &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_MID, 0, 40);
  lv_label_set_text(rlTitle, "Archon runs");
  rlCount = mkLabel(scrRuns, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 68);
  rlPrev = mkLabel(scrRuns, &lv_font_montserrat_16, C_DIM, LV_ALIGN_CENTER, 0, -104);

  rlPanel = lv_obj_create(scrRuns);
  lv_obj_set_size(rlPanel, 400, 128);
  lv_obj_align(rlPanel, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(rlPanel, 20, 0);
  lv_obj_set_style_bg_color(rlPanel, lv_color_hex(C_PANEL), 0);
  lv_obj_set_style_bg_opa(rlPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(rlPanel, 2, 0);
  lv_obj_set_style_pad_all(rlPanel, 0, 0);
  lv_obj_remove_flag(rlPanel, LV_OBJ_FLAG_SCROLLABLE);
  rlName   = mkLabel(rlPanel, &lv_font_montserrat_24, C_TEXT, LV_ALIGN_TOP_MID, 0, 14);
  rlStatus = mkLabel(rlPanel, &lv_font_montserrat_16, C_DIM, LV_ALIGN_TOP_MID, 0, 52);
  rlDetail = mkLabel(rlPanel, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 84);
  for (lv_obj_t *l : {rlName, rlStatus, rlDetail}) {     // long names: one line, cut with dots
    lv_obj_set_width(l, 372);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  }
  lv_obj_set_height(rlName, 30);
  lv_obj_set_height(rlStatus, 22);
  lv_obj_set_height(rlDetail, 20);

  rlNext = mkLabel(scrRuns, &lv_font_montserrat_16, C_DIM, LV_ALIGN_CENTER, 0, 104);
  rlPos  = mkLabel(scrRuns, &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -70);
  rlHint = mkLabel(scrRuns, &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -44);
}

void uiShowRunList(const ArRuns &r, int sel, bool loading) {
  if (lv_screen_active() != scrRuns) lv_screen_load_anim(scrRuns, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
  char buf[96], d[24];
  lv_label_set_text(rlPrev, "");
  lv_label_set_text(rlNext, "");
  lv_label_set_text(rlPos, "");
  lv_label_set_text(rlDetail, "");
  lv_obj_set_style_text_color(rlStatus, lv_color_hex(C_DIM), 0);
  lv_obj_set_style_border_color(rlPanel, lv_color_hex(C_EDGE), 0);

  if (r.seq == 0 || (!r.ok && loading)) {               // nothing to show yet
    lv_label_set_text(rlCount, "");
    lv_label_set_text(rlName, "Loading");
    lv_label_set_text(rlStatus, "asking the host for runs");
    lv_label_set_text(rlHint, "press: back");
    return;
  }
  if (!r.ok) {
    lv_label_set_text(rlCount, "");
    lv_label_set_text(rlName, "Unavailable");
    lv_label_set_text(rlStatus, r.error[0] ? r.error : "no answer from the host");
    lv_label_set_text(rlHint, "press: back");
    return;
  }
  snprintf(buf, sizeof buf, "%u active", r.nActive);
  lv_label_set_text(rlCount, buf);
  if (r.n == 0) {
    lv_label_set_text(rlName, "Nothing running");
    lv_label_set_text(rlStatus, "no active workflow runs");
    lv_label_set_text(rlHint, "press: back");
    return;
  }
  if (sel < 0) sel = 0;
  if (sel >= r.n) sel = r.n - 1;
  const ArRun &run = r.runs[sel];
  uint32_t col = runStatusColor(run.status);
  lv_label_set_text(rlName, run.name);
  fmtRunStatus(buf, sizeof buf, run.status, run.startedAt, run.endedAt);
  lv_label_set_text(rlStatus, buf);
  lv_obj_set_style_text_color(rlStatus, lv_color_hex(col), 0);
  lv_obj_set_style_border_color(rlPanel, lv_color_hex(col), 0);
  if (run.node[0] && run.project[0]) snprintf(buf, sizeof buf, "at %s  |  %s", run.node, run.project);
  else if (run.node[0]) snprintf(buf, sizeof buf, "at %s", run.node);
  else snprintf(buf, sizeof buf, "%s", run.project);
  lv_label_set_text(rlDetail, buf);
  if (run.endedAt) {                                    // finished run: say how long ago instead
    fmtDuration(d, sizeof d, (long)(time(nullptr) - run.endedAt));
    snprintf(buf, sizeof buf, "%s  |  ended %s ago", run.project, d);
    lv_label_set_text(rlDetail, buf);
  }
  if (sel > 0) lv_label_set_text(rlPrev, r.runs[sel - 1].name);
  if (sel + 1 < r.n) lv_label_set_text(rlNext, r.runs[sel + 1].name);
  snprintf(buf, sizeof buf, "%d / %u", sel + 1, r.n);
  lv_label_set_text(rlPos, buf);
  lv_label_set_text(rlHint, "tap: open graph    press: back");
}

// ------------------------------------------------------------------ workflow graph
// Geometry (px). Rows are ROW apart; the focused node's row sits at FOCUS_Y and everything else pans.
// The top and bottom bands are painted over the graph and carry the run / focused-node captions.
static const int ROW = 62, NODE_H = 36, LANE = 14, FOCUS_Y = 236, BAND_TOP = 84, BAND_BOT = 388;

static lv_obj_t *scrGraph, *gCanvas;
static ArGraph g;                 // what is on screen
static int gFocus = 0;
static bool gLoading = false, gBlink = false;
static int32_t gPan = 0;          // y of row 0 relative to FOCUS_Y, animated

struct GraphGeom { int col, nodeW, cx, half; };

static GraphGeom geom() {
  int ml = g.lanesL * LANE + (g.lanesL ? 10 : 0), mr = g.lanesR * LANE + (g.lanesR ? 10 : 0);
  int w = g.width ? g.width : 1;
  GraphGeom q;
  q.col = min(170, (420 - ml - mr) / w);
  q.nodeW = min(156, q.col - 14);
  q.cx = LCD_W / 2 + (ml - mr) / 2;
  q.half = ((w - 1) * q.col + q.nodeW) / 2;
  return q;
}

static void drawLine(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(color);
  d.width = 2;
  d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
  lv_draw_line(layer, &d);
}

static void drawText(lv_layer_t *layer, const char *text, const lv_font_t *font, uint32_t color, int cx, int y, int w) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.text = text;
  d.text_local = 1;               // the draw is deferred; `text` is usually a stack buffer
  d.font = font;
  d.color = lv_color_hex(color);
  d.align = LV_TEXT_ALIGN_CENTER;
  lv_area_t a = {cx - w / 2, y, cx + w / 2, y + lv_font_get_line_height(font)};
  lv_draw_label(layer, &d, &a);
}

static void graphDraw(lv_event_t *e) {
  lv_layer_t *layer = lv_event_get_layer(e);
  char buf[72];

  if (g.ok && g.nNodes) {
    const GraphGeom q = geom();
    auto nx = [&](int i) { return q.cx + (int)lroundf(g.nodes[i].x * q.col); };
    auto ny = [&](int i) { return FOCUS_Y + (int)g.nodes[i].layer * ROW - (int)gPan; };
    const int yMin = BAND_TOP - ROW, yMax = BAND_BOT + ROW;

    for (int k = 0; k < g.nEdges; k++) {
      const ArEdge &ed = g.edges[k];
      if (ed.from >= g.nNodes || ed.to >= g.nNodes) continue;
      int ax = nx(ed.from), ay = ny(ed.from), bx = nx(ed.to), by = ny(ed.to);
      if (by < yMin || ay > yMax) continue;
      NodeState sa = g.nodes[ed.from].state, sb = g.nodes[ed.to].state;
      bool lit = sa == NodeState::Completed && sb != NodeState::Pending && sb != NodeState::Skipped;
      uint32_t c = lit ? C_EDGE_LIT : C_EDGE;
      if (ed.lane == 0) {
        drawLine(layer, ax, ay + NODE_H / 2, bx, by - NODE_H / 2, c);
      } else {                      // out sideways, down the lane, back in
        int s = ed.lane > 0 ? 1 : -1;
        int lx = q.cx + s * (q.half + 12 + LANE * (abs(ed.lane) - 1));
        drawLine(layer, ax + s * q.nodeW / 2, ay, lx, ay, c);
        drawLine(layer, lx, ay, lx, by, c);
        drawLine(layer, lx, by, bx + s * q.nodeW / 2, by, c);
      }
    }

    const int maxChars = max(4, (int)((q.nodeW - 12) / 8.5f));
    for (int i = 0; i < g.nNodes; i++) {
      int x = nx(i), y = ny(i);
      if (y < yMin || y > yMax) continue;
      const ArNode &n = g.nodes[i];
      lv_color_t c = lv_color_hex(stateColor(n.state));
      bool quiet = n.state == NodeState::Pending || n.state == NodeState::Skipped;
      lv_draw_rect_dsc_t r;
      lv_draw_rect_dsc_init(&r);
      r.radius = 10;
      r.bg_opa = LV_OPA_COVER;      // opaque, so lanes and edges pass behind the nodes
      r.bg_color = quiet ? lv_color_hex(C_PANEL)
                         : lv_color_mix(c, lv_color_hex(C_BG), n.state == NodeState::Running ? (gBlink ? 150 : 90) : 60);
      r.border_color = i == gFocus ? lv_color_hex(0xFFFFFF) : c;
      r.border_width = i == gFocus ? 3 : 2;
      lv_area_t a = {x - q.nodeW / 2, y - NODE_H / 2, x + q.nodeW / 2, y + NODE_H / 2};
      lv_draw_rect(layer, &r, &a);
      size_t len = strlen(n.label);
      if ((int)len > maxChars) snprintf(buf, sizeof buf, "%.*s..", maxChars - 2, n.label);
      else snprintf(buf, sizeof buf, "%s", n.label);
      drawText(layer, buf, &lv_font_montserrat_14, n.state == NodeState::Skipped ? C_DIM : C_TEXT, x, y - 9, q.nodeW);
    }
  }

  // bands over the graph
  lv_draw_rect_dsc_t band;
  lv_draw_rect_dsc_init(&band);
  band.bg_color = lv_color_hex(C_BG);
  band.bg_opa = LV_OPA_COVER;
  lv_area_t top = {0, 0, LCD_W - 1, BAND_TOP}, bot = {0, BAND_BOT, LCD_W - 1, LCD_H - 1};
  lv_draw_rect(layer, &band, &top);
  lv_draw_rect(layer, &band, &bot);

  drawText(layer, g.name[0] ? g.name : "Workflow", &lv_font_montserrat_20, C_TEXT, LCD_W / 2, 32, 300);
  if (g.ok && g.nNodes) {
    snprintf(buf, sizeof buf, "%s  |  %u/%u nodes", g.status, g.done, g.total);
    drawText(layer, buf, &lv_font_montserrat_14, runStatusColor(g.status), LCD_W / 2, 60, 340);
    const ArNode &n = g.nodes[gFocus < g.nNodes ? gFocus : 0];
    drawText(layer, n.label, &lv_font_montserrat_20, C_TEXT, LCD_W / 2, 394, 380);
    if (n.at[0]) snprintf(buf, sizeof buf, "%s  |  at %s", stateName(n.state), n.at);
    else snprintf(buf, sizeof buf, "%s  |  node %d/%u", stateName(n.state), gFocus + 1, g.nNodes);
    drawText(layer, buf, &lv_font_montserrat_14, stateColor(n.state), LCD_W / 2, 420, 340);
    drawText(layer, "press: back", &lv_font_montserrat_14, C_DIM, LCD_W / 2, 442, 200);
  } else {
    const char *l1 = gLoading ? "Loading graph" : "No graph";
    const char *l2 = gLoading ? "the host is reading the run" : (g.error[0] ? g.error : "this run has no nodes");
    drawText(layer, l1, &lv_font_montserrat_24, C_TEXT, LCD_W / 2, 206, 400);
    drawText(layer, l2, &lv_font_montserrat_14, C_DIM, LCD_W / 2, 244, 380);
    drawText(layer, "press: back", &lv_font_montserrat_14, C_DIM, LCD_W / 2, 420, 200);
  }
}

static void buildGraph() {
  scrGraph = mkScreen();
  gCanvas = lv_obj_create(scrGraph);
  lv_obj_remove_style_all(gCanvas);
  lv_obj_set_size(gCanvas, LCD_W, LCD_H);
  lv_obj_remove_flag(gCanvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(gCanvas, graphDraw, LV_EVENT_DRAW_MAIN, nullptr);
}

static void panTo(int32_t target, bool animate) {
  lv_anim_delete(gCanvas, nullptr);
  if (!animate || target == gPan) { gPan = target; lv_obj_invalidate(gCanvas); return; }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, gCanvas);
  lv_anim_set_values(&a, gPan, target);
  lv_anim_set_duration(&a, 220);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { gPan = v; lv_obj_invalidate((lv_obj_t *)obj); });
  lv_anim_start(&a);
}

void uiShowRunGraph(const ArGraph &graph, int focus, bool loading) {
  bool entering = lv_screen_active() != scrGraph;
  if (entering) lv_screen_load_anim(scrGraph, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
  bool sameRun = !entering && !strcmp(g.id, graph.id) && g.ok && graph.ok;
  g = graph;
  gLoading = loading;
  if (g.nNodes > AR_MAX_NODES) g.nNodes = AR_MAX_NODES;
  if (focus < 0) focus = 0;
  if (g.nNodes && focus >= g.nNodes) focus = g.nNodes - 1;
  gFocus = focus;
  panTo(g.ok && g.nNodes ? (int32_t)g.nodes[gFocus].layer * ROW : 0, sameRun);   // glide only within a run
}

void uiGraphTick() {              // ~2 Hz: pulse the running nodes
  if (lv_screen_active() != scrGraph || !g.ok) return;
  for (int i = 0; i < g.nNodes; i++) {
    if (g.nodes[i].state == NodeState::Running) { gBlink = !gBlink; lv_obj_invalidate(gCanvas); return; }
  }
}

void uiArchonInit() {
  buildRunList();
  buildGraph();
}
