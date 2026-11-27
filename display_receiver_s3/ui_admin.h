/*
  ui_admin.h - ECU brand selection screen, admin PIN dialog, admin settings (general / RPM alert / clock),
  UI (re)build and update loop.
*/
#pragma once

// 1 = print the default PIN on the PIN dialog (handy while testing; set to 0 on the finished bike)
#define SHOW_PIN_HINT 0

static lv_obj_t *g_scrSelect = nullptr, *g_scrPin = nullptr, *g_scrAdmin = nullptr;
static bool      g_adminActive = false;
static uint32_t  g_adminLast = 0;
static lv_timer_t *g_adminTimer = nullptr;
static bool      g_tachDirty = false;

static void adminTouch() { g_adminLast = millis(); }

// ============================================================ ECU brand selection
static void selectCb(lv_event_t *e) {
  const uint8_t b = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  g_set.brand = b;
  settingsSave();
  onBrandChanged();
  uiShowPage(0);
  topBarUpdate();
  lv_obj_t *old = g_scrSelect;
  g_scrSelect = nullptr;
  if (old) lv_obj_del_async(old);
}
static void selectLangCb(lv_event_t *e) {
  (void)e;
  g_set.lang = g_set.lang ? 0 : 1;
  settingsSave();
  g_rebuildUi = true;
}

static void buildSelect() {
  g_scrSelect = mkScreen();
  lv_obj_t *s = g_scrSelect;
  mkLbl(s, S(S_SEL_TITLE), g_set.lang ? fMd() : fLatin(22), PCOL(text), 24, 10);
  mkLblClip(s, S(S_SEL_SUB), fSm(), PCOL(dim), 24, (42 + TOFF), 380);
  mkBtn(s, SW - 96, 12, 80, 24, "EN | ไทย", fTh(), PCOL(panel2), PCOL(mint), PCOL(border), selectLangCb, 0);

  struct Card { int x; const char *name; const char *sub; const char *c1, *c2, *c3; uint32_t col; uint8_t brand; };
  const Card cards[2] = {
    { 24,  S(S_BRAND_HONDA),  "K-Line - Keihin ECU - 10400 bps", S(S_SEL_H1), S(S_SEL_H2), S(S_SEL_H3), 0xFF3B30, BRAND_HONDA },
    { 244, S(S_BRAND_YAMAHA), "K-Line - 15625 bps",              S(S_SEL_Y1), S(S_SEL_Y2), S(S_SEL_Y3), PCOL(cyan), BRAND_YAMAHA }
  };
  for (int i = 0; i < 2; i++) {
    const Card &c = cards[i];
    lv_obj_t *card = mkBox(s, c.x, 68, 212, 176, PCOL(panel), 14, c.col, 2);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, clickSoundCb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(card, selectCb, LV_EVENT_CLICKED, (void *)(intptr_t)c.brand);
    mkBox(card, 14, 14, 4, 22, c.col, 2);
    mkLbl(card, c.name, g_set.lang ? fMd() : fLatin(28), PCOL(text), 26, g_set.lang ? 12 : 8);
    mkLbl(card, c.sub, &lv_font_montserrat_10, PCOL(dim), 14, 44, 190);
    const char *chips[3] = { c.c1, c.c2, c.c3 };
    for (int k = 0; k < 3; k++) {
      mkBox(card, 14, 64 + k * 22, 184, 19, PCOL(panel2), 9);
      mkLblClip(card, chips[k], fSm(), PCOL(text), 22, ((64 + TOFF)) + k * 22, 172);
    }
    lv_obj_t *pill = mkBox(card, 14, 134, 184, 28, c.col, 14);
    char b[48]; snprintf(b, sizeof(b), "%s %s", S(S_SEL_BTN), c.name);
    lv_obj_t *pl = mkLbl(pill, b, fSm(), PCOL(bg), 0, 0, 180, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(pl);
  }
  mkLbl(s, S(S_SEL_FOOT), fSm(), PCOL(dim), 0, 250, SW, LV_TEXT_ALIGN_CENTER);
}

// ============================================================ admin: leave
static void adminLeave() {
  g_adminActive = false;
  g_ledPreview = false;
  if (g_adminTimer) lv_timer_pause(g_adminTimer);
  lv_obj_t *p = g_scrPin, *a = g_scrAdmin;
  g_scrPin = nullptr; g_scrAdmin = nullptr;
  g_page = 0;
  lv_scr_load(g_scr[0]);
  if (p) lv_obj_del_async(p);
  if (a) lv_obj_del_async(a);
  if (g_tachDirty) { g_tachDirty = false; g_rebuildUi = true; }   // maximum RPM changed: redraw the scale
}
static void adminTimerCb(lv_timer_t *t) {
  (void)t;
  if (g_adminActive && millis() - g_adminLast > ADMIN_IDLE_MS) { settingsLoad(); adminLeave(); }
}

// ============================================================ admin: PIN dialog (card in the middle, 3 x 4 keys, auto-submit)
static PinEntry   g_pin;
static lv_obj_t  *g_pinDots[ADMIN_PIN_LEN], *g_pinMsg = nullptr;
static void adminOpen();

static void pinPaint() {
  for (int i = 0; i < ADMIN_PIN_LEN; i++) {
    const bool f = i < g_pin.n;
    setBg(g_pinDots[i], f ? PCOL(cyan) : PCOL(panel2));
    setBdr(g_pinDots[i], f ? PCOL(cyan) : PCOL(border));
  }
}
// key ids: 0..9 digit, 11 backspace, 14 clear, 15 close
static void pinKeyCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  adminTouch();
  if (id <= 9) {
    g_pin.push((char)('0' + id));
    setTxt(g_pinMsg, S(S_PIN_SUB)); setCol(g_pinMsg, PCOL(dim));
    if (g_pin.n == ADMIN_PIN_LEN) {                                   // all digits entered: check at once
      if (g_pin.check()) { g_pin.clear(); adminOpen(); return; }
      g_pin.clear();
      setTxt(g_pinMsg, S(S_PIN_WRONG)); setCol(g_pinMsg, PCOL(red));
    }
  }
  else if (id == 11) g_pin.backspace();
  else if (id == 14) g_pin.clear();
  else if (id == 15) { adminLeave(); return; }
  pinPaint();
}

static void pinOpen() {
  g_pin.clear();
  g_adminActive = true;
  adminTouch();
  if (!g_adminTimer) g_adminTimer = lv_timer_create(adminTimerCb, 1000, NULL);
  lv_timer_resume(g_adminTimer);

  lv_obj_t *old = g_scrPin;
  g_scrPin = mkScreen();
  lv_obj_t *s = g_scrPin;
  lv_obj_t *c = mkBox(s, 90, 8, 300, 256, PCOL(panel), 16, PCOL(border), 2);
  mkLbl(c, S(S_PIN_TITLE), fMd(), PCOL(cyan), 16, 6, 230);
  mkBtn(c, 254, 6, 32, 26, LV_SYMBOL_CLOSE, &lv_font_montserrat_14, PCOL(panel), PCOL(dim), 0, pinKeyCb, 15);
  mkBox(c, 14, 36, 268, 1, PCOL(border), 0);
#if SHOW_PIN_HINT
  mkLbl(c, S(S_PIN_HINT), fSm(), PCOL(dim), 16, 40, 260);
#endif
  for (int i = 0; i < ADMIN_PIN_LEN; i++) g_pinDots[i] = mkBox(c, 76 + i * 25, 46, 14, 14, PCOL(panel2), LV_RADIUS_CIRCLE, PCOL(border), 2);
  g_pinMsg = mkLbl(c, S(S_PIN_SUB), fSm(), PCOL(dim), 0, 62, 296, LV_TEXT_ALIGN_CENTER);

  const char *lab[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", S(S_CLEAR_KEY), "0", LV_SYMBOL_BACKSPACE };
  const int   id[12]  = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 14, 0, 11 };
  for (int i = 0; i < 12; i++) {
    const bool clr = id[i] == 14, bks = id[i] == 11;
    mkBtn(c, 14 + (i % 3) * 90, 92 + (i / 3) * 40, 86, 34, lab[i], clr ? fSm() : (bks ? fLatin(14) : fLatin(22)),
          clr ? 0x3A1018 : PCOL(panel2), clr ? PCOL(red) : (bks ? PCOL(amber) : PCOL(text)), clr ? PCOL(red) : 0, pinKeyCb, id[i]);
  }
  pinPaint();
  lv_scr_load(s);
  if (old) lv_obj_del_async(old);
}

// ============================================================ admin: settings
enum { AP_MAXRPM = 0, AP_CAL, AP_SINGLE, AP_HOUR, AP_MIN, AP_LEDBR, AP_LEDST, AP_INJ, AP_CYL, AP_G1 };       // AP_G1 .. AP_G1+5 = gears 1..6
#define ADMIN_TABS 5          // 0 general, 1 rpm alert, 2 LED, 3 clock, 4 fuel

static lv_obj_t *g_aTab[ADMIN_TABS], *g_aPanel[ADMIN_TABS];
static lv_obj_t *g_aOdo, *g_aLive, *g_aCal, *g_aMax, *g_aSingle, *g_aGear[GEAR_COUNT], *g_aHour, *g_aMinute;
static lv_obj_t *g_aMode[2], *g_aGearBox, *g_aSingleBox, *g_aPreset[4];
static int       g_aTabSel = 0;
static lv_obj_t *g_aLedBtn[3], *g_aLedSw[LED_PER_SIDE], *g_aLedBr, *g_aLedSt;
static int       g_ledSel = 0;
static lv_obj_t *g_aInj, *g_aCyl;
static const uint32_t LED_PALETTE[12] = { 0x00FF30, 0x9BFF00, 0xFFE000, 0xFFA000, 0xFF6A00, 0xFF0000,
                                          0xFF1493, 0xA040FF, 0x1E50FF, 0x00E5FF, 0xFFFFFF, 0x000000 };

static void adminRefresh() {
  char b[32];
  formatKm(b, sizeof(b), g_set.odoKm); setTxt(g_aOdo, b);
  snprintf(b, sizeof(b), "%.1f", g_live.speed); setTxt(g_aLive, b);
  snprintf(b, sizeof(b), "%.1f", g_set.speedCalPct); setTxt(g_aCal, b);
  fmtInt(b, sizeof(b), g_set.maxRpm); setTxt(g_aMax, b);
  fmtInt(b, sizeof(b), g_set.shiftSingle); setTxt(g_aSingle, b);
  for (int i = 0; i < GEAR_COUNT; i++) { fmtInt(b, sizeof(b), g_set.shiftGear[i]); setTxt(g_aGear[i], b); }
  for (int k = 0; k < 2; k++) {
    const bool on = (k == g_set.shiftMode);
    setBg(g_aMode[k], on ? PCOL(red) : PCOL(panel2));
    setCol(lv_obj_get_child(g_aMode[k], 0), on ? 0xFFFFFF : PCOL(dim));
  }
  show(g_aGearBox, g_set.shiftMode == 0);
  show(g_aSingleBox, g_set.shiftMode == 1);
  const uint16_t pv[4] = { 8000, 9000, 10000, 12000 };
  for (int i = 0; i < 4; i++) {
    const bool on = g_set.maxRpm == pv[i];
    setBg(g_aPreset[i], on ? PCOL(orange) : PCOL(panel2));
    setCol(lv_obj_get_child(g_aPreset[i], 0), on ? PCOL(bg) : PCOL(dim));
  }
  {
    const bool st[3] = { g_set.ledOn != 0, g_set.ledFlash != 0, g_set.ledRev != 0 };
    const StrId nm[3] = { S_LED_ENABLE, S_LED_FLASH, S_LED_REV };
    for (int i = 0; i < 3; i++) {
      char t[40]; snprintf(t, sizeof(t), "%s: %s", S(nm[i]), st[i] ? S(S_ON) : S(S_OFF));
      btnText(g_aLedBtn[i], t);
      setBg(g_aLedBtn[i], st[i] ? PCOL(lime) : PCOL(panel));
      setCol(lv_obj_get_child(g_aLedBtn[i], 0), st[i] ? PCOL(bg) : PCOL(lime));
    }
    snprintf(b, sizeof(b), "%u", (unsigned)g_set.ledBright); setTxt(g_aLedBr, b);
    snprintf(b, sizeof(b), "%u", (unsigned)g_set.ledStart);  setTxt(g_aLedSt, b);
    for (int i = 0; i < LED_PER_SIDE; i++) {
      setBg(g_aLedSw[i], g_set.ledColor[i] ? g_set.ledColor[i] : PCOL(panel2));
      setBdr(g_aLedSw[i], i == g_ledSel ? PCOL(text) : PCOL(border));
    }
  }
  snprintf(b, sizeof(b), "%u", (unsigned)g_set.injCc); setTxt(g_aInj, b);
  snprintf(b, sizeof(b), "%u", (unsigned)g_set.cyl);   setTxt(g_aCyl, b);
  const int m = clockMinutes();
  if (m < 0) { setTxt(g_aHour, "--"); setTxt(g_aMinute, "--"); }
  else { snprintf(b, sizeof(b), "%02d", m / 60); setTxt(g_aHour, b); snprintf(b, sizeof(b), "%02d", m % 60); setTxt(g_aMinute, b); }
}
static void adminTabPaint() {
  g_ledPreview = (g_aTabSel == 2);                       // LED tab open: LEDs show all 8 colours steady
  for (int i = 0; i < ADMIN_TABS; i++) {
    show(g_aPanel[i], i == g_aTabSel);
    const bool on = i == g_aTabSel;
    setBg(g_aTab[i], on ? PCOL(orange) : PCOL(panel2));
    setCol(lv_obj_get_child(g_aTab[i], 0), on ? PCOL(bg) : PCOL(orange));
  }
}
static void adminTabCb(lv_event_t *e) { adminTouch(); g_aTabSel = (int)(intptr_t)lv_event_get_user_data(e); adminTabPaint(); adminRefresh(); }

static void stepCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  const int p = id / 2, dir = (id % 2) ? 1 : -1;
  adminTouch();
  if (p == AP_MAXRPM) { g_set.maxRpm = (uint16_t)clampi((int)g_set.maxRpm + dir * 500, 6000, 16000); settingsClamp(g_set); g_tachDirty = true; }
  else if (p == AP_CAL) g_set.speedCalPct = clampf(g_set.speedCalPct + dir * 0.5f, 50.0f, 150.0f);
  else if (p == AP_SINGLE) g_set.shiftSingle = (uint16_t)clampi((int)g_set.shiftSingle + dir * 100, 3000, g_set.maxRpm);
  else if (p == AP_LEDBR) g_set.ledBright = (uint8_t)clampi((int)g_set.ledBright + dir * 5, 5, 100);
  else if (p == AP_LEDST) g_set.ledStart = (uint8_t)clampi((int)g_set.ledStart + dir * 5, 40, 95);
  else if (p == AP_INJ) g_set.injCc = (uint16_t)clampi((int)g_set.injCc + dir * 10, 0, 2000);
  else if (p == AP_CYL) g_set.cyl = (uint8_t)clampi((int)g_set.cyl + dir, 1, 4);
  else if (p == AP_HOUR || p == AP_MIN) {
    int m = clockMinutes(); if (m < 0) m = 0;
    int h = m / 60, mi = m % 60;
    if (p == AP_HOUR) h = (h + dir + 24) % 24; else mi = (mi + dir + 60) % 60;
    clockSet(h, mi);
  }
  else { const int g = p - AP_G1; if (g >= 0 && g < GEAR_COUNT) g_set.shiftGear[g] = (uint16_t)clampi((int)g_set.shiftGear[g] + dir * 100, 3000, g_set.maxRpm); }
  adminRefresh();
}
static void presetCb(lv_event_t *e) {
  adminTouch();
  g_set.maxRpm = (uint16_t)(intptr_t)lv_event_get_user_data(e);
  settingsClamp(g_set); g_tachDirty = true; adminRefresh();
}
static void modeCb(lv_event_t *e) { adminTouch(); g_set.shiftMode = (uint8_t)(intptr_t)lv_event_get_user_data(e); adminRefresh(); }

static void odoEntered(float v) { g_set.odoKm = clampf(v, 0, 999999.0f); adminRefresh(); toast(S(S_A_ODO_OK), PCOL(mint)); }
static void realEntered(float v) {
  const float np = newCalPct(g_set.speedCalPct, g_live.speed, v);
  if (np == g_set.speedCalPct) toast(S(S_A_TOO_SLOW), PCOL(amber));
  else { g_set.speedCalPct = np; toast(S(S_A_CAL_OK), PCOL(cyan)); }
  adminRefresh();
}
static void odoSetCb(lv_event_t *e)  { (void)e; adminTouch(); numOpen(S(S_A_NUM_ODO), true, odoEntered); }
static void realSpdCb(lv_event_t *e) { (void)e; adminTouch(); numOpen(S(S_A_NUM_SPD), true, realEntered); }
static void tripResetCb(lv_event_t *e) { (void)e; adminTouch(); g_set.tripA = 0; g_set.tripB = 0; toast(S(S_A_TRIPS_OK), PCOL(pink)); }
static void testFlashCb(lv_event_t *e) { (void)e; adminTouch(); g_testFlashUntil = millis() + 3000; }
static void adminSaveCb(lv_event_t *e) { (void)e; settingsSave(); adminLeave(); }
static void adminExitCb(lv_event_t *e) { (void)e; settingsLoad(); adminLeave(); }

static void ledToggleCb(lv_event_t *e) {
  adminTouch();
  switch ((int)(intptr_t)lv_event_get_user_data(e)) {
    case 0: g_set.ledOn    = g_set.ledOn ? 0 : 1; break;
    case 1: g_set.ledFlash = g_set.ledFlash ? 0 : 1; break;
    default: g_set.ledRev  = g_set.ledRev ? 0 : 1; break;
  }
  adminRefresh();
}
static void ledSelCb(lv_event_t *e) { adminTouch(); g_ledSel = (int)(intptr_t)lv_event_get_user_data(e); adminRefresh(); }
static void ledPalCb(lv_event_t *e) {
  adminTouch();
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  g_set.ledColor[g_ledSel] = LED_PALETTE[id];
  if (g_ledSel < LED_PER_SIDE - 1) g_ledSel++;                // jump to the next LED: tap colours left to right
  adminRefresh();
}
static void ledGradCb(lv_event_t *e) {
  (void)e; adminTouch();
  Settings d; settingsDefaults(d);
  for (int i = 0; i < LED_PER_SIDE; i++) g_set.ledColor[i] = d.ledColor[i];
  adminRefresh();
}
static void ledTestCb(lv_event_t *e) { (void)e; adminTouch(); g_ledTestUntil = millis() + 4000; }

static lv_obj_t *mkStepper(lv_obj_t *p, int x, int y, int w, int id, lv_obj_t **val, uint32_t col) {
  mkBtn(p, x, y, 34, 32, "-", fLatin(22), PCOL(panel2), PCOL(text), 0, stepCb, id * 2);
  mkBtn(p, x + w - 34, y, 34, 32, "+", fLatin(22), PCOL(panel2), PCOL(text), 0, stepCb, id * 2 + 1);
  *val = mkLbl(p, "--", fLatin(22), col, x + 36, y + 4, w - 72, LV_TEXT_ALIGN_CENTER);
  return *val;
}

static void adminOpen() {
  adminTouch();
  lv_obj_t *old = g_scrAdmin;
  g_scrAdmin = mkScreen();
  lv_obj_t *s = g_scrAdmin;
  g_aTabSel = 0;
  mkBox(s, 0, 0, SW, 24, PCOL(top), 0);
  mkBox(s, 0, 24, SW, 1, PCOL(border), 0);
  lv_obj_t *chip = mkBox(s, 8, 4, 90, 16, 0x3A2A12, 8, PCOL(orange), 1);
  lv_obj_t *cl = mkLbl(chip, S(S_ADMIN_MODE), fSm(), PCOL(orange), 0, 0, 88, LV_TEXT_ALIGN_CENTER); lv_obj_center(cl);
  const StrId tabNames[ADMIN_TABS] = { S_TAB_GENERAL, S_TAB_RPM, S_TAB_LED, S_TAB_CLOCK, S_TAB_FUEL };
  for (int i = 0; i < ADMIN_TABS; i++)
    g_aTab[i] = mkBtn(s, 104 + i * 74, 3, 70, 18, S(tabNames[i]), fSm(), i == 0 ? PCOL(orange) : PCOL(panel2), i == 0 ? PCOL(bg) : PCOL(orange), PCOL(orange), adminTabCb, i);

  // ---- tab 0: general
  g_aPanel[0] = mkBox(s, 0, 25, SW, 210, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aPanel[0], LV_OPA_TRANSP, 0);
  lv_obj_t *c1 = mkCard(g_aPanel[0], 8, 5, 464, 70, S(S_A_ODO_TITLE), PCOL(mint));
  g_aOdo = mkLbl(c1, "0.0", fLatin(28), PCOL(mint), 14, 22, 220);
  mkLbl(c1, S(S_KM), fSm(), PCOL(dim), 232, 34, 40);
  mkBtn(c1, 262, 26, 92, 30, S(S_A_SET_ODO), fSm(), PCOL(panel2), PCOL(mint), PCOL(mint), odoSetCb, 0);
  mkBtn(c1, 360, 26, 96, 30, S(S_A_RST_TRIPS), fSm(), PCOL(panel2), PCOL(pink), PCOL(pink), tripResetCb, 0);

  lv_obj_t *c2 = mkCard(g_aPanel[0], 8, 81, 464, 120, S(S_A_CAL_TITLE), PCOL(cyan));
  mkLbl(c2, S(S_A_READING), fSm(), PCOL(dim), 14, 26, 70);
  g_aLive = mkLbl(c2, "0.0", fLatin(22), PCOL(cyan), 86, 22, 90);
  mkLbl(c2, S(S_KMH), fSm(), PCOL(dim), 178, 28, 50);
  mkStepper(c2, 240, 22, 214, AP_CAL, &g_aCal, PCOL(cyan));
  mkLbl(c2, "%", fSm(), PCOL(dim), 440, 6, 20);
  mkLbl(c2, S(S_A_CAL_HELP), fSm(), PCOL(dim), 14, 60, 440);
  mkBtn(c2, 14, 90, 200, 26, S(S_A_ENTER_REAL), fSm(), PCOL(cyan), PCOL(bg), 0, realSpdCb, 0);

  // ---- tab 1: RPM alert
  g_aPanel[1] = mkBox(s, 0, 25, SW, 210, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aPanel[1], LV_OPA_TRANSP, 0);
  lv_obj_t *c3 = mkCard(g_aPanel[1], 8, 5, 228, 82, S(S_A_MAX_TITLE), PCOL(orange));
  mkStepper(c3, 10, 22, 206, AP_MAXRPM, &g_aMax, PCOL(orange));
  const uint16_t pv[4] = { 8000, 9000, 10000, 12000 };
  for (int i = 0; i < 4; i++) {
    char b[8]; snprintf(b, sizeof(b), "%u", pv[i]);
    g_aPreset[i] = mkBtn(c3, 10 + i * 52, 58, 48, 14, b, &lv_font_montserrat_10, PCOL(panel2), PCOL(dim), 0, presetCb, pv[i]);
  }
  lv_obj_t *c4 = mkCard(g_aPanel[1], 244, 5, 228, 82, S(S_A_MODE_TITLE), PCOL(red));
  g_aMode[0] = mkBtn(c4, 10, 24, 100, 30, S(S_A_PER_GEAR), fSm(), PCOL(panel2), PCOL(dim), 0, modeCb, 0);
  g_aMode[1] = mkBtn(c4, 118, 24, 100, 30, S(S_A_RPM_ONLY), fSm(), PCOL(panel2), PCOL(dim), 0, modeCb, 1);
  mkLblClip(c4, S(S_A_ONLY_NOTE), fSm(), PCOL(dim), 10, 54, 210);

  g_aGearBox = mkBox(s, 0, 25 + 93, SW, 112, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aGearBox, LV_OPA_TRANSP, 0);
  lv_obj_t *gp = mkBox(g_aGearBox, 8, 0, 464, 112, PCOL(panel), 12, PCOL(border), 1);
  mkBox(gp, 10, 8, 3, 12, PCOL(red), 2);
  mkLbl(gp, S(S_A_GEAR_WARN), fSm(), PCOL(text), 18, 3, 300);
  for (int i = 0; i < GEAR_COUNT; i++) {
    const int x = 8 + (i % 3) * 152, y = 26 + (i / 3) * 42;
    mkBox(gp, x, y, 144, 38, PCOL(panel2), 10);
    char b[24]; snprintf(b, sizeof(b), "%s %d", S(S_A_GEAR_N), i + 1);
    mkLbl(gp, b, fSm(), PCOL(dim), x + 6, y - 1, 100);
    mkBtn(gp, x + 4, y + 14, 24, 20, "-", fLatin(18), PCOL(panel), PCOL(text), 0, stepCb, (AP_G1 + i) * 2);
    mkBtn(gp, x + 116, y + 14, 24, 20, "+", fLatin(18), PCOL(panel), PCOL(text), 0, stepCb, (AP_G1 + i) * 2 + 1);
    g_aGear[i] = mkLbl(gp, "--", fLatin(14), PCOL(red), x + 30, y + 17, 84, LV_TEXT_ALIGN_CENTER);
  }
  g_aSingleBox = mkBox(s, 0, 25 + 93, SW, 112, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aSingleBox, LV_OPA_TRANSP, 0);
  lv_obj_t *sp = mkBox(g_aSingleBox, 8, 0, 464, 112, PCOL(panel), 12, PCOL(border), 1);
  mkBox(sp, 10, 8, 3, 12, PCOL(red), 2);
  mkLbl(sp, S(S_A_ALL_WARN), fSm(), PCOL(text), 18, 3, 300);
  mkStepper(sp, 110, 32, 244, AP_SINGLE, &g_aSingle, PCOL(red));
  mkLbl(sp, S(S_A_ALL_NOTE), fSm(), PCOL(dim), 8, 76, 448, LV_TEXT_ALIGN_CENTER);
  // the two boxes sit on top of tab 1 only
  lv_obj_set_parent(g_aGearBox, g_aPanel[1]);   lv_obj_set_pos(g_aGearBox, 0, 93);
  lv_obj_set_parent(g_aSingleBox, g_aPanel[1]); lv_obj_set_pos(g_aSingleBox, 0, 93);

  // ---- tab 2: LED shift lights (2 x 8 WS2812B on one pin)
  g_aPanel[2] = mkBox(s, 0, 25, SW, 210, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aPanel[2], LV_OPA_TRANSP, 0);
  lv_obj_t *l1 = mkCard(g_aPanel[2], 8, 5, 228, 200, S(S_LED_TITLE), PCOL(lime));
  g_aLedBtn[0] = mkBtn(l1, 10, 24, 206, 26, "", fSm(), PCOL(panel), PCOL(lime), PCOL(lime), ledToggleCb, 0);
  mkLbl(l1, S(S_LED_BRIGHT), fSm(), PCOL(dim), 12, 52 + TOFF);
  mkStepper(l1, 10, 64, 208, AP_LEDBR, &g_aLedBr, PCOL(lime));
  mkLbl(l1, S(S_LED_START), fSm(), PCOL(dim), 12, 100 + TOFF);
  mkStepper(l1, 10, 112, 208, AP_LEDST, &g_aLedSt, PCOL(lime));
  g_aLedBtn[1] = mkBtn(l1, 10, 152, 100, 26, "", fSm(), PCOL(panel), PCOL(lime), PCOL(lime), ledToggleCb, 1);
  g_aLedBtn[2] = mkBtn(l1, 116, 152, 100, 26, "", fSm(), PCOL(panel), PCOL(lime), PCOL(lime), ledToggleCb, 2);

  lv_obj_t *l2 = mkCard(g_aPanel[2], 244, 5, 228, 200, S(S_LED_COLORS), PCOL(mint));
  for (int i = 0; i < LED_PER_SIDE; i++) {
    g_aLedSw[i] = mkBox(l2, 10 + i * 29, 24, 25, 26, PCOL(panel2), 5, PCOL(border), 2);
    lv_obj_add_flag(g_aLedSw[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_aLedSw[i], clickSoundCb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_aLedSw[i], ledSelCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  for (int i = 0; i < 12; i++) {
    lv_obj_t *sw = mkBox(l2, 10 + (i % 6) * 35, 58 + (i / 6) * 28, 31, 24, LED_PALETTE[i] ? LED_PALETTE[i] : PCOL(panel2), 5, PCOL(border), 1);
    lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sw, clickSoundCb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(sw, ledPalCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  mkLblClip(l2, S(S_LED_HINT), fSm(), PCOL(dim), 12, 116 + TOFF, 206);
  mkBtn(l2, 10, 138, 100, 26, S(S_LED_GRAD), fSm(), PCOL(panel), PCOL(mint), PCOL(mint), ledGradCb, 0);
  mkBtn(l2, 116, 138, 100, 26, S(S_LED_TEST), fSm(), PCOL(panel), PCOL(amber), PCOL(amber), ledTestCb, 0);
  { char pb[56];
    if (led_pin_set()) snprintf(pb, sizeof(pb), S(S_LED_PIN), (int)WS2812_PIN); else snprintf(pb, sizeof(pb), "%s", S(S_LED_PIN_NONE));
    mkLblClip(l2, pb, fSm(), led_pin_set() ? PCOL(dim) : PCOL(amber), 12, 170 + TOFF, 206); }

  // ---- tab 3: clock (no RTC: the time runs from the moment it is set)
  g_aPanel[3] = mkBox(s, 0, 25, SW, 210, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aPanel[3], LV_OPA_TRANSP, 0);
  lv_obj_t *c5 = mkCard(g_aPanel[3], 8, 5, 464, 150, S(S_A_CLK_TITLE), PCOL(cyan));
  mkLbl(c5, S(S_A_HOUR), fSm(), PCOL(dim), 14, 30, 120);
  mkStepper(c5, 14, 54, 200, AP_HOUR, &g_aHour, PCOL(cyan));
  mkLbl(c5, S(S_A_MIN), fSm(), PCOL(dim), 246, 30, 120);
  mkStepper(c5, 246, 54, 200, AP_MIN, &g_aMinute, PCOL(cyan));
  mkLbl(c5, S(S_A_CLK_NOTE), fSm(), PCOL(dim), 14, 100, 440);

  // ---- tab 4: fuel economy (injector size)
  g_aPanel[4] = mkBox(s, 0, 25, SW, 210, PCOL(bg), 0);
  lv_obj_set_style_bg_opa(g_aPanel[4], LV_OPA_TRANSP, 0);
  lv_obj_t *f1 = mkCard(g_aPanel[4], 8, 5, 464, 150, S(S_A_INJ_TITLE), PCOL(yellow));
  mkLbl(f1, S(S_A_INJ_CC), fSm(), PCOL(dim), 14, 30 + TOFF, 200);
  mkStepper(f1, 14, 50, 210, AP_INJ, &g_aInj, PCOL(yellow));
  mkLbl(f1, S(S_A_CYL), fSm(), PCOL(dim), 246, 30 + TOFF, 200);
  mkStepper(f1, 246, 50, 210, AP_CYL, &g_aCyl, PCOL(yellow));
  mkLbl(f1, S(S_A_INJ_NOTE), fSm(), PCOL(dim), 14, 100 + TOFF, 440);

  // ---- bottom bar (all tabs)
  mkBtn(s, 8, 240, 110, 24, S(S_A_SAVE), fMd(), PCOL(mint), PCOL(bg), 0, adminSaveCb, 0);
  mkBtn(s, 124, 240, 110, 24, S(S_A_EXIT), fMd(), PCOL(panel2), PCOL(red), PCOL(red), adminExitCb, 0);
  mkBtn(s, 300, 240, 172, 24, S(S_A_TEST), fSm(), PCOL(panel2), PCOL(amber), PCOL(amber), testFlashCb, 0);

  adminTabPaint();
  adminRefresh();
  lv_scr_load(s);
  if (old) lv_obj_del_async(old);
  lv_obj_t *oldPin = g_scrPin; g_scrPin = nullptr;
  if (oldPin) lv_obj_del_async(oldPin);
}

static void adminUpdate() {
  static uint32_t t = 0;
  if (millis() - t < 250) return;
  t = millis();
  if (g_aLive) { char b[16]; snprintf(b, sizeof(b), "%.1f", g_live.speed); setTxt(g_aLive, b); }
}

// ============================================================ build everything / update
static void uiShowPage(int page) {
  if (page < 0 || page >= N_PAGES) page = 0;
  if (g_page == 4 && page != 4) fuelLeave();             // store the learned map when the page is left
  g_page = (uint8_t)page;
  lv_scr_load(g_scr[page]);
  topBarUpdate();
  bottomBarUpdate();
  if (page == 3) g_dtcRedraw = true;
  if (page == 4) g_fuForce = true;
}

static void uiBuildAll() {
  g_pal = g_set.dark ? &PAL_DARK : &PAL_LIGHT;
  lv_obj_t *old[N_PAGES];
  for (int i = 0; i < N_PAGES; i++) old[i] = g_scr[i];
  lv_obj_t *oldSel = g_scrSelect; g_scrSelect = nullptr;

  lv_obj_clean(lv_layer_top());                     // popup menu, number pad, toast, flash overlay
  g_cardCount = 0;
  memset(g_top, 0, sizeof(g_top));
  memset(&g_bot, 0, sizeof(g_bot));
  g_tickCount = 0;

  buildMain();
  buildData();
  buildChart();
  buildDtc();
  buildFuel();
  lv_obj_t *top = lv_layer_top();
  buildMenu(top);
  buildNumModal(top);
  buildToast(top);
  buildFlash(top);

  if (g_set.brand == BRAND_NONE) { buildSelect(); lv_scr_load(g_scrSelect); }
  else lv_scr_load(g_scr[g_page]);

  for (int i = 0; i < N_PAGES; i++) if (old[i]) lv_obj_del_async(old[i]);
  if (oldSel) lv_obj_del_async(oldSel);
  if (g_dtcState == DTC_DONE) g_dtcFresh = true;
  g_dtcRedraw = true;
  topBarUpdate();
  bottomBarUpdate();
  if (g_reopenMenu) { g_reopenMenu = false; menuOpen(); }
}

static void uiUpdate() {
  static uint32_t tTop = 0, tBot = 0, tChart = 0, tDtc = 0;
  const uint32_t now = millis();
  if (now - tTop >= 100)   { tTop = now;   topBarUpdate(); }
  if (now - tBot >= 50)    { tBot = now;   bottomBarUpdate(); }
  if (now - tChart >= 250) { tChart = now; chartPush(); }
  if (now - tDtc >= 50)    { tDtc = now;   dtcUpdate(); }
  if (g_adminActive) { adminUpdate(); return; }
  if (g_set.brand == BRAND_NONE) return;

  // the visible page is refreshed on every call (change detection keeps it cheap)
  if (g_page == 0) mainUpdate();
  else if (g_page == 1) dataUpdate();
  else if (g_page == 4) fuelUpdate();
}
