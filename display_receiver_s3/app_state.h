/*
  app_state.h - shared state used by the UI and the main sketch (single translation unit).
*/
#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "app_logic.h"
#include "fuel_map.h"
#include "shared_data_struct.h"

struct Live {
  uint16_t rpm;
  float    speedRaw;      // speed before calibration
  float    speed;         // calibrated speed (what is shown and counted)
  bool     hasSpeed;
  int8_t   gear;          // 0 = N, 1..9, -1 = unknown
  float    tps, tpsVolt;
  int16_t  tempC;
  float    batteryV, injectorMs, ignitionDeg, o2Volt, afr;
  bool     ecuConnected;
  uint8_t  ecuType;
  uint8_t  cpuLoad;
  int8_t   cpuTemp;
};

enum { LINK_NONE = 0, LINK_ESPNOW = 1, LINK_KLINE = 2 };
enum { CONN_OFF = 0, CONN_BUSY = 1, CONN_ON = 2 };

static Settings g_set;
static Live     g_live;
static uint8_t  g_link = LINK_NONE;
static uint8_t  g_conn = CONN_OFF;
static char     g_ecmId[32] = "--";

// DTC
enum DtcUi { DTC_IDLE = 0, DTC_READING, DTC_CLEARING, DTC_BUSY, DTC_DONE, DTC_ENGINE_ON, DTC_TIMEOUT };
static DtcDataPacket g_dtc;
static bool          g_dtcFresh = false;      // a new list has to be drawn
static DtcUi         g_dtcState = DTC_IDLE;
static uint32_t      g_dtcT0 = 0;

// requests from the UI to the main loop (never rebuild widgets inside an event callback)
static volatile bool g_rebuildUi   = false;
static bool          g_reopenMenu  = false;
static uint32_t      g_testFlashUntil = 0;
static uint32_t      g_ledTestUntil = 0;      // admin: run the LED test sweep until this time
static bool          g_ledPreview = false;    // admin LED tab open: show all LEDs steady so colours can be judged
static uint8_t       g_page = 0;               // 0 main, 1 data, 2 chart, 3 dtc, 4 fuel map

// fuel map (self-learning AFR maps) - filled in updateData(), shown on page 4
static FuelMap       g_fuel;
static bool          g_fuelDirty = false;       // learned data / edits not stored in flash yet

// implemented in the .ino
void settingsSave();
void fuelSave();
void fuelLoad();
void settingsLoad();
void sendDtcCommand(uint8_t cmd);
void sendEcuSelect();
void onBrandChanged();
