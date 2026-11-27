/*
  app_logic.h - pure logic (no Arduino / LVGL calls) so it can be unit-tested on a PC.
  Odometer + trips, per-gear over-rev limit, PIN entry, 6 s hold tracker, speed calibration,
  value alert levels.
*/
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#define GEAR_COUNT       6
#define LED_PER_SIDE     7          // WS2812B shift lights: 7 per side, 2 sides daisy-chained on ONE data pin
#define LED_FILL_FROM_LAST 1        // 1 = the lights run starting from the LAST led of each side
#define LED_TOTAL        (LED_PER_SIDE * 2)
#define ADMIN_PIN        "202030"
#define ADMIN_PIN_LEN    6
#define ADMIN_HOLD_MS    6000
#define ADMIN_IDLE_MS    60000
#define ODO_SAVE_STEP_KM 0.5f

#define BRAND_NONE   0
#define BRAND_HONDA  1
#define BRAND_YAMAHA 2

struct Settings {
  uint8_t  lang;          // 0 = English, 1 = Thai
  uint8_t  dark;          // 1 = dark theme
  uint8_t  sport;         // 1 = sport mode: shift badge + over-rev alarm enabled
  uint8_t  brand;         // BRAND_*
  float    odoKm;
  float    tripA;
  float    tripB;
  float    speedCalPct;   // 100.0 = no correction
  uint16_t maxRpm;        // gauge full scale
  uint8_t  shiftMode;     // 0 = per gear, 1 = single value (gear ignored)
  uint16_t shiftSingle;
  uint16_t shiftGear[GEAR_COUNT];
  // ---- LED shift lights (admin mode)
  uint8_t  ledOn;                       // 1 = enabled
  uint8_t  ledBright;                   // 5 .. 100 %
  uint8_t  ledStart;                    // 40 .. 95 : % of the warning rpm where the first LED lights
  uint8_t  ledFlash;                    // 1 = all LEDs blink once the warning rpm is reached
  uint8_t  ledRev;                      // 1 = right side is wired in the opposite direction
  uint32_t ledColor[LED_PER_SIDE];      // 0xRRGGBB of step 1..7 in the order they light up (same on both sides)
  // ---- fuel economy (km/L): injector size, used only for the economy figure
  uint16_t injCc;                       // injector flow cc/min, 0 = economy hidden
  uint8_t  cyl;                         // number of injectors / cylinders 1..4
};

static inline void settingsDefaults(Settings &s) {
  s.lang = 0; s.dark = 1; s.sport = 1; s.brand = BRAND_NONE;
  s.odoKm = 0; s.tripA = 0; s.tripB = 0;
  s.speedCalPct = 100.0f;
  s.maxRpm = 10000;
  s.shiftMode = 1;
  s.shiftSingle = 9000;
  const uint16_t g[GEAR_COUNT] = { 6000, 7500, 8500, 9000, 9500, 9800 };
  for (int i = 0; i < GEAR_COUNT; i++) s.shiftGear[i] = g[i];
  s.ledOn = 1; s.ledBright = 30; s.ledStart = 70; s.ledFlash = 1; s.ledRev = 0;
  const uint32_t c[LED_PER_SIDE] = { 0x00FF30, 0x9BFF00, 0xFFE000, 0xFFA000, 0xFF6A00, 0xFF0000, 0xFF0000 };
  for (int i = 0; i < LED_PER_SIDE; i++) s.ledColor[i] = c[i];
  s.injCc = 0; s.cyl = 1;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

// Make sure values read back from flash can never break the UI
static inline void settingsClamp(Settings &s) {
  if (s.lang > 1) s.lang = 0;
  s.dark = s.dark ? 1 : 0;
  s.sport = s.sport ? 1 : 0;
  if (s.brand > BRAND_YAMAHA) s.brand = BRAND_NONE;
  if (!(s.odoKm >= 0 && s.odoKm < 1000000.0f)) s.odoKm = 0;
  if (!(s.tripA >= 0 && s.tripA < 100000.0f))  s.tripA = 0;
  if (!(s.tripB >= 0 && s.tripB < 100000.0f))  s.tripB = 0;
  if (!(s.speedCalPct >= 50.0f && s.speedCalPct <= 150.0f)) s.speedCalPct = 100.0f;
  s.maxRpm = (uint16_t)clampi(s.maxRpm, 6000, 16000);
  s.shiftMode = s.shiftMode ? 1 : 0;
  s.shiftSingle = (uint16_t)clampi(s.shiftSingle, 3000, s.maxRpm);
  for (int i = 0; i < GEAR_COUNT; i++) s.shiftGear[i] = (uint16_t)clampi(s.shiftGear[i], 3000, s.maxRpm);
  s.ledOn = s.ledOn ? 1 : 0;  s.ledFlash = s.ledFlash ? 1 : 0;  s.ledRev = s.ledRev ? 1 : 0;
  s.ledBright = (uint8_t)clampi(s.ledBright, 5, 100);
  s.ledStart  = (uint8_t)clampi(s.ledStart, 40, 95);
  for (int i = 0; i < LED_PER_SIDE; i++) s.ledColor[i] &= 0xFFFFFF;
  if (s.injCc > 2000) s.injCc = 0;
  s.cyl = (uint8_t)clampi(s.cyl, 1, 4);
}

// ---------------- over-rev ----------------
// gear: 0 = neutral / 255 = unknown / 1..6 = gear.  In "single" mode (or when the gear is not
// known) the single limit is used, so the warning works from RPM alone.
static inline uint16_t shiftLimit(const Settings &s, uint8_t gear) {
  if (s.shiftMode == 1 || gear == 0 || gear > GEAR_COUNT) return s.shiftSingle;
  return s.shiftGear[gear - 1];
}

struct RevAlarm {
  bool on;
  RevAlarm() : on(false) {}
  // Turns on at rpm >= limit, off again 200 rpm below it (no flicker at the limit)
  bool update(const Settings &s, uint16_t rpm, uint8_t gear) {
    const uint16_t lim = shiftLimit(s, gear);
    if (!on && rpm >= lim) on = true;
    else if (on && rpm + 200 < lim) on = false;
    return on;
  }
};

// ---------------- speed calibration ----------------
static inline float calibratedSpeed(float raw, float pct) { return raw * pct / 100.0f; }

// shown = speed currently displayed (already calibrated), real = true speed from a reference
static inline float newCalPct(float curPct, float shown, float real) {
  if (shown < 5.0f || real < 5.0f) return curPct;      // too slow to calibrate reliably
  return clampf(curPct * real / shown, 50.0f, 150.0f);
}

// ---------------- odometer / trips ----------------
struct OdoTracker {
  uint32_t lastMs;
  bool     haveLast;
  float    unsavedKm;
  OdoTracker() : lastMs(0), haveLast(false), unsavedKm(0) {}
  // Adds distance for the time since the last call. Returns km added.
  float update(Settings &s, float speedKmh, uint32_t nowMs) {
    if (!haveLast) { haveLast = true; lastMs = nowMs; return 0; }
    uint32_t dt = nowMs - lastMs;
    lastMs = nowMs;
    if (dt == 0 || dt > 2000) return 0;                // data gap: do not guess
    if (!(speedKmh > 0.5f) || speedKmh > 300.0f) return 0;
    const float km = speedKmh * (float)dt / 3600000.0f;
    s.odoKm += km; s.tripA += km; s.tripB += km;
    unsavedKm += km;
    return km;
  }
  bool needSave() const { return unsavedKm >= ODO_SAVE_STEP_KM; }
  void saved() { unsavedKm = 0; }
};

// "12,345.6" (no locale, no printf dependency on thousands grouping)
static inline void formatKm(char *out, size_t cap, float km) {
  if (cap < 16) { if (cap) out[0] = 0; return; }
  if (km < 0) km = 0;
  uint32_t tenths = (uint32_t)(km * 10.0f + 0.5f);
  uint32_t whole = tenths / 10, frac = tenths % 10;
  char tmp[16]; int n = 0;
  if (whole == 0) tmp[n++] = '0';
  int digits = 0;
  while (whole > 0 && n < 14) {
    if (digits && digits % 3 == 0) tmp[n++] = ',';
    tmp[n++] = (char)('0' + whole % 10);
    whole /= 10; digits++;
  }
  size_t k = 0;
  while (n > 0) out[k++] = tmp[--n];
  out[k++] = '.'; out[k++] = (char)('0' + frac); out[k] = 0;
}

// ---------------- admin PIN ----------------
struct PinEntry {
  char    buf[ADMIN_PIN_LEN + 1];
  uint8_t n;
  PinEntry() { clear(); }
  void clear() { memset(buf, 0, sizeof(buf)); n = 0; }
  bool push(char d) {
    if (d < '0' || d > '9' || n >= ADMIN_PIN_LEN) return false;
    buf[n++] = d; buf[n] = 0; return true;
  }
  void backspace() { if (n) { buf[--n] = 0; } }
  bool check() const { return n == ADMIN_PIN_LEN && strcmp(buf, ADMIN_PIN) == 0; }
};

// ---------------- long-press (6 s) ----------------
struct HoldTracker {
  bool     active;
  uint32_t t0;
  HoldTracker() : active(false), t0(0) {}
  void start(uint32_t now) { active = true; t0 = now; }
  void stop() { active = false; }
  float progress(uint32_t now, uint32_t total) const {
    if (!active) return 0;
    const uint32_t e = now - t0;
    return e >= total ? 1.0f : (float)e / (float)total;
  }
  bool done(uint32_t now, uint32_t total) const { return active && (now - t0) >= total; }
};

// ---------------- alert levels: 0 normal, 1 warning, 2 danger, 3 low/cold ----------------
static inline uint8_t levelSpeed(float v)   { return v >= 110 ? 2 : (v >= 90 ? 1 : 0); }
static inline uint8_t levelTemp(float t)    { return t >= 105 ? 2 : (t >= 95 ? 1 : (t < 50 ? 3 : 0)); }
static inline uint8_t levelBatt(float v)    { return (v < 11.5f || v >= 15.2f) ? 2 : ((v < 12.0f || v >= 14.8f) ? 1 : 0); }
static inline uint8_t levelAfr(float a)     { return a >= 16.5f ? 2 : ((a >= 15.5f || a < 11.5f) ? 1 : 0); }
static inline uint8_t levelInj(float ms)    { return ms >= 10.0f ? 1 : 0; }
static inline uint8_t levelIgn(float deg)   { return (deg > 45.0f || deg < 0.0f) ? 1 : 0; }

// RPM bar / arc colour zone from the fraction of full scale: 0 green, 1 amber, 2 red
static inline uint8_t rpmZone(uint16_t rpm, uint16_t maxRpm) {
  const float f = maxRpm ? (float)rpm / (float)maxRpm : 0;
  return f < 0.60f ? 0 : (f < 0.90f ? 1 : 2);
}

// Gear from packet: 0 = N, 1..6, anything else = unknown
static inline int8_t gearValue(uint8_t g) { return (g == 0) ? 0 : ((g <= 9) ? (int8_t)g : -1); }

// ---------------- LED shift lights ----------------
// How many LEDs (per side, 0..LED_PER_SIDE) are lit: none below ledStart % of the warning rpm, all 8 at the warning rpm.
static inline uint8_t ledLitCount(const Settings &s, uint16_t rpm, uint16_t lim) {
  if (rpm >= lim) return LED_PER_SIDE;
  const float start = (float)lim * (float)s.ledStart / 100.0f;
  if ((float)rpm <= start || lim <= start) return 0;
  const float f = ((float)rpm - start) / ((float)lim - start);              // 0 .. 1
  int n = (int)ceilf(f * LED_PER_SIDE);
  return (uint8_t)clampi(n, 1, LED_PER_SIDE);
}

// Builds the LED frame (LED_TOTAL = 14) (R,G,B bytes per LED). Chain order: 0..6 = left side, 7..13 = right side.
// Both sides show the same pattern; with ledRev the right side is filled in the opposite direction
// (use it when the second strip was wired the other way round).
static inline void ledFrame(const Settings &s, uint8_t lit, uint8_t *rgb) {
  memset(rgb, 0, LED_TOTAL * 3);
  const uint32_t sc = (uint32_t)s.ledBright * 255u / 100u;                  // brightness 0..255
  for (int k = 0; k < LED_PER_SIDE && k < lit; k++) {
    const uint32_t c = s.ledColor[k];
    const uint8_t r = (uint8_t)((((c >> 16) & 0xFF) * sc) / 255u);
    const uint8_t g = (uint8_t)((((c >> 8) & 0xFF) * sc) / 255u);
    const uint8_t b = (uint8_t)(((c & 0xFF) * sc) / 255u);
    const int p = LED_FILL_FROM_LAST ? (LED_PER_SIDE - 1 - k) : k;          // physical position of step k on one side
    const int left = p, right = LED_PER_SIDE + (s.ledRev ? (LED_PER_SIDE - 1 - p) : p);
    rgb[left * 3] = r;  rgb[left * 3 + 1] = g;  rgb[left * 3 + 2] = b;
    rgb[right * 3] = r; rgb[right * 3 + 1] = g; rgb[right * 3 + 2] = b;
  }
}
