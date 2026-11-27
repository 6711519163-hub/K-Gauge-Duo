/*
  fuel_map.h - self-learning AFR map (pure logic, no Arduino / LVGL: unit-tested on a PC).

  Two 5 x 5 maps are learned at the same time from the live AFR:
     rt : RPM (rows)   x  throttle % (columns)
     sg : speed (rows) x  gear       (columns, 5 and 6 share a column)
  Every cell keeps the average AFR seen there, a target AFR, and the trim % the rider has written down for
  that cell (the trim is only a note / suggestion - the display never changes the ECU).
*/
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#define FM_N        5
#define FM_MIN_N    3              // samples needed before a cell is shown
#define FM_WARM_C   75             // "T>75" switch: learn only with the coolant at or above this

struct FuelCell {
  float    afr;                    // learned average AFR
  float    tgt;                    // target AFR
  uint16_t n;                      // samples
  int8_t   trim;                   // trim % noted by the rider (-50 .. +50)
  uint8_t  _pad;
};

struct FuelMap {
  uint32_t version;                // storage format
  FuelCell rt[FM_N][FM_N];
  FuelCell sg[FM_N][FM_N];
  uint8_t  autoTune;               // 1 = learning on
  uint8_t  warmOnly;               // 1 = learn only when warm
  uint8_t  view;                   // 0 = AFR, 1 = TRIM %
  uint8_t  mode;                   // 0 = rpm x tps, 1 = speed x gear, 2 = advisor
};
#define FM_VERSION 0xF0E10001u

static inline void fuelDefaults(FuelMap &m) {
  memset(&m, 0, sizeof(m));
  m.version = FM_VERSION;
  const float tpsT[FM_N] = { 14.7f, 14.7f, 14.0f, 13.4f, 12.9f };       // richer with more throttle
  const float spdT[FM_N] = { 14.7f, 14.7f, 14.2f, 13.6f, 13.0f };       // richer at higher speed
  for (int r = 0; r < FM_N; r++) for (int c = 0; c < FM_N; c++) {
    m.rt[r][c].tgt = tpsT[c];
    m.sg[r][c].tgt = spdT[r];
  }
  m.autoTune = 1; m.warmOnly = 1; m.view = 0; m.mode = 0;
}

// ---- bins ----------------------------------------------------------------
static inline int fmRpmRow(unsigned rpm) { return rpm < 2500 ? 0 : (rpm < 4500 ? 1 : (rpm < 6500 ? 2 : (rpm < 8500 ? 3 : 4))); }
static inline int fmTpsCol(float tps)    { return tps < 10 ? 0 : (tps < 30 ? 1 : (tps < 50 ? 2 : (tps < 70 ? 3 : 4))); }
static inline int fmSpdRow(float kmh)    { return kmh <= 20 ? 0 : (kmh <= 55 ? 1 : (kmh <= 90 ? 2 : (kmh <= 125 ? 3 : 4))); }
static inline int fmGearCol(int gear)    { return gear < 1 ? -1 : (gear >= 5 ? 4 : gear - 1); }

static inline const char *fmRowLabel(int mode, int r) {
  static const char *rt[FM_N] = { "1.5K", "3.5K", "5.5K", "7.5K", "9.5K+" };
  static const char *sg[FM_N] = { "0-20", "21-55", "56-90", "91-125", "126+" };
  return mode == 1 ? sg[r] : rt[r];
}
static inline const char *fmColLabel(int mode, int c) {
  static const char *rt[FM_N] = { "0%", "20%", "40%", "60%", "80%+" };
  static const char *sg[FM_N] = { "G1", "G2", "G3", "G4", "G5-6" };
  return mode == 1 ? sg[c] : rt[c];
}

// ---- learning ----------------------------------------------------------------
static inline void fmAddSample(FuelCell &c, float afr) {
  if (c.n < 30) { c.n++; c.afr += (afr - c.afr) / (float)c.n; }
  else          { c.afr += (afr - c.afr) * 0.03f; }
}

// Returns true when a sample was stored. afr must come from a valid O2 reading.
static inline bool fuelLearn(FuelMap &m, unsigned rpm, float tps, float kmh, int gear, float afr, int tempC) {
  if (!m.autoTune) return false;
  if (!(afr >= 9.0f && afr <= 19.0f)) return false;
  if (rpm < 1000) return false;
  if (m.warmOnly && tempC < FM_WARM_C) return false;
  fmAddSample(m.rt[fmRpmRow(rpm)][fmTpsCol(tps)], afr);
  const int gc = fmGearCol(gear);
  if (gc >= 0) fmAddSample(m.sg[fmSpdRow(kmh)][gc], afr);
  return true;
}

// ---- classification ------------------------------------------------------------
enum { FS_NONE = 0, FS_RICH = 1, FS_OK = 2, FS_SLEAN = 3, FS_LEAN = 4 };
static inline uint8_t fuelStatusOf(float afr, float tgt) {
  const float d = afr - tgt;
  if (d <= -0.7f) return FS_RICH;
  if (d < 0.35f)  return FS_OK;
  if (d < 0.8f)   return FS_SLEAN;
  return FS_LEAN;
}
static inline uint8_t fuelStatus(const FuelCell &c) { return c.n < FM_MIN_N ? FS_NONE : fuelStatusOf(c.afr, c.tgt); }

// Fuel change still needed to hit the target, in %: + = add fuel (mixture is lean), - = remove fuel
static inline int fuelNeedPct(float afr, float tgt, int trimAlready) {
  if (tgt < 1.0f) return 0;
  return (int)lroundf((afr / tgt - 1.0f) * 100.0f) - trimAlready;
}

// ---- fuel flow / economy -----------------------------------------------------------
// 4-stroke: every injector fires once per two crankshaft turns.
static inline float fuelLitresPerHour(float injMs, unsigned rpm, unsigned injCcMin, unsigned cyl) {
  if (injCcMin == 0 || !(injMs > 0.0f) || rpm == 0) return 0.0f;
  const float ccPerSec = (float)injCcMin / 60.0f;
  const float ccPerShot = ccPerSec * injMs / 1000.0f;
  const float shotsPerSec = (float)rpm / 120.0f;
  return ccPerShot * shotsPerSec * (float)cyl * 3.6f;                  // cc/s -> L/h
}
