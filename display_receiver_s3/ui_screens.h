/*
  ui_screens.h - the five pages: HOME (single RPM bar cluster), LIVE DATA, GRAPH, FAULT CODES, FUEL MAP.
  Zero-Latency Edition:
   - Direct raw RPM bar response (no exponential lag filter)
   - Instant updates on all 6 Home tiles & all 14 Live Data rows (no 200ms throttle)
   - Full Change-Detection caching so LVGL only repaints digits that actually changed
*/
#pragma once

static lv_obj_t *g_scr[N_PAGES];

static inline const lv_font_t *fVal() { return g_set.lang ? &font_thai_15 : &lv_font_montserrat_14; }

static void fmtInt(char *out, size_t cap, unsigned v) {
  if (v >= 1000) snprintf(out, cap, "%u,%03u", v / 1000, v % 1000); else snprintf(out, cap, "%u", v);
}

// ============================================================ HOME
#define BAR_X     8
#define BAR_Y     28
#define BAR_W     266
#define BAR_H     22

#define TILE_W    96            // value tiles (2 x 3, right side)
#define TILE_H    70
#define TILE_BW   84            // width of the little bar inside a tile

#define MAX_TICKS 17
static lv_obj_t *g_barFill, *g_redMark;
static lv_obj_t *g_tick[MAX_TICKS];
static int       g_tickN = 0;
static lv_obj_t *g_spdLbl, *g_peakLbl, *g_redLbl, *g_redDot, *g_shiftBadge, *g_shiftTxt;

struct Tile {
  lv_obj_t *box, *val, *badge, *fill;
  uint32_t accent;
  char lastTxt[16];
  uint32_t lastCol, lastFillCol, lastBdrCol;
  int16_t lastW;
};
static Tile g_tAfr, g_tBatt, g_tCool, g_tInj, g_tIgn, g_tO2;
static int g_tickCount = 0;

static uint16_t g_peakRpm = 0;

static int      s_bw = -1;
static uint32_t s_barCol = 0;
static int8_t   s_lastGear = -2;
static int      s_lastSpeed = -2;
static int8_t   s_badgeOn = -1;
static uint16_t s_lastLim = 0;
static char     s_lastPeakTxt[32] = "";
static char     s_lastRedTxt[32] = "";
static char     s_lastAfrBadge[16] = "";
static uint32_t s_lastAfrBadgeCol = 0xFFFFFFFF;

static void barSet(int w, uint32_t col) {
  if (w < 0) w = 0;
  if (w > BAR_W) w = BAR_W;
  if (w != s_bw) {
    s_bw = w;
    if (w <= 0) show(g_barFill, false);
    else { show(g_barFill, true); lv_obj_set_width(g_barFill, w < 4 ? 4 : w); }
  }
  if (col != s_barCol) { s_barCol = col; setBg(g_barFill, col); }
}

static Tile mkTile(lv_obj_t *s, int x, int y, const char *title, const char *unit, uint32_t accent, const char *icon, bool badge) {
  Tile t;
  t.accent = accent;
  t.badge = nullptr;
  t.lastTxt[0] = '\0';
  t.lastCol = 0xFFFFFFFF;
  t.lastFillCol = 0xFFFFFFFF;
  t.lastBdrCol = 0xFFFFFFFF;
  t.lastW = -1;

  t.box = mkBox(s, x, y, TILE_W, TILE_H, PCOL(panel), 10, PCOL(border), 1);
  mkLblClip(t.box, title, fSm(), PCOL(dim), 6, (3 + TOFF), badge ? 36 : 68);
  if (badge)      t.badge = mkLblClip(t.box, "", fSm(), accent, 42, (3 + TOFF), 48, LV_TEXT_ALIGN_RIGHT);
  else if (icon)  mkLbl(t.box, icon, &lv_font_montserrat_12, accent, 76, 4);
  else            mkBox(t.box, 82, 8, 7, 7, accent, LV_RADIUS_CIRCLE);
  t.val = mkLblClip(t.box, "--", fLatin(28), PCOL(dim), 5, 20, 68);
  mkLbl(t.box, unit, fLatin(14), PCOL(dim), 42, 32, 48, LV_TEXT_ALIGN_RIGHT);
  mkBox(t.box, 6, 58, TILE_BW, 4, PCOL(track), 2);
  t.fill = mkBox(t.box, 6, 58, 1, 4, accent, 2);
  return t;
}

static void tileSet(Tile &t, bool valid, const char *txt, uint8_t lvl, float pct) {
  const char *nt = valid ? txt : "--";
  if (strcmp(t.lastTxt, nt) != 0) {
    strncpy(t.lastTxt, nt, sizeof(t.lastTxt) - 1);
    t.lastTxt[sizeof(t.lastTxt) - 1] = '\0';
    setTxt(t.val, nt);
  }

  const uint32_t nc = valid ? lvlColor(lvl, PCOL(text)) : PCOL(dim);
  if (t.lastCol != nc) {
    t.lastCol = nc;
    setCol(t.val, nc);
  }

  int w = valid ? (int)((float)TILE_BW * clampf(pct, 0.0f, 1.0f) + 0.5f) : 0;
  if (w < 1) w = 1;
  if (t.lastW != w) {
    t.lastW = (int16_t)w;
    lv_obj_set_width(t.fill, w);
  }

  const uint32_t fc = valid ? lvlColor(lvl, t.accent) : PCOL(track);
  if (t.lastFillCol != fc) {
    t.lastFillCol = fc;
    setBg(t.fill, fc);
  }

  const uint32_t bc = (valid && lvl) ? lvlColor(lvl, PCOL(border)) : PCOL(border);
  if (t.lastBdrCol != bc) {
    t.lastBdrCol = bc;
    setBdr(t.box, bc);
  }
}

static void peakResetCb(lv_event_t *e) { (void)e; g_peakRpm = 0; toast(S(S_PEAK_RST), PCOL(cyan)); }

static void buildMain() {
  s_bw = -1; s_barCol = 0; s_lastGear = -2; s_lastSpeed = -2; s_badgeOn = -1; s_lastLim = 0;
  s_lastPeakTxt[0] = '\0'; s_lastRedTxt[0] = '\0'; s_lastAfrBadge[0] = '\0'; s_lastAfrBadgeCol = 0xFFFFFFFF;

  lv_obj_t *s = g_scr[0] = mkScreen();
  buildTopBar(s, 0);
  buildBottomBar(s);

  mkBox(s, BAR_X, BAR_Y, BAR_W, BAR_H, PCOL(track), 4);
  const int T = g_set.maxRpm / 1000 > MAX_TICKS - 1 ? MAX_TICKS - 1 : g_set.maxRpm / 1000;
  g_tickN = T + 1;
  for (int k = 0; k <= T; k++) {
    int cx = BAR_X + k * BAR_W / T;
    mkBox(s, cx - 1, BAR_Y + BAR_H + 1, 2, 3, PCOL(dim), 0);
    if (cx < BAR_X + 8) cx = BAR_X + 8;
    if (cx > BAR_X + BAR_W - 8) cx = BAR_X + BAR_W - 8;
    char b[4]; snprintf(b, sizeof(b), "%d", k);
    g_tick[k] = mkLbl(s, b, &lv_font_montserrat_12, PCOL(dim), cx - 10, BAR_Y + BAR_H + 4, 20, LV_TEXT_ALIGN_CENTER);
  }
  g_barFill = mkBox(s, BAR_X, BAR_Y, 4, BAR_H, PCOL(cyan), 2);
  show(g_barFill, false);
  g_redMark = mkBox(s, BAR_X + BAR_W - 2, BAR_Y - 3, 3, BAR_H + 6, PCOL(red), 1);

  mkLbl(s, S(S_GEAR), fSm(), PCOL(dim), 8, (90 + TOFF), 118, LV_TEXT_ALIGN_CENTER);
  g_gearLbl = mkLbl(s, "-", fLatin(48), PCOL(dim), 8, 102, 118, LV_TEXT_ALIGN_CENTER);
  g_shiftBadge = mkBox(s, 14, 164, 106, 20, PCOL(red), 10);
  g_shiftTxt = mkLbl(g_shiftBadge, "", fSm(), 0xFFFFFF, 0, 0, 104, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(g_shiftTxt);
  show(g_shiftBadge, false);
  mkLbl(s, S(S_SPEED), fSm(), PCOL(dim), 132, (90 + TOFF), 142, LV_TEXT_ALIGN_RIGHT);
  g_spdLbl = mkLbl(s, "--", fLatin(48), PCOL(text), 132, 102, 142, LV_TEXT_ALIGN_RIGHT);
  mkLbl(s, S(S_KMH), fMd(), PCOL(cyan), 132, g_set.lang ? 161 : 164, 142, LV_TEXT_ALIGN_RIGHT);

  g_peakLbl = mkLblClip(s, "", fSm(), PCOL(text), 14, (226 + TOFF), 130);
  lv_obj_add_flag(g_peakLbl, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_peakLbl, peakResetCb, LV_EVENT_LONG_PRESSED, NULL);
  g_redDot = mkBox(s, 160, g_set.lang ? 231 : 232, 7, 7, PCOL(red), LV_RADIUS_CIRCLE);
  g_redLbl = mkLblClip(s, "", fSm(), PCOL(text), 171, (226 + TOFF), 100);

  g_tAfr  = mkTile(s, 278, 26,  S(S_TILE_AFR),  ":1", PCOL(mint),   nullptr, true);
  g_tBatt = mkTile(s, 378, 26,  S(S_TILE_BATT), "V",  PCOL(cyan),   LV_SYMBOL_BATTERY_FULL, false);
  g_tCool = mkTile(s, 278, 100, S(S_TILE_COOL), "\xC2\xB0""C", PCOL(cyan), nullptr, false);
  g_tInj  = mkTile(s, 378, 100, S(S_TILE_INJ),  "%",  PCOL(orange), nullptr, false);
  g_tIgn  = mkTile(s, 278, 174, S(S_TILE_IGN),  "\xC2\xB0", PCOL(yellow), LV_SYMBOL_CHARGE, false);
  g_tO2   = mkTile(s, 378, 174, S(S_TILE_O2),   "V",  PCOL(violet), nullptr, false);
}

static void mainUpdate() {
  const Live &L = g_live;
  const bool on  = L.ecuConnected;
  const bool yam = on && L.ecuType == ECU_TYPE_YAMAHA;
  const uint8_t  gearArg = L.gear < 0 ? 255 : (uint8_t)L.gear;
  const uint16_t lim = shiftLimit(g_set, gearArg);
  const uint16_t rpm = on ? L.rpm : 0;
  char b[48];

  // ตัดตัวกรองหน่วงออกทั้งหมด: แถบวัดรอบขยับตามค่าจริงทันที 0ms
  const float frac = clampf((float)rpm / (float)g_set.maxRpm, 0.0f, 1.0f);
  const uint32_t barCol = (rpm >= lim) ? PCOL(red) : ((float)rpm >= 0.88f * (float)lim ? PCOL(amber) : PCOL(cyan));
  barSet((int)(frac * (float)BAR_W + 0.5f), barCol);
  if (rpm > g_peakRpm) g_peakRpm = rpm;

  const int currentSpeed = (on && L.hasSpeed) ? (int)(L.speed + 0.5f) : -1;
  if (currentSpeed != s_lastSpeed) {
    s_lastSpeed = currentSpeed;
    if (currentSpeed >= 0) {
      snprintf(b, sizeof(b), "%d", currentSpeed);
      setTxt(g_spdLbl, b);
      setCol(g_spdLbl, lvlColor(levelSpeed(L.speed), PCOL(text)));
    } else {
      setTxt(g_spdLbl, "--");
      setCol(g_spdLbl, PCOL(dim));
    }
  }

  const int8_t gk = on ? L.gear : -1;
  if (gk != s_lastGear) {
    if (s_lastGear != -2 && gk >= 0) gearEffect();
    s_lastGear = gk;
    snprintf(b, sizeof(b), "%d", gk);
    setTxt(g_gearLbl, gk < 0 ? "-" : (gk == 0 ? "N" : b));
    setCol(g_gearLbl, gk < 0 ? PCOL(dim) : PCOL(cyan));
  }

  const int8_t badge = (on && g_set.sport && rpm >= lim) ? 1 : 0;
  if (badge != s_badgeOn) {
    s_badgeOn = badge;
    if (badge) { snprintf(b, sizeof(b), "%s %s", S(S_SHIFT_UP), LV_SYMBOL_UP); setTxt(g_shiftTxt, b); lv_obj_center(g_shiftTxt); }
    show(g_shiftBadge, badge);
  }

  // อัปเดตช่องย่อยทั้ง 6 ช่องทันทีโดยไม่ต้องรอ 200ms (ใช้ Change Detection คุมการวาด)
  {
    char n[12]; fmtInt(n, sizeof(n), g_peakRpm);
    snprintf(b, sizeof(b), "%s: %s", S(S_PEAK_RPM), n);
    if (strcmp(s_lastPeakTxt, b) != 0) {
      strncpy(s_lastPeakTxt, b, sizeof(s_lastPeakTxt) - 1);
      s_lastPeakTxt[sizeof(s_lastPeakTxt) - 1] = '\0';
      setTxt(g_peakLbl, b);
    }
  }

  snprintf(b, sizeof(b), "%s: %.1fK", S(S_REDLINE), lim / 1000.0f);
  if (strcmp(s_lastRedTxt, b) != 0) {
    strncpy(s_lastRedTxt, b, sizeof(s_lastRedTxt) - 1);
    s_lastRedTxt[sizeof(s_lastRedTxt) - 1] = '\0';
    setTxt(g_redLbl, b);
  }

  if (lim != s_lastLim) {
    s_lastLim = lim;
    lv_obj_set_x(g_redMark, BAR_X + (int)(clampf((float)lim / (float)g_set.maxRpm, 0.0f, 1.0f) * BAR_W) - 2);
    for (int t = 0; t < g_tickN; t++) setCol(g_tick[t], (uint32_t)t * 1000 >= lim ? PCOL(red) : PCOL(dim));
  }

  const bool afrOk = on && !yam;
  snprintf(b, sizeof(b), "%.1f", L.afr);
  tileSet(g_tAfr, afrOk, b, levelAfr(L.afr), (L.afr - 10.0f) / 10.0f);
  if (afrOk) {
    const bool st = fabsf(L.afr - 14.7f) <= 0.4f;
    const char *bt = st ? S(S_B_STOICH) : (L.afr < 14.7f ? S(S_B_RICH) : S(S_B_LEAN));
    const uint32_t bc = st ? PCOL(green) : PCOL(amber);
    if (strcmp(s_lastAfrBadge, bt) != 0) {
      strncpy(s_lastAfrBadge, bt, sizeof(s_lastAfrBadge) - 1);
      s_lastAfrBadge[sizeof(s_lastAfrBadge) - 1] = '\0';
      setTxt(g_tAfr.badge, bt);
    }
    if (s_lastAfrBadgeCol != bc) {
      s_lastAfrBadgeCol = bc;
      setCol(g_tAfr.badge, bc);
    }
  } else if (s_lastAfrBadge[0] != '\0') {
    s_lastAfrBadge[0] = '\0';
    setTxt(g_tAfr.badge, "");
  }

  snprintf(b, sizeof(b), "%.1f", L.batteryV);
  tileSet(g_tBatt, on && L.batteryV > 0.1f, b, levelBatt(L.batteryV), (L.batteryV - 10.0f) / 5.0f);
  snprintf(b, sizeof(b), "%d", (int)L.tempC);
  tileSet(g_tCool, on && L.tempC != 0, b, levelTemp((float)L.tempC), (float)L.tempC / 120.0f);

  float duty = (on && !yam) ? L.injectorMs * (float)rpm / 1200.0f : 0.0f;
  if (duty > 100.0f) duty = 100.0f;
  snprintf(b, sizeof(b), "%.0f", duty);
  tileSet(g_tInj, on && !yam, b, duty >= 90.0f ? 2 : (duty >= 80.0f ? 1 : 0), duty / 100.0f);
  snprintf(b, sizeof(b), "%.1f", L.ignitionDeg);
  tileSet(g_tIgn, on && !yam, b, levelIgn(L.ignitionDeg), L.ignitionDeg / 50.0f);
  snprintf(b, sizeof(b), "%.2f", L.o2Volt);
  tileSet(g_tO2, on && !yam, b, 0, L.o2Volt / 1.0f);
}

// ============================================================ LIVE DATA
#define DATA_ROWS 14
static lv_obj_t *g_dataVal[DATA_ROWS];
static lv_obj_t *g_dataEcm;
static char      s_dvTxt[DATA_ROWS][32];
static uint32_t  s_dvCol[DATA_ROWS];
static char      s_ecmTxt[40];

static void buildData() {
  for (int i = 0; i < DATA_ROWS; i++) { s_dvTxt[i][0] = '\0'; s_dvCol[i] = 0xFFFFFFFF; }
  s_ecmTxt[0] = '\0';

  lv_obj_t *s = g_scr[1] = mkScreen();
  buildTopBar(s, 1);
  mkLbl(s, LV_SYMBOL_LIST, &lv_font_montserrat_12, PCOL(cyan), 10, 29);
  mkLblClip(s, S(S_DATA_TITLE), fSm(), PCOL(cyan), 28, (29 + TOFF), 250);
  g_dataEcm = mkLblClip(s, "ECM: --", fSm(), PCOL(dim), 262, (29 + TOFF), 210, LV_TEXT_ALIGN_RIGHT);

  const char *names[DATA_ROWS] = {
    S(S_D_RPM), S(S_D_GEAR), S(S_D_SPD_RAW), S(S_D_SPD_CAL), S(S_D_TPS), S(S_D_TPSV), S(S_D_COOL),
    S(S_D_BATT), S(S_D_INJ), S(S_D_IGN), S(S_D_O2), S(S_D_AFR), S(S_D_CPU), S(S_D_LINK) };
  const uint32_t acc[DATA_ROWS] = {
    PCOL(orange), PCOL(cyan), PCOL(cyan), PCOL(cyan), PCOL(lime), PCOL(lime), PCOL(green),
    PCOL(yellow), PCOL(orange), PCOL(violet), PCOL(mint), PCOL(pink), PCOL(dim), PCOL(green) };
  for (int i = 0; i < DATA_ROWS; i++) {
    const int col = i / 7, r = i % 7;
    const int x = col == 0 ? 6 : 244, y = 48 + r * 31;
    mkBox(s, x, y, 230, 28, PCOL(panel), 8, PCOL(border), 1);
    mkBox(s, x + 6, y + 8, 3, 12, acc[i], 2);
    mkLblClip(s, names[i], fSm(), PCOL(dim), x + 14, y + (6 + TOFF), 112);
    g_dataVal[i] = mkLblClip(s, "--", fVal(), PCOL(dim), x + 128, y + (g_set.lang ? 3 : 6), 96, LV_TEXT_ALIGN_RIGHT);
  }
}

static void dataUpdate() {
  const Live &L = g_live;
  const bool on = L.ecuConnected, yam = on && L.ecuType == ECU_TYPE_YAMAHA;
  char b[48];

  #define DV(i, ok, col) do { \
    const char *_nt = (ok) ? b : "--"; \
    const uint32_t _nc = (ok) ? (col) : PCOL(dim); \
    if (strcmp(s_dvTxt[i], _nt) != 0) { \
      strncpy(s_dvTxt[i], _nt, sizeof(s_dvTxt[i]) - 1); \
      s_dvTxt[i][sizeof(s_dvTxt[i]) - 1] = '\0'; \
      setTxt(g_dataVal[i], _nt); \
    } \
    if (s_dvCol[i] != _nc) { \
      s_dvCol[i] = _nc; \
      setCol(g_dataVal[i], _nc); \
    } \
  } while (0)

  const uint8_t gearArg = L.gear < 0 ? 255 : (uint8_t)L.gear;
  fmtInt(b, sizeof(b), on ? L.rpm : 0);                                       DV(0, on, lvlColor(on && L.rpm >= shiftLimit(g_set, gearArg) ? 2 : 0, PCOL(text)));
  if (L.gear == 0) snprintf(b, sizeof(b), "N"); else snprintf(b, sizeof(b), "%d", L.gear);   DV(1, on && L.gear >= 0, PCOL(cyan));
  snprintf(b, sizeof(b), "%d %s", (int)(L.speedRaw + 0.5f), S(S_KMH));        DV(2, on && L.hasSpeed, PCOL(text));
  snprintf(b, sizeof(b), "%d %s", (int)(L.speed + 0.5f), S(S_KMH));           DV(3, on && L.hasSpeed, lvlColor(levelSpeed(L.speed), PCOL(cyan)));
  snprintf(b, sizeof(b), "%.1f %%", L.tps);                                   DV(4, on && !yam, PCOL(text));
  snprintf(b, sizeof(b), "%.2f V", L.tpsVolt);                                DV(5, on && !yam, PCOL(text));
  snprintf(b, sizeof(b), "%d \xC2\xB0""C", (int)L.tempC);                     DV(6, on && L.tempC != 0, lvlColor(levelTemp((float)L.tempC), PCOL(green)));
  snprintf(b, sizeof(b), "%.1f V", L.batteryV);                               DV(7, on && L.batteryV > 0.1f, lvlColor(levelBatt(L.batteryV), PCOL(yellow)));
  snprintf(b, sizeof(b), "%.2f ms", L.injectorMs);                            DV(8, on && !yam, lvlColor(levelInj(L.injectorMs), PCOL(orange)));
  snprintf(b, sizeof(b), "%.1f \xC2\xB0", L.ignitionDeg);                     DV(9, on && !yam, lvlColor(levelIgn(L.ignitionDeg), PCOL(violet)));
  snprintf(b, sizeof(b), "%.2f V", L.o2Volt);                                 DV(10, on && !yam, PCOL(mint));
  snprintf(b, sizeof(b), "%.1f : 1", L.afr);                                  DV(11, on && !yam, lvlColor(levelAfr(L.afr), PCOL(pink)));
  snprintf(b, sizeof(b), "%u%% / %d\xC2\xB0""C", (unsigned)L.cpuLoad, (int)L.cpuTemp);
  DV(12, L.cpuLoad || L.cpuTemp, L.cpuTemp >= 75 ? PCOL(red) : (L.cpuTemp >= 60 ? PCOL(amber) : PCOL(text)));
  if (g_link == LINK_ESPNOW) snprintf(b, sizeof(b), S(S_LINK_ESPNOW), (int)ESPNOW_WIFI_CHANNEL);
  else snprintf(b, sizeof(b), "%s", S(S_LINK_KLINE));
  DV(13, g_link != LINK_NONE, g_link == LINK_ESPNOW ? PCOL(green) : PCOL(amber));
  #undef DV

  snprintf(b, sizeof(b), "ECM: %s", g_ecmId);
  if (strcmp(s_ecmTxt, b) != 0) {
    strncpy(s_ecmTxt, b, sizeof(s_ecmTxt) - 1);
    s_ecmTxt[sizeof(s_ecmTxt) - 1] = '\0';
    setTxt(g_dataEcm, b);
  }
}

// ============================================================ GRAPH
enum { GSIG_RPM = 0, GSIG_SPEED, GSIG_TPS, GSIG_TEMP, GSIG_BATT, GSIG_INJ, GSIG_IGN, GSIG_AFR, GSIG_N };
#define CHART_POINTS 120

static lv_obj_t         *g_chart = nullptr;
static lv_chart_series_t *g_ser[GSIG_N];
static lv_obj_t         *g_legPanel, *g_legRow[GSIG_N], *g_legVal[GSIG_N];
static lv_obj_t         *g_sigModal, *g_sigChip[GSIG_N];
static uint8_t           g_sigMask = 0x0F;

static uint32_t sigColor(int i) {
  const uint32_t c[GSIG_N] = { PCOL(orange), PCOL(cyan), PCOL(lime), PCOL(green), PCOL(yellow), PCOL(amber), PCOL(violet), PCOL(pink) };
  return c[i];
}
static const char *sigName(int i) {
  static const StrId id[GSIG_N] = { S_SIG_RPM, S_SIG_SPEED, S_SIG_TPS, S_SIG_TEMP, S_SIG_BATT, S_SIG_INJ, S_SIG_IGN, S_SIG_AFR };
  return S(id[i]);
}

static void chartLayout() {
  int k = 0;
  for (int i = 0; i < GSIG_N; i++) {
    const bool vis = g_sigMask & (1 << i);
    lv_chart_hide_series(g_chart, g_ser[i], !vis);
    show(g_legRow[i], vis);
    if (vis) { lv_obj_set_y(g_legRow[i], 4 + k * 18); k++; }
  }
  lv_obj_set_height(g_legPanel, k * 18 + 8);
  show(g_legPanel, k > 0);
}

static void sigChipCb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  g_sigMask ^= (uint8_t)(1 << i);
  const bool on = g_sigMask & (1 << i);
  setBg(g_sigChip[i], on ? sigColor(i) : PCOL(panel2));
  setCol(lv_obj_get_child(g_sigChip[i], 0), on ? PCOL(bg) : PCOL(dim));
  chartLayout();
}
static void sigOpenCb(lv_event_t *e) { (void)e; show(g_sigModal, true); lv_obj_move_foreground(g_sigModal); }
static void sigCloseCb(lv_event_t *e) { (void)e; show(g_sigModal, false); }
static void chartClearCb(lv_event_t *e) {
  (void)e;
  for (int i = 0; i < GSIG_N; i++) lv_chart_set_all_value(g_chart, g_ser[i], LV_CHART_POINT_NONE);
  lv_chart_refresh(g_chart);
}

static void buildChart() {
  lv_obj_t *s = g_scr[2] = mkScreen();
  buildTopBar(s, 2);
  g_chart = lv_chart_create(s);
  lv_obj_remove_style_all(g_chart);
  lv_obj_set_pos(g_chart, 4, 26);
  lv_obj_set_size(g_chart, 472, 244);
  lv_obj_set_style_bg_color(g_chart, lv_color_hex(PCOL(panel)), 0);
  lv_obj_set_style_bg_opa(g_chart, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g_chart, 11, 0);
  lv_obj_set_style_border_color(g_chart, lv_color_hex(PCOL(border)), 0);
  lv_obj_set_style_border_width(g_chart, 1, 0);
  lv_obj_set_style_pad_all(g_chart, 6, 0);
  lv_obj_set_style_line_color(g_chart, lv_color_hex(PCOL(track)), LV_PART_MAIN);
  lv_obj_set_style_line_width(g_chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(g_chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_width(g_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(g_chart, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(g_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(g_chart, CHART_POINTS);
  lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(g_chart, 5, 9);
  for (int i = 0; i < GSIG_N; i++) {
    g_ser[i] = lv_chart_add_series(g_chart, lv_color_hex(sigColor(i)), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(g_chart, g_ser[i], LV_CHART_POINT_NONE);
  }

  g_legPanel = mkBox(s, 10, 32, 112, 70, PCOL(bg), 8);
  lv_obj_set_style_bg_opa(g_legPanel, LV_OPA_70, 0);
  for (int i = 0; i < GSIG_N; i++) {
    g_legRow[i] = mkBox(g_legPanel, 0, 4, 112, 18, PCOL(bg), 0);
    lv_obj_set_style_bg_opa(g_legRow[i], LV_OPA_TRANSP, 0);
    mkBox(g_legRow[i], 6, 6, 6, 6, sigColor(i), LV_RADIUS_CIRCLE);
    mkLblClip(g_legRow[i], sigName(i), fSm(), PCOL(dim), 16, (1 + TOFF), 50);
    g_legVal[i] = mkLbl(g_legRow[i], "--", fLatin(10), sigColor(i), 64, 3, 44, LV_TEXT_ALIGN_RIGHT);
  }

  mkBtn(s, 472 - 8 - 72 - 54, 32, 72, 22, S(S_SIGNALS), fSm(), PCOL(panel2), PCOL(mint), PCOL(mint), sigOpenCb, 0);
  mkBtn(s, 472 - 8 - 50, 32, 50, 22, S(S_CLEAR), fSm(), PCOL(panel2), PCOL(dim), PCOL(border), chartClearCb, 0);

  g_sigModal = mkBox(s, 0, TOPH + 1, SW, SH - TOPH - 1, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_sigModal, LV_OPA_90, 0);
  lv_obj_add_flag(g_sigModal, LV_OBJ_FLAG_CLICKABLE);
  mkBox(g_sigModal, 18, 14, 3, 13, PCOL(orange), 2);
  mkLbl(g_sigModal, S(S_SEL_SIGNALS), fMd(), PCOL(text), 26, 8);
  for (int i = 0; i < GSIG_N; i++) {
    const bool on = g_sigMask & (1 << i);
    g_sigChip[i] = mkBtn(g_sigModal, 18 + (i % 4) * 112, 50 + (i / 4) * 54, 104, 42, sigName(i), fSm(),
                         on ? sigColor(i) : PCOL(panel2), on ? PCOL(bg) : PCOL(dim), 0, sigChipCb, i);
  }
  mkBtn(g_sigModal, 170, 176, 140, 36, S(S_OK), fMd(), PCOL(mint), PCOL(bg), 0, sigCloseCb, 0);
  show(g_sigModal, false);
  chartLayout();
}

static void chartPush() {
  if (!g_chart) return;
  const Live &L = g_live;
  const bool on = L.ecuConnected, yam = on && L.ecuType == ECU_TYPE_YAMAHA;
  const int32_t NONE = LV_CHART_POINT_NONE;
  int32_t v[GSIG_N];
  v[GSIG_RPM]   = on ? (int32_t)clampf(L.rpm * 100.0f / g_set.maxRpm, 0, 100) : NONE;
  v[GSIG_SPEED] = (on && L.hasSpeed) ? (int32_t)clampf(L.speed * 100.0f / 200.0f, 0, 100) : NONE;
  v[GSIG_TPS]   = (on && !yam) ? (int32_t)clampf(L.tps, 0, 100) : NONE;
  v[GSIG_TEMP]  = (on && L.tempC != 0) ? (int32_t)clampf(L.tempC * 100.0f / 150.0f, 0, 100) : NONE;
  v[GSIG_BATT]  = (on && L.batteryV > 0.1f) ? (int32_t)clampf(L.batteryV * 100.0f / 16.0f, 0, 100) : NONE;
  v[GSIG_INJ]   = (on && !yam) ? (int32_t)clampf(L.injectorMs * 100.0f / 15.0f, 0, 100) : NONE;
  v[GSIG_IGN]   = (on && !yam) ? (int32_t)clampf((L.ignitionDeg + 10.0f) * 100.0f / 70.0f, 0, 100) : NONE;
  v[GSIG_AFR]   = (on && !yam) ? (int32_t)clampf((L.afr - 8.0f) * 100.0f / 12.0f, 0, 100) : NONE;
  for (int i = 0; i < GSIG_N; i++) lv_chart_set_next_value(g_chart, g_ser[i], v[i]);

  if (g_page == 2) {
    char b[24];
    const char *fmt[GSIG_N] = { "%.0f", "%.0f", "%.0f", "%.0f", "%.1f", "%.2f", "%.1f", "%.1f" };
    const float val[GSIG_N] = { (float)L.rpm, L.speed, L.tps, (float)L.tempC, L.batteryV, L.injectorMs, L.ignitionDeg, L.afr };
    for (int i = 0; i < GSIG_N; i++) {
      if (v[i] == NONE) { setTxt(g_legVal[i], "--"); continue; }
      snprintf(b, sizeof(b), fmt[i], val[i]);
      setTxt(g_legVal[i], b);
    }
  }
}

// ============================================================ FAULT CODES
struct DtcName { uint8_t code; const char *pcode; const char *en; const char *th; };
static const DtcName DTC_HONDA[] = {
  { 1,  "P0105", "MAP sensor circuit failure",        "วงจรเซนเซอร์ความดันท่อไอดี (MAP) ผิดปกติ" },
  { 7,  "P0115", "ECT sensor circuit failure",        "วงจรเซนเซอร์อุณหภูมิน้ำหล่อเย็น (ECT) ผิดปกติ" },
  { 8,  "P0120", "TPS sensor circuit failure",        "วงจรเซนเซอร์ตำแหน่งคันเร่ง (TPS) ผิดปกติ" },
  { 9,  "P0110", "IAT sensor circuit failure",        "วงจรเซนเซอร์อุณหภูมิอากาศ (IAT) ผิดปกติ" },
  { 11, "P0500", "VS sensor circuit failure",         "วงจรเซนเซอร์ความเร็วรถ (VS) ผิดปกติ" },
  { 12, "P0201", "Injector circuit fault",            "วงจรหัวฉีดผิดปกติ" },
  { 21, "P0130", "O2 sensor circuit malfunction",     "วงจรเซนเซอร์ออกซิเจน (O2) ผิดปกติ" },
  { 29, "P0505", "IACV valve circuit fault",          "วงจรวาล์วควบคุมรอบเดินเบา (IACV) ผิดปกติ" },
  { 33, "P0603", "ECM EEPROM error",                  "หน่วยความจำ ECM (EEPROM) ผิดพลาด" },
  { 54, nullptr, "Bank angle sensor circuit fault",   "วงจรเซนเซอร์มุมเอียงรถ (ล้ม) ผิดปกติ" },
};
static const DtcName DTC_YAMAHA[] = {
  { 12, nullptr, "Crankshaft position sensor fault",  "เซนเซอร์ตำแหน่งเพลาข้อเหวี่ยงผิดปกติ" },
  { 13, nullptr, "Intake air pressure (MAP) sensor",  "เซนเซอร์ความดันอากาศไอดี (MAP) ผิดปกติ" },
  { 14, nullptr, "Air pressure pipe / hose",          "ท่อวัดความดันอากาศหลุดหรืออุดตัน" },
  { 15, nullptr, "TPS sensor short / open circuit",   "เซนเซอร์ตำแหน่งคันเร่ง (TPS) ลัดวงจรหรือวงจรเปิด" },
  { 16, nullptr, "TPS sensor stuck",                  "เซนเซอร์ตำแหน่งคันเร่ง (TPS) ค้าง" },
  { 19, nullptr, "Side stand switch open circuit",    "สวิตช์ขาตั้งข้างวงจรเปิด" },
  { 21, nullptr, "Coolant temperature sensor (ECT)",  "เซนเซอร์อุณหภูมิน้ำหล่อเย็น (ECT) ผิดปกติ" },
  { 22, nullptr, "Intake air temperature sensor",     "เซนเซอร์อุณหภูมิอากาศไอดี (IAT) ผิดปกติ" },
  { 24, nullptr, "O2 sensor no signal / malfunction", "เซนเซอร์ออกซิเจน (O2) ไม่มีสัญญาณหรือผิดปกติ" },
  { 30, nullptr, "Lean angle sensor activated",       "เซนเซอร์มุมเอียงทำงาน (รถล้ม)" },
  { 33, nullptr, "Primary ignition coil fault",       "คอยล์จุดระเบิดหลักผิดปกติ" },
  { 39, nullptr, "Fuel injector open / short",        "หัวฉีดวงจรเปิดหรือลัดวงจร" },
  { 41, nullptr, "Lean angle (tip-over) sensor",      "เซนเซอร์มุมเอียงรถ (ล้ม) ผิดปกติ" },
  { 42, nullptr, "Speed sensor / neutral switch",     "เซนเซอร์ความเร็ว / สวิตช์เกียร์ว่างผิดปกติ" },
  { 46, nullptr, "Power supply system malfunction",   "ระบบไฟเลี้ยงผิดปกติ" },
};
static const DtcName *dtcFind(uint8_t code, uint8_t ecuType) {
  const DtcName *tab = ecuType == ECU_TYPE_YAMAHA ? DTC_YAMAHA : DTC_HONDA;
  const size_t n = ecuType == ECU_TYPE_YAMAHA ? sizeof(DTC_YAMAHA) / sizeof(DtcName) : sizeof(DTC_HONDA) / sizeof(DtcName);
  for (size_t i = 0; i < n; i++) if (tab[i].code == code) return &tab[i];
  return nullptr;
}

static lv_obj_t *g_dtcBanner, *g_dtcIcon, *g_dtcTitle, *g_dtcSub, *g_dtcList, *g_dtcTag, *g_dtcTagTxt, *g_dtcEcm;
static bool g_dtcRedraw = true;

static void dtcReadCb(lv_event_t *e) {
  (void)e;
  sendDtcCommand(0x01);
  g_dtcState = DTC_READING; g_dtcT0 = millis(); g_dtcRedraw = true;
}
static void dtcClearCb(lv_event_t *e) {
  (void)e;
  if (g_live.rpm > 300) { g_dtcState = DTC_ENGINE_ON; g_dtcT0 = millis(); g_dtcRedraw = true; return; }
  sendDtcCommand(0x02);
  g_dtcState = DTC_CLEARING; g_dtcT0 = millis(); g_dtcRedraw = true;
}

static void buildDtc() {
  lv_obj_t *s = g_scr[3] = mkScreen();
  buildTopBar(s, 3);
  mkLbl(s, LV_SYMBOL_WARNING, &lv_font_montserrat_12, PCOL(amber), 10, 29);
  mkLblClip(s, S(S_DTC_TITLE), fSm(), PCOL(amber), 28, (29 + TOFF), 300);
  g_dtcEcm = mkLblClip(s, "ECM: --", fSm(), PCOL(dim), 310, (29 + TOFF), 162, LV_TEXT_ALIGN_RIGHT);

  g_dtcBanner = mkBox(s, 6, 48, 468, 46, PCOL(panel), 10, PCOL(border), 2);
  lv_obj_t *ring = mkBox(g_dtcBanner, 8, 9, 26, 26, PCOL(panel2), LV_RADIUS_CIRCLE);
  g_dtcIcon = mkLbl(ring, LV_SYMBOL_LIST, &lv_font_montserrat_14, PCOL(dim), 0, 0);
  lv_obj_center(g_dtcIcon);
  g_dtcTitle = mkLblClip(g_dtcBanner, "", fSm(), PCOL(text), 44, (5 + TOFF), 330);
  g_dtcSub   = mkLblClip(g_dtcBanner, "", fSm(), PCOL(dim), 44, (23 + TOFF), 330);
  g_dtcTag = mkBox(g_dtcBanner, 376, 10, 84, 24, PCOL(panel2), 12, PCOL(amber), 1);
  g_dtcTagTxt = mkLbl(g_dtcTag, "", fSm(), PCOL(amber), 0, 0, 82, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(g_dtcTagTxt);
  show(g_dtcTag, false);

  g_dtcList = mkBox(s, 6, 98, 468, 124, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_dtcList, LV_OPA_TRANSP, 0);
  lv_obj_add_flag(g_dtcList, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_dtcList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_dtcList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_dtcList, LV_OBJ_FLAG_CLICKABLE);

  char tx[48];
  snprintf(tx, sizeof(tx), "%s  %s", LV_SYMBOL_REFRESH, S(S_BTN_READ));
  mkBtn(s, 6, 228, 150, 38, tx, fSm(), PCOL(panel2), PCOL(cyan), PCOL(cyan), dtcReadCb, 0);
  snprintf(tx, sizeof(tx), "%s  %s", LV_SYMBOL_TRASH, S(S_BTN_CLEAR));
  mkBtn(s, 324, 228, 150, 38, tx, fSm(), PCOL(panel2), PCOL(red), PCOL(red), dtcClearCb, 0);
  g_dtcRedraw = true;
}

static void dtcDrawList() {
  lv_obj_clean(g_dtcList);
  const uint8_t maxItems = sizeof(g_dtc.items) / sizeof(g_dtc.items[0]);
  const uint8_t n = (g_dtcState == DTC_DONE && !g_dtc.busy) ? (g_dtc.count < maxItems ? g_dtc.count : maxItems) : 0;
  for (uint8_t i = 0; i < n; i++) {
    const int y = i * 48;
    const DtcName *dn = dtcFind(g_dtc.items[i].mainCode, g_live.ecuType);
    lv_obj_t *row = mkBox(g_dtcList, 0, y, 468, 44, PCOL(panel), 10, PCOL(border), 1);
    mkBox(row, 0, 8, 4, 26, PCOL(amber), 2);
    lv_obj_t *badge = mkBox(row, 12, 7, 62, 28, PCOL(panel2), 8, PCOL(amber), 1);
    char b[48];
    if (dn && dn->pcode) snprintf(b, sizeof(b), "%s", dn->pcode); else snprintf(b, sizeof(b), "%02u", g_dtc.items[i].mainCode);
    lv_obj_t *cl = mkLbl(badge, b, fLatin(14), PCOL(amber), 0, 0); lv_obj_center(cl);
    mkLblClip(row, dn ? (g_set.lang ? dn->th : dn->en) : S(S_DTC_UNKNOWN), fSm(), PCOL(text), 84, (4 + TOFF), 290);
    snprintf(b, sizeof(b), "%s %02u-%02u", S(S_DTC_CODE), g_dtc.items[i].mainCode, g_dtc.items[i].subCode);
    mkLblClip(row, b, fSm(), PCOL(dim), 84, (22 + TOFF), 200);
    lv_obj_t *tag = mkBox(row, 380, 9, 76, 24, PCOL(panel2), 12, PCOL(red), 1);
    lv_obj_t *tl = mkLbl(tag, S(S_DTC_ACTIVE), fSm(), PCOL(red), 0, 0, 74, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(tl);
  }
}

static void dtcUpdate() {
  if (!g_dtcBanner) return;
  if ((g_dtcState == DTC_READING || g_dtcState == DTC_CLEARING) && millis() - g_dtcT0 > 6000) { g_dtcState = DTC_TIMEOUT; g_dtcRedraw = true; }
  if (g_dtcState == DTC_ENGINE_ON && millis() - g_dtcT0 > 3000) { g_dtcState = DTC_IDLE; g_dtcRedraw = true; }

  if (g_dtcFresh) {
    g_dtcFresh = false;
    g_dtcState = g_dtc.busy ? DTC_BUSY : DTC_DONE;
    g_dtcRedraw = true;
    dtcDrawList();
  }
  if (!g_dtcRedraw) return;
  g_dtcRedraw = false;

  uint32_t col = PCOL(dim);
  const char *title = "", *sub = "", *icon = LV_SYMBOL_LIST;
  bool tag = false;
  char tb[64] = "", tg[24] = "";
  switch (g_dtcState) {
    case DTC_READING:   title = S(S_DTC_READING);  col = PCOL(amber); icon = LV_SYMBOL_REFRESH; break;
    case DTC_CLEARING:  title = S(S_DTC_CLEARING); col = PCOL(amber); icon = LV_SYMBOL_REFRESH; break;
    case DTC_BUSY:      title = S(S_DTC_PROC);     col = PCOL(amber); icon = LV_SYMBOL_REFRESH; break;
    case DTC_ENGINE_ON: title = S(S_DTC_ENGON);    sub = S(S_DTC_ENGON_S);    col = PCOL(red); icon = LV_SYMBOL_WARNING; break;
    case DTC_TIMEOUT:   title = S(S_DTC_TIMEOUT);  sub = S(S_DTC_TIMEOUT_S);  col = PCOL(red); icon = LV_SYMBOL_WARNING; break;
    case DTC_DONE:
      if (g_dtc.count == 0) { title = S(S_DTC_NONE); col = PCOL(green); icon = LV_SYMBOL_OK; }
      else {
        snprintf(tb, sizeof(tb), S(S_DTC_FOUND), (unsigned)g_dtc.count); title = tb;
        snprintf(tg, sizeof(tg), S(S_DTC_TAG), (unsigned)g_dtc.count);
        col = PCOL(amber); icon = LV_SYMBOL_WARNING; tag = true;
      }
      break;
    default:            title = S(S_DTC_IDLE); sub = S(S_DTC_IDLE_S); break;
  }
  setTxt(g_dtcTitle, title); setCol(g_dtcTitle, col);
  setTxt(g_dtcSub, sub);
  setTxt(g_dtcIcon, icon); setCol(g_dtcIcon, col); lv_obj_center(g_dtcIcon);
  show(g_dtcTag, tag);
  if (tag) { setTxt(g_dtcTagTxt, tg); lv_obj_center(g_dtcTagTxt); }
  setBdr(g_dtcBanner, col);
  char ecm[40]; snprintf(ecm, sizeof(ecm), "ECM: %s", g_ecmId);
  setTxt(g_dtcEcm, ecm);
  if (g_dtcState != DTC_DONE) dtcDrawList();
}
// ============================================================ FUEL MAP (page 4)
// Self-learning AFR maps (see fuel_map.h): RPM x TPS, SPD x GEAR, and an advisor view (G + SPD).
// Cells are only repainted when their text / colour really changes; the page refreshes ~8x per second.
static lv_obj_t *g_fuTab[3], *g_fuView[2], *g_fuAuto, *g_fuWarm;
static lv_obj_t *g_fuGrid, *g_fuAdv;                         // grid + left panel  |  advisor cards
static lv_obj_t *g_fuCell[FM_N][FM_N], *g_fuCellTx[FM_N][FM_N], *g_fuRowH[FM_N], *g_fuColH[FM_N], *g_fuCorner;
static lv_obj_t *g_fuName, *g_fuGearTag, *g_fuAfrVal, *g_fuAfrSt, *g_fuTgtVal, *g_fuTrimLbl, *g_fuTgtBtn;
static lv_obj_t *g_fuMsg;
static lv_obj_t *g_faVal[3], *g_faSub[3], *g_faSub2, *g_faStrip[3];
static int       g_fuSelR = 2, g_fuSelC = 2;
static bool      g_fuFollow = true, g_fuForce = true;
static uint32_t  g_fuTapMs = 0;
static float     s_fuLph = 0, s_fuKpl = 0;

static uint32_t fuFill(uint8_t st) {
  switch (st) { case FS_RICH: return 0x1A3F9E; case FS_OK: return 0x05503E; case FS_SLEAN: return 0x8A5A00; case FS_LEAN: return 0x8A1235; default: return PCOL(panel2); }
}
static uint32_t fuAccent(uint8_t st) {
  switch (st) { case FS_RICH: return 0x5B8CFF; case FS_OK: return PCOL(green); case FS_SLEAN: return PCOL(amber); case FS_LEAN: return PCOL(red); default: return PCOL(dim); }
}
static const char *fuStatusTxt(uint8_t st) {
  return st == FS_RICH ? S(S_B_RICH) : (st == FS_OK ? S(S_FM_OK) : ((st == FS_LEAN || st == FS_SLEAN) ? S(S_B_LEAN) : "--"));
}

static void fuelPaintButtons() {
  for (int i = 0; i < 3; i++) {
    const bool on = g_fuel.mode == i;
    setBg(g_fuTab[i], on ? PCOL(cyan) : PCOL(panel));
    setCol(lv_obj_get_child(g_fuTab[i], 0), on ? PCOL(bg) : PCOL(dim));
  }
  for (int i = 0; i < 2; i++) {
    const bool on = g_fuel.view == i;
    setBg(g_fuView[i], on ? PCOL(mint) : PCOL(panel));
    setCol(lv_obj_get_child(g_fuView[i], 0), on ? PCOL(bg) : PCOL(dim));
  }
  setBg(g_fuAuto, g_fuel.autoTune ? PCOL(amber) : PCOL(panel));
  setCol(lv_obj_get_child(g_fuAuto, 0), g_fuel.autoTune ? PCOL(bg) : PCOL(dim));
  setBg(g_fuWarm, g_fuel.warmOnly ? PCOL(cyan) : PCOL(panel));
  setCol(lv_obj_get_child(g_fuWarm, 0), g_fuel.warmOnly ? PCOL(bg) : PCOL(dim));
  show(g_fuGrid, g_fuel.mode != 2);
  show(g_fuAdv, g_fuel.mode == 2);
  g_fuForce = true;
}

static void fuTabCb(lv_event_t *e)  { g_fuel.mode = (uint8_t)(intptr_t)lv_event_get_user_data(e); g_fuFollow = true; g_fuelDirty = true; fuelPaintButtons(); }
static void fuViewCb(lv_event_t *e) { g_fuel.view = (uint8_t)(intptr_t)lv_event_get_user_data(e); g_fuelDirty = true; fuelPaintButtons(); }
static void fuAutoCb(lv_event_t *e) { (void)e; g_fuel.autoTune = g_fuel.autoTune ? 0 : 1; g_fuelDirty = true; fuelPaintButtons(); }
static void fuWarmCb(lv_event_t *e) { (void)e; g_fuel.warmOnly = g_fuel.warmOnly ? 0 : 1; g_fuelDirty = true; fuelPaintButtons(); }
static void fuCellCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  g_fuSelR = id / FM_N; g_fuSelC = id % FM_N;
  g_fuFollow = false; g_fuTapMs = millis(); g_fuForce = true;
}
static FuelCell *fuSelCell() { return g_fuel.mode == 1 ? &g_fuel.sg[g_fuSelR][g_fuSelC] : &g_fuel.rt[g_fuSelR][g_fuSelC]; }
static void fuTrimCb(lv_event_t *e) {
  FuelCell *c = fuSelCell();
  c->trim = (int8_t)clampi((int)c->trim + (int)(intptr_t)lv_event_get_user_data(e), -50, 50);
  g_fuelDirty = true; g_fuTapMs = millis(); g_fuForce = true;
}
static void fuSetTgtCb(lv_event_t *e) {
  (void)e;
  FuelCell *c = fuSelCell();
  if (c->n < FM_MIN_N) return;
  c->tgt = clampf(floorf(c->afr * 10.0f + 0.5f) / 10.0f, 10.0f, 18.0f);
  char b[40]; snprintf(b, sizeof(b), S(S_FM_TGT_SET), c->tgt);
  toast(b, PCOL(amber));
  g_fuelDirty = true; g_fuForce = true;
}
static void fuResetClickCb(lv_event_t *e) { (void)e; toast(S(S_FM_RESET_HINT), PCOL(dim)); }
static void fuResetHoldCb(lv_event_t *e) {
  (void)e;
  const FuelMap keep = g_fuel;
  fuelDefaults(g_fuel);
  g_fuel.autoTune = keep.autoTune; g_fuel.warmOnly = keep.warmOnly; g_fuel.view = keep.view; g_fuel.mode = keep.mode;
  g_fuelDirty = true; g_fuForce = true;
  toast(S(S_FM_RESET_DONE), PCOL(amber));
}

static lv_obj_t *fuBtn(lv_obj_t *p, int x, int y, int w, int h, const char *t, lv_event_cb_t cb, intptr_t id, uint32_t outline) {
  return mkBtn(p, x, y, w, h, t, fLatin(12), PCOL(panel), PCOL(dim), outline, cb, id);
}

static void buildFuel() {
  lv_obj_t *s = g_scr[4] = mkScreen();
  buildTopBar(s, 4);

  // ---- tabs (left) and switches (right)
  g_fuTab[0] = fuBtn(s, 4,   26, 78, 20, S(S_FM_TAB_RT), fuTabCb, 0, PCOL(cyan));
  g_fuTab[1] = fuBtn(s, 86,  26, 78, 20, S(S_FM_TAB_SG), fuTabCb, 1, PCOL(cyan));
  g_fuTab[2] = fuBtn(s, 168, 26, 78, 20, S(S_FM_TAB_GS), fuTabCb, 2, PCOL(amber));
  g_fuView[0] = fuBtn(s, 256, 26, 38, 20, "AFR",   fuViewCb, 0, PCOL(mint));
  g_fuView[1] = fuBtn(s, 298, 26, 48, 20, "TRIM%", fuViewCb, 1, PCOL(mint));
  g_fuAuto    = fuBtn(s, 350, 26, 74, 20, S(S_FM_AUTO), fuAutoCb, 0, PCOL(amber));
  g_fuWarm    = fuBtn(s, 428, 26, 48, 20, S(S_FM_WARM), fuWarmCb, 0, PCOL(cyan));

  // ---- grid page: left panel + 5 x 5 map (children are relative to y = 48)
  g_fuGrid = mkBox(s, 0, 48, SW, 186, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_fuGrid, LV_OPA_TRANSP, 0);
  lv_obj_t *cb = mkBox(g_fuGrid, 4, 2, 124, 40, PCOL(panel), 8, PCOL(cyan), 1);
  mkLbl(cb, S(S_FM_CELL), fSm(), PCOL(dim), 6, (1 + TOFF));
  g_fuGearTag = mkLblClip(cb, "--", fSm(), PCOL(amber), 56, (1 + TOFF), 60, LV_TEXT_ALIGN_RIGHT);
  g_fuName = mkLblClip(cb, "--", fVal(), PCOL(cyan), 6, g_set.lang ? 14 : 17, 112);

  lv_obj_t *ab = mkBox(g_fuGrid, 4, 46, 124, 70, PCOL(panel), 8, PCOL(border), 1);
  mkLblClip(ab, S(S_FM_ACTUAL), fSm(), PCOL(dim), 6, (1 + TOFF), 70);
  g_fuAfrVal = mkLblClip(ab, "--", fLatin(28), PCOL(dim), 5, 17, 70);
  g_fuAfrSt  = mkLblClip(ab, "--", fSm(), PCOL(dim), 6, (48 + TOFF), 60);
  mkLblClip(ab, S(S_FM_TARGET), fSm(), PCOL(dim), 70, (1 + TOFF), 48, LV_TEXT_ALIGN_RIGHT);
  g_fuTgtVal = mkLblClip(ab, "--", fLatin(22), PCOL(amber), 66, 22, 52, LV_TEXT_ALIGN_RIGHT);

  lv_obj_t *tb = mkBox(g_fuGrid, 4, 120, 124, 64, PCOL(panel), 8, PCOL(border), 1);
  g_fuTrimLbl = mkLblClip(tb, "--", fSm(), PCOL(dim), 6, (1 + TOFF), 112);
  g_fuTgtBtn = mkBtn(tb, 6, 19, 112, 20, S(S_FM_SETTGT), fSm(), PCOL(panel), PCOL(green), PCOL(green), fuSetTgtCb, 0);
  const char *tl[4] = { "-5", "-1", "+1", "+5" };
  const int   td[4] = { -5, -1, 1, 5 };
  for (int i = 0; i < 4; i++)
    mkBtn(tb, 6 + i * 29, 42, 26, 19, tl[i], fLatin(12), PCOL(panel2), td[i] < 0 ? PCOL(cyan) : PCOL(red), td[i] < 0 ? PCOL(cyan) : PCOL(red), fuTrimCb, td[i]);

  g_fuCorner = mkLblClip(g_fuGrid, "", fLatin(10), PCOL(dim), 132, 8, 54, LV_TEXT_ALIGN_CENTER);
  for (int c = 0; c < FM_N; c++) g_fuColH[c] = mkLbl(g_fuGrid, "", fLatin(12), PCOL(cyan), 188 + c * 57, 5, 57, LV_TEXT_ALIGN_CENTER);
  for (int r = 0; r < FM_N; r++) {
    g_fuRowH[r] = mkLbl(g_fuGrid, "", fLatin(12), PCOL(dim), 132, 26 + r * 30 + 7, 54, LV_TEXT_ALIGN_CENTER);
    for (int c = 0; c < FM_N; c++) {
      g_fuCell[r][c] = mkBox(g_fuGrid, 188 + c * 57 + 1, 26 + r * 30, 55, 28, PCOL(panel2), 5, PCOL(panel2), 2);
      lv_obj_add_flag(g_fuCell[r][c], LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(g_fuCell[r][c], clickSoundCb, LV_EVENT_CLICKED, NULL);
      lv_obj_add_event_cb(g_fuCell[r][c], fuCellCb, LV_EVENT_CLICKED, (void *)(intptr_t)(r * FM_N + c));
      g_fuCellTx[r][c] = mkLbl(g_fuCell[r][c], "--", fLatin(14), PCOL(dim), 0, 0, 51, LV_TEXT_ALIGN_CENTER);
      lv_obj_center(g_fuCellTx[r][c]);
    }
  }

  // ---- advisor page (G + SPD): three cards + a strip
  g_fuAdv = mkBox(s, 0, 48, SW, 186, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_fuAdv, LV_OPA_TRANSP, 0);
  const StrId ct[3] = { S_FA_1, S_FA_2, S_FA_3 };
  const uint32_t ca[3] = { PCOL(mint), PCOL(cyan), PCOL(yellow) };
  for (int i = 0; i < 3; i++) {
    lv_obj_t *c = mkBox(g_fuAdv, 4 + i * 160, 2, 152, 120, PCOL(panel), 10, i == 1 ? PCOL(cyan) : PCOL(border), 1);
    mkLblClip(c, S(ct[i]), fSm(), PCOL(dim), 4, (3 + TOFF), 142, LV_TEXT_ALIGN_CENTER);
    g_faVal[i] = mkLblClip(c, "--", fLatin(36), ca[i], 2, 30, 146, LV_TEXT_ALIGN_CENTER);
    g_faSub[i] = mkLblClip(c, "", fSm(), PCOL(dim), 4, (82 + TOFF), 142, LV_TEXT_ALIGN_CENTER);
    if (i == 0) g_faSub2 = mkLblClip(c, "", fSm(), PCOL(dim), 4, (98 + TOFF), 142, LV_TEXT_ALIGN_CENTER);
  }
  lv_obj_t *st = mkBox(g_fuAdv, 4, 130, 472, 34, PCOL(panel), 8, PCOL(border), 1);
  for (int i = 0; i < 3; i++) g_faStrip[i] = mkLblClip(st, "", fSm(), PCOL(text), 10 + i * 156, (8 + TOFF), 150);

  // ---- message bar + legend + reset (both pages)
  g_fuMsg = mkLbl(s, "", fSm(), PCOL(text), 6, (240 + TOFF), 292);
  lv_label_set_long_mode(g_fuMsg, LV_LABEL_LONG_CLIP);
  lv_obj_set_height(g_fuMsg, 30);
  const uint32_t lg[3] = { 0x1A3F9E, 0x05503E, 0x8A1235 };
  const StrId    ln[3] = { S_B_RICH, S_FM_OK, S_B_LEAN };
  for (int i = 0; i < 3; i++) {
    mkBox(s, 304 + i * 44, 246, 8, 8, lg[i], 2);
    mkLblClip(s, S(ln[i]), fSm(), PCOL(dim), 315 + i * 44, (243 + TOFF), 30);
  }
  lv_obj_t *rb = mkBtn(s, 438, 242, 38, 24, S(S_FM_RESET), fSm(), PCOL(panel), PCOL(dim), PCOL(border), fuResetClickCb, 0);
  lv_obj_add_event_cb(rb, fuResetHoldCb, LV_EVENT_LONG_PRESSED, NULL);
  g_fuForce = true;
  fuelPaintButtons();
}

static void fuelLeave() {
  if (g_fuelDirty) { fuelSave(); g_fuelDirty = false; }
}

static void fuelUpdate() {
  static uint32_t t = 0;
  const uint32_t now = millis();
  if (!g_fuForce && (uint32_t)(now - t) < 120) return;
  t = now; g_fuForce = false;

  const Live &L = g_live;
  const bool on = L.ecuConnected && L.ecuType != ECU_TYPE_YAMAHA;
  const int mode = g_fuel.mode == 1 ? 1 : 0;                        // which grid is on screen (advisor uses the RPM x TPS map)
  FuelCell (*map)[FM_N] = mode == 1 ? g_fuel.sg : g_fuel.rt;

  int lr = -1, lc = -1;                                             // cell the engine is in right now
  if (on) {
    if (mode == 0) { lr = fmRpmRow(L.rpm); lc = fmTpsCol(L.tps); }
    else           { lr = fmSpdRow(L.speed); lc = fmGearCol(L.gear); }
  }
  if (!g_fuFollow && (uint32_t)(now - g_fuTapMs) > 10000) g_fuFollow = true;       // back to the live cell after 10 s
  if (g_fuFollow && lr >= 0 && lc >= 0) { g_fuSelR = lr; g_fuSelC = lc; }

  char b[64];
  if (g_fuel.mode != 2) {
    setTxt(g_fuCorner, mode == 0 ? "RPM\\TPS" : "km/h\\G");
    for (int c = 0; c < FM_N; c++) { setTxt(g_fuColH[c], fmColLabel(mode, c)); setCol(g_fuColH[c], c == lc ? PCOL(amber) : PCOL(cyan)); }
    for (int r = 0; r < FM_N; r++) {
      setTxt(g_fuRowH[r], fmRowLabel(mode, r)); setCol(g_fuRowH[r], r == lr ? PCOL(amber) : PCOL(dim));
      for (int c = 0; c < FM_N; c++) {
        const FuelCell &cell = map[r][c];
        uint32_t fill; uint32_t tc = 0xFFFFFF;
        if (g_fuel.view == 0) {
          const uint8_t st = fuelStatus(cell);
          fill = fuFill(st); tc = st == FS_NONE ? PCOL(dim) : 0xFFFFFF;
          if (st == FS_NONE) snprintf(b, sizeof(b), "--"); else snprintf(b, sizeof(b), "%.1f", cell.afr);
        } else {
          fill = cell.trim > 0 ? 0x8A5A00 : (cell.trim < 0 ? 0x1A3F9E : PCOL(panel2));
          tc = cell.trim == 0 ? PCOL(dim) : 0xFFFFFF;
          if (cell.trim == 0) snprintf(b, sizeof(b), "0"); else snprintf(b, sizeof(b), "%+d", (int)cell.trim);
        }
        setTxt(g_fuCellTx[r][c], b); lv_obj_center(g_fuCellTx[r][c]);
        setCol(g_fuCellTx[r][c], tc);
        setBg(g_fuCell[r][c], fill);
        const bool sel = (r == g_fuSelR && c == g_fuSelC), live = (r == lr && c == lc);
        setBdr(g_fuCell[r][c], sel ? PCOL(text) : (live ? PCOL(amber) : fill));
      }
    }
    // ---- left panel: the selected cell
    const FuelCell &sc = map[g_fuSelR][g_fuSelC];
    snprintf(b, sizeof(b), "%sx%s", fmRowLabel(mode, g_fuSelR), fmColLabel(mode, g_fuSelC)); setTxt(g_fuName, b);
    if (!on || L.gear < 0) snprintf(b, sizeof(b), "--"); else if (L.gear == 0) snprintf(b, sizeof(b), "N"); else snprintf(b, sizeof(b), "G%d", L.gear);
    setTxt(g_fuGearTag, b);
    const uint8_t sst = fuelStatus(sc);
    if (sst == FS_NONE) snprintf(b, sizeof(b), "--"); else snprintf(b, sizeof(b), "%.1f", sc.afr);
    setTxt(g_fuAfrVal, b); setCol(g_fuAfrVal, fuAccent(sst));
    setTxt(g_fuAfrSt, fuStatusTxt(sst)); setCol(g_fuAfrSt, fuAccent(sst));
    snprintf(b, sizeof(b), "%.1f", sc.tgt); setTxt(g_fuTgtVal, b);
    snprintf(b, sizeof(b), "%s: %+d%%", S(S_FM_TRIM), (int)sc.trim); setTxt(g_fuTrimLbl, b);
  } else {
    // ---- advisor cards use the live RPM x TPS cell
    const FuelCell *lcell = (lr >= 0 && lc >= 0 && mode == 0) ? &g_fuel.rt[lr][lc] : nullptr;
    const float afrNow = on ? L.afr : 0.0f;
    const float tgt = lcell ? lcell->tgt : 14.7f;
    const uint8_t stNow = on ? fuelStatusOf(afrNow, tgt) : FS_NONE;
    if (on) snprintf(b, sizeof(b), "%.1f", afrNow); else snprintf(b, sizeof(b), "--");
    setTxt(g_faVal[0], b); setCol(g_faVal[0], fuAccent(stNow));
    setTxt(g_faSub[0], on ? fuStatusTxt(stNow) : ""); setCol(g_faSub[0], fuAccent(stNow));
    snprintf(b, sizeof(b), S(S_FA_TGT), tgt); setTxt(g_faSub2, on ? b : "");
    // fuel advice for the live cell (learned average if there is one, otherwise the live reading)
    const float basis = (lcell && lcell->n >= FM_MIN_N) ? lcell->afr : afrNow;
    const int need = on ? fuelNeedPct(basis, tgt, lcell ? lcell->trim : 0) : 0;
    if (on) snprintf(b, sizeof(b), "%+d%%", need); else snprintf(b, sizeof(b), "--");
    setTxt(g_faVal[1], b); setCol(g_faVal[1], need > 1 ? PCOL(amber) : (need < -1 ? 0x5B8CFF : PCOL(green)));
    if (on && L.injectorMs > 0.05f) snprintf(b, sizeof(b), S(S_FA_INJ), L.injectorMs, L.injectorMs * (1.0f + (float)need / 100.0f)); else b[0] = 0;
    setTxt(g_faSub[1], b);
    // economy
    const float lph = on ? fuelLitresPerHour(L.injectorMs, L.rpm, g_set.injCc, g_set.cyl) : 0.0f;
    s_fuLph += (lph - s_fuLph) * 0.2f;
    float kpl = (s_fuLph > 0.05f && on && L.hasSpeed && L.speed > 3.0f) ? L.speed / s_fuLph : 0.0f;
    s_fuKpl += (kpl - s_fuKpl) * 0.2f;
    if (g_set.injCc == 0) { setTxt(g_faVal[2], "--"); setTxt(g_faSub[2], S(S_FA_SETINJ)); }
    else {
      if (s_fuKpl > 0.1f) snprintf(b, sizeof(b), "%.1f", s_fuKpl); else snprintf(b, sizeof(b), "--");
      setTxt(g_faVal[2], b);
      snprintf(b, sizeof(b), S(S_FA_LPH), s_fuLph); setTxt(g_faSub[2], b);
    }
    // strip: rpm / gear / coolant
    char g[8]; if (!on || L.gear < 0) snprintf(g, sizeof(g), "--"); else if (L.gear == 0) snprintf(g, sizeof(g), "N"); else snprintf(g, sizeof(g), "%d", L.gear);
    char rr[12]; fmtInt(rr, sizeof(rr), on ? L.rpm : 0);
    snprintf(b, sizeof(b), "%s: %s", S(S_RPM), rr); setTxt(g_faStrip[0], b);
    snprintf(b, sizeof(b), "%s: %s", S(S_GEAR), g); setTxt(g_faStrip[1], b);
    snprintf(b, sizeof(b), "%s: %d\xC2\xB0""C", S(S_TILE_COOL), on ? (int)L.tempC : 0); setTxt(g_faStrip[2], b);
  }

  // ---- message bar (about the selected cell in grid mode, the live cell in advisor mode)
  const FuelCell *mc = nullptr; const char *mname = "";
  char nm[24];
  if (g_fuel.mode == 2) { if (on && lr >= 0 && lc >= 0) { mc = &g_fuel.rt[lr][lc]; snprintf(nm, sizeof(nm), "%sx%s", fmRowLabel(0, lr), fmColLabel(0, lc)); mname = nm; } }
  else { mc = &map[g_fuSelR][g_fuSelC]; snprintf(nm, sizeof(nm), "%sx%s", fmRowLabel(mode, g_fuSelR), fmColLabel(mode, g_fuSelC)); mname = nm; }
  if (!g_fuel.autoTune)                                           snprintf(b, sizeof(b), "%s", S(S_FM_OFF));
  else if (on && g_fuel.warmOnly && L.tempC < FM_WARM_C)          snprintf(b, sizeof(b), "%s", S(S_FM_PAUSED));
  else if (!mc || fuelStatus(*mc) == FS_NONE)                     snprintf(b, sizeof(b), S(S_FM_NODATA), mname);
  else {
    const uint8_t st = fuelStatus(*mc);
    const int need = fuelNeedPct(mc->afr, mc->tgt, mc->trim);
    if (st == FS_OK)        snprintf(b, sizeof(b), S(S_FM_OK_MSG), mname, mc->afr, mc->tgt);
    else if (st == FS_RICH) snprintf(b, sizeof(b), S(S_FM_RICH_MSG), mname, mc->afr, need < 0 ? -need : need);
    else                    snprintf(b, sizeof(b), S(S_FM_LEAN_MSG), mname, mc->afr, need < 0 ? 0 : need);
  }
  setTxt(g_fuMsg, b);
  setCol(g_fuMsg, (mc && fuelStatus(*mc) >= FS_SLEAN) ? PCOL(amber) : PCOL(text));
}
