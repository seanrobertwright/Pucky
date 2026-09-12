#include "ui.h"
#include <lvgl.h>
#include <esp_heap_caps.h>
#include "board.h"
#include "board_pins.h"
#include "net.h"
#include "icons/icons.h"

// ------------------------------------------------------------------ palette
static const uint32_t C_BG = 0x000000, C_RING_BG = 0x1E2228, C_TEXT = 0xF2F2F2, C_DIM = 0x8A8F98,
                      C_WARN = 0xFFB300, C_CRIT = 0xE53935, C_STALE = 0x5A5F66;

static uint32_t statusColor(Status s, uint32_t accent) {
  switch (s) {
    case Status::Ok:    return accent;
    case Status::Warn:  return C_WARN;
    case Status::Crit:  return C_CRIT;
    case Status::Error: return C_CRIT;
    default:            return C_STALE;
  }
}

// ------------------------------------------------------------------ display driver
static const uint16_t BUF_LINES = 40;
static lv_display_t *disp;

static void flushCb(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px,
                          area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  lv_display_flush_ready(d);
}

// ------------------------------------------------------------------ text helpers
static void fmtClock(char *out, size_t n, time_t local) {
  struct tm t; gmtime_r(&local, &t);
  int h = t.tm_hour % 12; if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
}

static void fmtReset(char *out, size_t n, time_t resetsAt, const Model &m) {
  if (!resetsAt) { snprintf(out, n, "reset time unknown"); return; }
  long secs = (long)(resetsAt - time(nullptr));
  if (secs <= 0) { snprintf(out, n, "resetting now"); return; }
  if (secs < 86400) {
    long h = secs / 3600, mi = (secs % 3600) / 60;
    if (h) snprintf(out, n, "resets in %ldh %02ldm", h, mi);
    else   snprintf(out, n, "resets in %ldm", mi);
    return;
  }
  static const char *DOW[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  time_t local = resetsAt + (time_t)m.tzOffsetMin * 60;
  struct tm t; gmtime_r(&local, &t);
  char clk[16]; fmtClock(clk, sizeof clk, local);
  snprintf(out, n, "resets %s %s", DOW[t.tm_wday], clk);
}

static void fmtAgo(char *out, size_t n, time_t updatedAt) {
  if (!updatedAt) { snprintf(out, n, "no data"); return; }
  long s = (long)(time(nullptr) - updatedAt);
  if (s < 0) s = 0;
  if (s < 60) snprintf(out, n, "updated %lds ago", s);
  else if (s < 3600) snprintf(out, n, "updated %ldm ago", s / 60);
  else snprintf(out, n, "updated %ldh ago", s / 3600);
}

static void fmtMoney(char *out, size_t n, float v) {
  if (v >= 1000) snprintf(out, n, "$%.0f", v);
  else if (v >= 100) snprintf(out, n, "$%.1f", v);
  else snprintf(out, n, "$%.2f", v);
}

static void fmtCount(char *out, size_t n, int64_t v) {   // 1077 -> "1.1k", 196845097 -> "197M"
  double d = (double)v;
  if (d >= 1e9)      snprintf(out, n, "%.1fB", d / 1e9);
  else if (d >= 1e8) snprintf(out, n, "%.0fM", d / 1e6);
  else if (d >= 1e6) snprintf(out, n, "%.1fM", d / 1e6);
  else if (d >= 1e5) snprintf(out, n, "%.0fk", d / 1e3);
  else if (d >= 1e3) snprintf(out, n, "%.1fk", d / 1e3);
  else               snprintf(out, n, "%lld", (long long)v);
}

// "1.1k req · 197M tok / 30d" (either part optional); empty string when nothing is known
static void fmtUsage30d(char *out, size_t n, const Provider &p) {
  char a[16], b[16];
  if (p.requests30d >= 0 && p.tokens30d >= 0) {
    fmtCount(a, sizeof a, p.requests30d); fmtCount(b, sizeof b, p.tokens30d);
    snprintf(out, n, "%s req · %s tok / 30d", a, b);
  } else if (p.requests30d >= 0) {
    fmtCount(a, sizeof a, p.requests30d); snprintf(out, n, "%s req / 30d", a);
  } else if (p.tokens30d >= 0) {
    fmtCount(b, sizeof b, p.tokens30d); snprintf(out, n, "%s tok / 30d", b);
  } else out[0] = 0;
}

// ------------------------------------------------------------------ widgets
static lv_obj_t *mkLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_align_t align, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(l, "");
  lv_obj_align(l, align, x, y);
  return l;
}

static lv_obj_t *mkArc(lv_obj_t *parent, int size, int width) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_obj_center(a);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 1000);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(C_RING_BG), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  return a;
}

static void animArc(lv_obj_t *arc, int32_t to, bool animate) {
  int32_t from = lv_arc_get_value(arc);
  if (!animate || from == to) { lv_arc_set_value(arc, to); return; }
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arc);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, 450);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) { lv_arc_set_value((lv_obj_t *)obj, v); });
  lv_anim_start(&a);
}

static void setArcColor(lv_obj_t *arc, uint32_t c) {
  lv_obj_set_style_arc_color(arc, lv_color_hex(c), LV_PART_INDICATOR);
}

// ------------------------------------------------------------------ provider screen
static lv_obj_t *scrProv, *arcOuter, *arcInner, *imgIcon, *lblName, *lblPlan, *lblBig, *lblSub, *lblSub2,
                *lblClock, *lblWifi, *lblAgo, *dots[MAX_PROVIDERS];

static void buildProvider() {
  scrProv = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrProv, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scrProv, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scrProv, LV_OBJ_FLAG_SCROLLABLE);

  arcOuter = mkArc(scrProv, 470, 16);
  arcInner = mkArc(scrProv, 428, 12);

  lblClock = mkLabel(scrProv, &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_MID, 0, 40);
  lblWifi  = mkLabel(scrProv, &lv_font_montserrat_16, C_DIM, LV_ALIGN_TOP_MID, 58, 43);

  imgIcon = lv_image_create(scrProv);
  lv_obj_align(imgIcon, LV_ALIGN_TOP_MID, 0, 78);

  lblName = mkLabel(scrProv, &lv_font_montserrat_24, C_TEXT, LV_ALIGN_TOP_MID, 0, 182);
  lblPlan = mkLabel(scrProv, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 212);
  lblBig  = mkLabel(scrProv, &lv_font_montserrat_48, C_TEXT, LV_ALIGN_TOP_MID, 0, 238);
  lblSub  = mkLabel(scrProv, &lv_font_montserrat_16, C_DIM, LV_ALIGN_TOP_MID, 0, 300);
  lblSub2 = mkLabel(scrProv, &lv_font_montserrat_14, C_DIM, LV_ALIGN_TOP_MID, 0, 326);
  lv_obj_set_width(lblSub2, 300);
  lv_label_set_long_mode(lblSub2, LV_LABEL_LONG_WRAP);
  lblAgo  = mkLabel(scrProv, &lv_font_montserrat_14, C_DIM, LV_ALIGN_BOTTOM_MID, 0, -62);

  // page dots
  for (int i = 0; i < MAX_PROVIDERS; i++) {
    dots[i] = lv_obj_create(scrProv);
    lv_obj_set_size(dots[i], 8, 8);
    lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dots[i], 0, 0);
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(C_RING_BG), 0);
    lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void layoutDots(uint8_t n, uint8_t cur, uint32_t accent) {
  const int gap = 16;
  int x0 = -((int)n - 1) * gap / 2;
  for (int i = 0; i < MAX_PROVIDERS; i++) {
    if (i >= n) { lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(dots[i], LV_ALIGN_BOTTOM_MID, x0 + i * gap, -40);
    lv_obj_set_style_bg_color(dots[i], lv_color_hex(i == cur ? accent : C_RING_BG), 0);
  }
}

void uiShowProvider(const Model &m, uint8_t idx, bool animate) {
  if (lv_screen_active() != scrProv) lv_screen_load_anim(scrProv, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
  if (idx >= m.nProviders) return;
  const Provider &p = m.providers[idx];
  const ProviderStyle *st = providerStyle(p.id);
  uint32_t accent = st ? st->accent : 0xFFFFFF;
  uint32_t col = statusColor(p.status, accent);
  char buf[64], buf2[64];

  if (st) lv_image_set_src(imgIcon, st->icon96);
  lv_obj_set_style_image_opa(imgIcon, p.status == Status::Stale ? LV_OPA_50 : LV_OPA_COVER, 0);
  lv_label_set_text(lblName, p.name);
  // plan line: "Lite · 1.1k req · 197M tok / 30d" for windows providers (money providers put usage below)
  fmtUsage30d(buf, sizeof buf, p);
  if (p.kind == Kind::Windows && buf[0]) {
    if (p.plan[0]) snprintf(buf2, sizeof buf2, "%s · %s", p.plan, buf); else snprintf(buf2, sizeof buf2, "%s", buf);
    lv_label_set_text(lblPlan, buf2);
  } else {
    lv_label_set_text(lblPlan, p.plan);
  }
  setArcColor(arcOuter, col);
  setArcColor(arcInner, col);
  lv_obj_set_style_text_color(lblBig, lv_color_hex(p.status == Status::Stale ? C_DIM : C_TEXT), 0);

  if (p.kind == Kind::Windows && p.nWindows > 0) {
    const Window &w0 = p.windows[0];
    animArc(arcOuter, (int32_t)(w0.usedPct * 10), animate);
    snprintf(buf, sizeof buf, "%.0f%%", w0.usedPct);
    lv_label_set_text(lblBig, buf);
    fmtReset(buf, sizeof buf, w0.resetsAt, m);
    snprintf(buf2, sizeof buf2, "%s · %s", w0.label, buf);
    lv_label_set_text(lblSub, buf2);
    if (p.nWindows > 1) {
      const Window &w1 = p.windows[1];
      lv_obj_remove_flag(arcInner, LV_OBJ_FLAG_HIDDEN);
      animArc(arcInner, (int32_t)(w1.usedPct * 10), animate);
      fmtReset(buf, sizeof buf, w1.resetsAt, m);
      snprintf(buf2, sizeof buf2, "%s %.0f%% · %s", w1.label, w1.usedPct, buf);
      lv_label_set_text(lblSub2, buf2);
    } else {
      lv_obj_add_flag(arcInner, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(lblSub2, "");
    }
  } else if (p.kind == Kind::Money) {
    const Money &mo = p.money;
    lv_obj_add_flag(arcInner, LV_OBJ_FLAG_HIDDEN);
    int32_t frac = mo.total > 0 ? (int32_t)(mo.remaining / mo.total * 1000) : 1000;
    animArc(arcOuter, constrain(frac, 0, 1000), animate);
    fmtMoney(buf, sizeof buf, mo.remaining);
    lv_label_set_text(lblBig, buf);
    if (mo.total > 0) { fmtMoney(buf, sizeof buf, mo.total); snprintf(buf2, sizeof buf2, "left of %s", buf); }
    else snprintf(buf2, sizeof buf2, "remaining");
    lv_label_set_text(lblSub, buf2);
    fmtMoney(buf, sizeof buf, mo.spend30d);
    char usage[48]; fmtUsage30d(usage, sizeof usage, p);
    if (usage[0]) snprintf(buf2, sizeof buf2, "%s spent · %s", buf, usage);
    else snprintf(buf2, sizeof buf2, "%s spent / 30d", buf);
    lv_label_set_text(lblSub2, buf2);
  } else {
    lv_obj_add_flag(arcInner, LV_OBJ_FLAG_HIDDEN);
    animArc(arcOuter, 0, false);
    lv_label_set_text(lblBig, "—");
    lv_label_set_text(lblSub, p.status == Status::Stale ? "no data" : "error");
    lv_label_set_text(lblSub2, p.message);
  }
  if (p.status == Status::Stale || p.status == Status::Error) {
    if (p.message[0]) lv_label_set_text(lblSub2, p.message);
  }
  layoutDots(m.nProviders, idx, accent);
  uiTick(m, idx, false);
}

// ------------------------------------------------------------------ overview screen
static lv_obj_t *scrOver, *ovArc[MAX_PROVIDERS], *ovIcon[MAX_PROVIDERS], *ovLblSpend, *ovLblTitle, *ovClock;

static void buildOverview() {
  scrOver = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scrOver, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scrOver, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scrOver, LV_OBJ_FLAG_SCROLLABLE);
  for (int i = 0; i < MAX_PROVIDERS; i++) {
    ovArc[i] = mkArc(scrOver, 118, 9);
    ovIcon[i] = lv_image_create(scrOver);
    lv_image_set_scale(ovIcon[i], 200);    // 64 px asset shown at 50 px
    lv_obj_add_flag(ovArc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ovIcon[i], LV_OBJ_FLAG_HIDDEN);
  }
  ovClock = mkLabel(scrOver, &lv_font_montserrat_16, C_DIM, LV_ALIGN_TOP_MID, 0, 24);
  ovLblTitle = mkLabel(scrOver, &lv_font_montserrat_14, C_DIM, LV_ALIGN_CENTER, 0, -22);
  lv_label_set_text(ovLblTitle, "AI spend / 30d");
  ovLblSpend = mkLabel(scrOver, &lv_font_montserrat_32, C_TEXT, LV_ALIGN_CENTER, 0, 8);
}

void uiShowOverview(const Model &m, uint8_t currentIdx) {
  if (lv_screen_active() != scrOver) lv_screen_load_anim(scrOver, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
  const float R = 158.0f;
  for (int i = 0; i < MAX_PROVIDERS; i++) {
    if (i >= m.nProviders) {
      lv_obj_add_flag(ovArc[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ovIcon[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const Provider &p = m.providers[i];
    const ProviderStyle *st = providerStyle(p.id);
    float ang = -90.0f + 360.0f * i / m.nProviders;
    int cx = (int)(R * cosf(ang * DEG_TO_RAD)), cy = (int)(R * sinf(ang * DEG_TO_RAD));
    lv_obj_remove_flag(ovArc[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ovIcon[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(ovArc[i], LV_ALIGN_CENTER, cx, cy);
    lv_obj_align(ovIcon[i], LV_ALIGN_CENTER, cx, cy);
    if (st) lv_image_set_src(ovIcon[i], st->icon64);
    uint32_t col = statusColor(p.status, st ? st->accent : 0xFFFFFF);
    setArcColor(ovArc[i], col);
    lv_obj_set_style_arc_color(ovArc[i], lv_color_hex(i == currentIdx ? 0x3A4048 : C_RING_BG), LV_PART_MAIN);
    lv_obj_set_style_image_opa(ovIcon[i], p.status == Status::Stale ? LV_OPA_50 : LV_OPA_COVER, 0);
    int32_t v = 0;
    if (p.kind == Kind::Windows && p.nWindows) v = (int32_t)(p.windows[0].usedPct * 10);
    else if (p.kind == Kind::Money) v = p.money.total > 0 ? (int32_t)(p.money.remaining / p.money.total * 1000) : 1000;
    lv_arc_set_value(ovArc[i], constrain(v, 0, 1000));
  }
  char buf[24];
  fmtMoney(buf, sizeof buf, m.spend30d);
  lv_label_set_text(ovLblSpend, buf);
  uiTick(m, currentIdx, true);
}

// ------------------------------------------------------------------ message overlay
static lv_obj_t *msgBox, *msgL1, *msgL2, *msgSpin;

static void buildMessage() {
  msgBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(msgBox, LCD_W, LCD_H);
  lv_obj_center(msgBox);
  lv_obj_set_style_bg_color(msgBox, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(msgBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(msgBox, 0, 0);
  lv_obj_set_style_radius(msgBox, 0, 0);
  lv_obj_remove_flag(msgBox, LV_OBJ_FLAG_SCROLLABLE);
  msgSpin = lv_spinner_create(msgBox);
  lv_obj_set_size(msgSpin, 90, 90);
  lv_obj_align(msgSpin, LV_ALIGN_CENTER, 0, -60);
  lv_obj_set_style_arc_color(msgSpin, lv_color_hex(C_RING_BG), LV_PART_MAIN);
  lv_obj_set_style_arc_color(msgSpin, lv_color_hex(0xD97757), LV_PART_INDICATOR);
  msgL1 = mkLabel(msgBox, &lv_font_montserrat_24, C_TEXT, LV_ALIGN_CENTER, 0, 30);
  msgL2 = mkLabel(msgBox, &lv_font_montserrat_14, C_DIM, LV_ALIGN_CENTER, 0, 66);
  lv_obj_set_width(msgL2, 320);
  lv_label_set_long_mode(msgL2, LV_LABEL_LONG_WRAP);
  lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
}

void uiShowMessage(const char *l1, const char *l2) {
  lv_label_set_text(msgL1, l1);
  lv_label_set_text(msgL2, l2);
  lv_obj_remove_flag(msgBox, LV_OBJ_FLAG_HIDDEN);
}

void uiHideMessage() { lv_obj_add_flag(msgBox, LV_OBJ_FLAG_HIDDEN); }

// ------------------------------------------------------------------ periodic
void uiTick(const Model &m, uint8_t idx, bool overview) {
  char buf[48];
  if (m.valid) fmtClock(buf, sizeof buf, nowLocal(m)); else buf[0] = 0;
  lv_label_set_text(overview ? ovClock : lblClock, buf);
  if (overview) return;
  const char *wifi = netWifiConnected() ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
  uint32_t wcol = !netWifiConnected() ? C_CRIT : m.hostReachable ? C_DIM : C_WARN;
  lv_label_set_text(lblWifi, wifi);
  lv_obj_set_style_text_color(lblWifi, lv_color_hex(wcol), 0);
  if (idx < m.nProviders) {
    const Provider &p = m.providers[idx];
    fmtAgo(buf, sizeof buf, p.updatedAt);
    if (p.status == Status::Stale) {
      char b2[56]; snprintf(b2, sizeof b2, LV_SYMBOL_WARNING " %s", buf);
      lv_label_set_text(lblAgo, b2);
      lv_obj_set_style_text_color(lblAgo, lv_color_hex(C_WARN), 0);
    } else {
      lv_label_set_text(lblAgo, buf);
      lv_obj_set_style_text_color(lblAgo, lv_color_hex(C_DIM), 0);
    }
  }
}

// ------------------------------------------------------------------ init
void uiInit() {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  size_t bufBytes = (size_t)LCD_W * BUF_LINES * 2;
  void *b1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void *b2 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1 || !b2) { b1 = ps_malloc(bufBytes); b2 = ps_malloc(bufBytes); }
  lv_display_set_buffers(disp, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  buildProvider();
  buildOverview();
  buildMessage();
  lv_screen_load(scrProv);
}
