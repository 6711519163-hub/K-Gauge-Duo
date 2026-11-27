/*
  display_receiver_s3.ino - SPORT DASHBOARD (Honda / Yamaha K-Line) *** LVGL 8.3 ***
  Zero-Latency Edition:
   - Instant Event-Driven Wakeup (xTaskNotifyGive on ESP-NOW packet arrival)
   - Zero Mutex Contention (updateData runs directly inside TaskLVGL)
   - 8ms LVGL Internal Refresh Timer + 40-line SRAM DMA Double Buffering
*/
#pragma GCC optimize ("O2")
#pragma GCC optimize ("fast-math")

#define WS2812_PIN  18

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Touch_GT911.h"
#include "shared_data_struct.h"
#include "app_state.h"
#include "driver_i2s.h"
#include "driver_led.h"
#include "driver_kline.h"
#include "ui_core.h"
#include "ui_screens.h"
#include "ui_admin.h"

#define BLACK 0x0000

#define TOUCH_SDA 8
#define TOUCH_SCL 4
#define TOUCH_INT 3
#define TOUCH_RST 38

#define LCD_BL 1
#define LEDC_BASE_FREQ    5000
#define LEDC_TIMER_12_BIT 12

Touch_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 480, 272);
Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39, 80000000);
Arduino_GFX *gfx = new Arduino_NV3041A(bus, GFX_NOT_DEFINED, 1, true);

portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t xGuiMutex = NULL;
static TaskHandle_t g_lvglTaskHandle = NULL;

// ============================================================ settings in flash
static Preferences g_prefs;

void settingsLoad() {
  Settings s;
  settingsDefaults(s);
  g_prefs.begin("cfg", true);
  s.lang        = g_prefs.getUChar("lang", s.lang);
  s.dark        = g_prefs.getUChar("dark", s.dark);
  s.sport       = g_prefs.getUChar("sport", s.sport);
  s.brand       = g_prefs.getUChar("brand", s.brand);
  s.ledOn       = g_prefs.getUChar("ledon", s.ledOn);
  s.ledBright   = g_prefs.getUChar("ledbr", s.ledBright);
  s.ledStart    = g_prefs.getUChar("ledst", s.ledStart);
  s.ledFlash    = g_prefs.getUChar("ledfl", s.ledFlash);
  s.ledRev      = g_prefs.getUChar("ledrev", s.ledRev);
  for (int i = 0; i < LED_PER_SIDE; i++) {
    char k[6]; snprintf(k, sizeof(k), "lc%d", i);
    s.ledColor[i] = g_prefs.getUInt(k, s.ledColor[i]);
  }
  s.injCc       = g_prefs.getUShort("injcc", s.injCc);
  s.cyl         = g_prefs.getUChar("cyl", s.cyl);
  s.odoKm       = g_prefs.getFloat("odo", s.odoKm);
  s.tripA       = g_prefs.getFloat("tripA", s.tripA);
  s.tripB       = g_prefs.getFloat("tripB", s.tripB);
  s.speedCalPct = g_prefs.getFloat("cal", s.speedCalPct);
  s.maxRpm      = g_prefs.getUShort("maxrpm", s.maxRpm);
  s.shiftMode   = g_prefs.getUChar("smode", s.shiftMode);
  s.shiftSingle = g_prefs.getUShort("ssingle", s.shiftSingle);
  for (int i = 0; i < GEAR_COUNT; i++) {
    char k[6]; snprintf(k, sizeof(k), "g%d", i);
    s.shiftGear[i] = g_prefs.getUShort(k, s.shiftGear[i]);
  }
  g_prefs.end();
  settingsClamp(s);
  g_set = s;
}

void settingsSave() {
  g_prefs.begin("cfg", false);
  g_prefs.putUChar("lang", g_set.lang);
  g_prefs.putUChar("dark", g_set.dark);
  g_prefs.putUChar("sport", g_set.sport);
  g_prefs.putUChar("brand", g_set.brand);
  g_prefs.putUChar("ledon", g_set.ledOn);
  g_prefs.putUChar("ledbr", g_set.ledBright);
  g_prefs.putUChar("ledst", g_set.ledStart);
  g_prefs.putUChar("ledfl", g_set.ledFlash);
  g_prefs.putUChar("ledrev", g_set.ledRev);
  for (int i = 0; i < LED_PER_SIDE; i++) {
    char k[6]; snprintf(k, sizeof(k), "lc%d", i);
    g_prefs.putUInt(k, g_set.ledColor[i]);
  }
  g_prefs.putUShort("injcc", g_set.injCc);
  g_prefs.putUChar("cyl", g_set.cyl);
  g_prefs.putFloat("odo", g_set.odoKm);
  g_prefs.putFloat("tripA", g_set.tripA);
  g_prefs.putFloat("tripB", g_set.tripB);
  g_prefs.putFloat("cal", g_set.speedCalPct);
  g_prefs.putUShort("maxrpm", g_set.maxRpm);
  g_prefs.putUChar("smode", g_set.shiftMode);
  g_prefs.putUShort("ssingle", g_set.shiftSingle);
  for (int i = 0; i < GEAR_COUNT; i++) {
    char k[6]; snprintf(k, sizeof(k), "g%d", i);
    g_prefs.putUShort(k, g_set.shiftGear[i]);
  }
  g_prefs.end();
}

// ---- learned fuel map (separate namespace, one 608-byte blob)
void fuelLoad() {
  FuelMap m;
  fuelDefaults(m);
  g_prefs.begin("fuel", true);
  if (g_prefs.getBytesLength("map") == sizeof(FuelMap)) {
    FuelMap t;
    g_prefs.getBytes("map", &t, sizeof(t));
    if (t.version == FM_VERSION) m = t;
  }
  g_prefs.end();
  // never trust flash: reset any cell that holds nonsense
  FuelCell *cells[2] = { &m.rt[0][0], &m.sg[0][0] };
  FuelMap d; fuelDefaults(d);
  const FuelCell *dc[2] = { &d.rt[0][0], &d.sg[0][0] };
  for (int k = 0; k < 2; k++)
    for (int i = 0; i < FM_N * FM_N; i++) {
      FuelCell &c = cells[k][i];
      if (!(c.afr >= 0.0f && c.afr < 30.0f) || !(c.tgt >= 9.0f && c.tgt <= 19.0f) || c.n > 60000 || c.trim < -50 || c.trim > 50) c = dc[k][i];
    }
  if (m.mode > 2) m.mode = 0;
  m.view = m.view ? 1 : 0; m.autoTune = m.autoTune ? 1 : 0; m.warmOnly = m.warmOnly ? 1 : 0;
  g_fuel = m;
}

void fuelSave() {
  g_prefs.begin("fuel", false);
  g_prefs.putBytes("map", &g_fuel, sizeof(FuelMap));
  g_prefs.end();
}

static void odoSave() {
  g_prefs.begin("cfg", false);
  g_prefs.putFloat("odo", g_set.odoKm);
  g_prefs.putFloat("tripA", g_set.tripA);
  g_prefs.putFloat("tripB", g_set.tripB);
  g_prefs.end();
}

// ============================================================ backlight
void setBrightness(uint8_t value) {
  uint32_t duty = 4095 * value / 255;
  ledcWrite(LCD_BL, duty);
}

// ============================================================ LVGL 8.3 glue
static lv_disp_draw_buf_t s_drawBufDesc;
static lv_disp_drv_t      s_dispDrv;
static lv_indev_drv_t     s_indevDrv;

#define SHOW_FPS 0
static volatile uint32_t s_fpsFrames = 0;
static volatile uint32_t s_flushUs = 0, s_flushPx = 0;
static volatile uint32_t s_rxPkts = 0, s_rxMaxGap = 0, s_rxLastMs = 0;

static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
  const uint32_t t0 = micros();
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
  s_flushUs += micros() - t0; s_flushPx += w * h;
  if (lv_disp_flush_is_last(drv)) s_fpsFrames++;
  lv_disp_flush_ready(drv);
}

static void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  (void)drv;
  touch.read();
  if (touch.isTouched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touch.points[0].x;
    data->point.y = touch.points[0].y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

#if SHOW_FPS
static void fpsPrint() {
  static uint32_t t0 = 0;
  const uint32_t now = millis();
  if (now - t0 < 1000) return;
  Serial.printf("[FPS] %u | [RX] %u pkt/s, longest gap %u ms | flush %u ms, %u kpx | heap free %u\n",
                (unsigned)s_fpsFrames, (unsigned)s_rxPkts, (unsigned)s_rxMaxGap,
                (unsigned)(s_flushUs / 1000), (unsigned)(s_flushPx / 1000), (unsigned)ESP.getFreeHeap());
  s_fpsFrames = 0; s_rxPkts = 0; s_rxMaxGap = 0; s_flushUs = 0; s_flushPx = 0; t0 = now;
}
#endif

void lvglInit() {
  ledcAttach(LCD_BL, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
  setBrightness(255);

  if (!gfx->begin()) Serial.println("gfx->begin() failed!");
  gfx->setRotation(2);
  gfx->fillScreen(BLACK);

  touch.begin(GT911_ADDR1);
  Wire.setClock(800000);

  lv_init();

  // Double Buffering with DMA in Internal SRAM (40 Lines each)
  const uint32_t lines = 40;
  const size_t bufSize = 480UL * lines * sizeof(lv_color_t);
  
  lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  lv_color_t *buf2 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

  if (!buf1 || !buf2) {
    if (buf1) heap_caps_free(buf1);
    if (buf2) heap_caps_free(buf2);
    buf1 = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    buf2 = NULL;
  }

  lv_disp_draw_buf_init(&s_drawBufDesc, buf1, buf2, 480UL * lines);

  lv_disp_drv_init(&s_dispDrv);
  s_dispDrv.hor_res  = 480;
  s_dispDrv.ver_res  = 272;
  s_dispDrv.flush_cb = flushCb;
  s_dispDrv.draw_buf = &s_drawBufDesc;
  lv_disp_t *disp = lv_disp_drv_register(&s_dispDrv);
  lv_timer_set_period(disp->refr_timer, 8);               // 8ms (125 Hz) ลดเวลารอรอบวาดของ LVGL

  lv_indev_drv_init(&s_indevDrv);
  s_indevDrv.type = LV_INDEV_TYPE_POINTER;
  s_indevDrv.read_cb = touchReadCb;
  s_indevDrv.long_press_time = 700;
  lv_indev_t *indev = lv_indev_drv_register(&s_indevDrv);
  lv_timer_set_period(indev->driver->read_timer, 20);
}

// ============================================================ ESP-NOW
volatile GaugeDataPacket latestData;
volatile bool            newDataAvailable = false;
volatile uint32_t        lastGaugePacketMs = 0;
volatile DtcDataPacket   latestDtcData;
volatile bool            newDtcAvailable = false;
volatile EcmIdPacket     latestEcmIdData;
volatile bool            newEcmIdAvailable = false;

uint8_t  espnowBroadcastMac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint32_t heartbeatSeq = 0;
uint32_t lastHeartbeatSentMs = 0;

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void onEspNowRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
#else
void onEspNowRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
  if (len == sizeof(GaugeDataPacket)) {
    GaugeDataPacket d;
    memcpy(&d, incomingData, sizeof(d));
    if (d.packetType == PKT_TYPE_GAUGE_DATA) {
      const uint32_t nowMs = millis();
      portENTER_CRITICAL_ISR(&dataMux);
      memcpy((void *)&latestData, &d, sizeof(d));
      newDataAvailable = true;
      lastGaugePacketMs = nowMs;
      portEXIT_CRITICAL_ISR(&dataMux);

      // ปลุก TaskLVGL ให้ตื่นขึ้นมาวาดจอทันทีที่ข้อมูลมาถึง (< 0.05 ms)
      if (g_lvglTaskHandle != NULL) xTaskNotifyGive(g_lvglTaskHandle);

#if SHOW_FPS
      if (s_rxLastMs) { const uint32_t g = nowMs - s_rxLastMs; if (g > s_rxMaxGap) s_rxMaxGap = g; }
      s_rxLastMs = nowMs; s_rxPkts++;
#endif
    }
  } else if (len == sizeof(DtcDataPacket)) {
    DtcDataPacket d;
    memcpy(&d, incomingData, sizeof(d));
    if (d.packetType == PKT_TYPE_DTC_DATA) {
      portENTER_CRITICAL_ISR(&dataMux);
      memcpy((void *)&latestDtcData, &d, sizeof(d));
      newDtcAvailable = true;
      portEXIT_CRITICAL_ISR(&dataMux);
      if (g_lvglTaskHandle != NULL) xTaskNotifyGive(g_lvglTaskHandle);
    }
  } else if (len == sizeof(EcmIdPacket)) {
    EcmIdPacket d;
    memcpy(&d, incomingData, sizeof(d));
    if (d.packetType == PKT_TYPE_ECM_ID) {
      portENTER_CRITICAL_ISR(&dataMux);
      memcpy((void *)&latestEcmIdData, &d, sizeof(d));
      newEcmIdAvailable = true;
      portEXIT_CRITICAL_ISR(&dataMux);
      if (g_lvglTaskHandle != NULL) xTaskNotifyGive(g_lvglTaskHandle);
    }
  }
}

void espNowSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, espnowBroadcastMac, 6);
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void sendHeartbeatIfDue() {
  const uint32_t now = millis();
  if (now - lastHeartbeatSentMs < 1000) return;
  lastHeartbeatSentMs = now;
  HeartbeatPacket hb;
  hb.packetType = PKT_TYPE_HEARTBEAT;
  hb.seq = heartbeatSeq++;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&hb, sizeof(hb));
}

void sendDtcCommand(uint8_t cmd) {
  DtcCommandPacket cmdPkt;
  cmdPkt.packetType = PKT_TYPE_DTC_CMD;
  cmdPkt.cmd = cmd;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&cmdPkt, sizeof(cmdPkt));
}

void sendEcuSelect() {
  EcuSelectPacket p;
  p.packetType = PKT_TYPE_ECU_SELECT;
  p.brand = g_set.brand;
  p.chk   = (uint8_t)(g_set.brand ^ 0xA5);
  p.magic = 0x5A;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&p, sizeof(p));
}

static void sendEcuSelectIfDue() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (g_set.brand == BRAND_NONE || now - last < 2000) return;
  last = now;
  sendEcuSelect();
}

void onBrandChanged() {
  kline_set_brand(g_set.brand);
  sendEcuSelect();
  memset(&g_live, 0, sizeof(g_live));
  g_live.gear = -1;
  g_dtcState = DTC_IDLE;
  g_dtcRedraw = true;
  strcpy(g_ecmId, "--");
}

// ============================================================ live data
static float calcAfr(float o2Volt) {
  return (o2Volt <= 0.45f) ? (18.0f - (o2Volt / 0.45f) * 3.3f)
                           : (14.7f - ((o2Volt - 0.45f) / 0.55f) * 3.7f);
}

static OdoTracker g_odo;
static RevAlarm   g_alarm;

static void updateData() {
  static GaugeDataPacket esp;
  static bool haveEsp = false;
  const uint32_t now = millis();

  if (newDataAvailable) {
    portENTER_CRITICAL(&dataMux);
    memcpy(&esp, (const void *)&latestData, sizeof(esp));
    newDataAvailable = false;
    portEXIT_CRITICAL(&dataMux);
    haveEsp = true;
  }
  if (newEcmIdAvailable) {
    EcmIdPacket e;
    portENTER_CRITICAL(&dataMux);
    memcpy(&e, (const void *)&latestEcmIdData, sizeof(e));
    newEcmIdAvailable = false;
    portEXIT_CRITICAL(&dataMux);
    if (e.valid && e.text[0]) { e.text[sizeof(e.text) - 1] = 0; snprintf(g_ecmId, sizeof(g_ecmId), "%s", e.text); }
  }
  if (newDtcAvailable) {
    portENTER_CRITICAL(&dataMux);
    memcpy(&g_dtc, (const void *)&latestDtcData, sizeof(g_dtc));
    newDtcAvailable = false;
    portEXIT_CRITICAL(&dataMux);
    g_dtcFresh = true;
  }

  KLineSnapshot ks;
  kline_get(&ks);

  const uint8_t want = g_set.brand;
  const bool espAlive = haveEsp && lastGaugePacketMs != 0 && (now - lastGaugePacketMs) < 2000;
  const bool espOk    = espAlive && esp.ecuConnected && (want == BRAND_NONE || esp.ecuType == want);
  const bool klOk     = ks.linkUp && (want == BRAND_NONE || ks.ecuType == want);

  uint8_t src = LINK_NONE;
  if (espOk) src = LINK_ESPNOW; else if (klOk) src = LINK_KLINE;
  g_link = src;
  if (src != LINK_NONE) g_conn = CONN_ON;
  else if ((espAlive && !esp.ecuConnected) || ks.state == KL_CONNECTING) g_conn = CONN_BUSY;
  else g_conn = CONN_OFF;

  Live L;
  memset(&L, 0, sizeof(L));
  L.gear = -1;
  if (src == LINK_ESPNOW) {
    L.ecuType = esp.ecuType; L.rpm = esp.rpm; L.speedRaw = esp.speedKmh; L.hasSpeed = true;
    L.gear = gearValue(esp.gear);
    L.tps = esp.tps; L.tpsVolt = esp.tpsVolt; L.tempC = esp.tempC; L.batteryV = esp.batteryV;
    L.injectorMs = esp.injectorMs; L.ignitionDeg = esp.ignitionDeg; L.o2Volt = esp.o2Volt; L.afr = calcAfr(esp.o2Volt);
    L.ecuConnected = true; L.cpuLoad = esp.senderCpuLoad; L.cpuTemp = esp.senderCpuTemp;
  } else if (src == LINK_KLINE) {
    L.ecuType = ks.ecuType; L.rpm = ks.rpm; L.speedRaw = ks.speedKmh; L.hasSpeed = ks.speedValid;
    L.tps = ks.tps; L.tpsVolt = ks.tpsVolt; L.tempC = ks.tempC; L.batteryV = ks.batteryV;
    L.injectorMs = ks.injectorMs; L.ignitionDeg = ks.ignitionDeg; L.o2Volt = ks.o2Volt; L.afr = calcAfr(ks.o2Volt);
    L.ecuConnected = true;
    if (ks.ecmId[0]) snprintf(g_ecmId, sizeof(g_ecmId), "%s", ks.ecmId);
  } else if (g_conn == CONN_OFF) {
    strcpy(g_ecmId, "--");
  }
  L.speed = calibratedSpeed(L.speedRaw, g_set.speedCalPct);
  g_live = L;

  // ---- fuel map: learn the AFR per cell (Honda only - the O2 value is what the map is built from)
  {
    static uint32_t lastLearn = 0, stopSince = 0;
    if (L.ecuConnected && L.ecuType != ECU_TYPE_YAMAHA && L.o2Volt > 0.02f && (uint32_t)(now - lastLearn) >= 100) {
      lastLearn = now;
      if (fuelLearn(g_fuel, L.rpm, L.tps, L.speed, L.gear, L.afr, L.tempC)) g_fuelDirty = true;
    }
    // store it only while standing still: a flash write freezes the display for a moment
    if (L.speed < 1.0f) { if (!stopSince) stopSince = now; } else stopSince = 0;
    if (g_fuelDirty && stopSince && (uint32_t)(now - stopSince) > 5000) { fuelSave(); g_fuelDirty = false; }
  }

  if (L.ecuConnected && L.hasSpeed) {
    g_odo.update(g_set, L.speed, now);
    if (g_odo.needSave()) { odoSave(); g_odo.saved(); }
  } else {
    g_odo.haveLast = false;
  }

  const uint8_t gearArg = L.gear < 0 ? 255 : (uint8_t)L.gear;
  const bool overRaw = L.ecuConnected && g_alarm.update(g_set, L.rpm, gearArg);
  const bool over = overRaw && g_set.sport;
  const bool test = (int32_t)(g_testFlashUntil - now) > 0;

  flashSet(over || test, over ? L.rpm : shiftLimit(g_set, gearArg));
  audio_set_shift_alarm(over || test);
  audio_set_overheat_alarm(L.ecuConnected && L.tempC >= 105);
}

// ============================================================ FreeRTOS Task (Core 1 Dedicated GUI Engine)
void TaskLVGL(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    // รอสูงสุด 8ms หรือตื่นทันทีที่แพ็กเกจ ESP-NOW เข้ามา
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8));

    if (xSemaphoreTake(xGuiMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      updateData(); // ดึงข้อมูลล่าสุดและอัปเดตทันทีโดยไม่ต้องรอแย่ง Mutex กับ loop()
      if (g_rebuildUi) { 
        g_rebuildUi = false; 
        uiBuildAll(); 
      }
      uiUpdate();
      lv_timer_handler();
      xSemaphoreGive(xGuiMutex);
    }
  }
}

// ============================================================ LED shift lights
static void ledService(uint32_t now) {
  static uint32_t t = 0;
  if ((uint32_t)(now - t) < 20) return;
  t = now;
  const Live &L = g_live;
  const uint8_t gearArg = L.gear < 0 ? 255 : (uint8_t)L.gear;
  const uint16_t lim = shiftLimit(g_set, gearArg);
  uint8_t lit = 0;
  if ((int32_t)(g_ledTestUntil - now) > 0) {
    const uint32_t ph = (now / 90) % (2 * LED_PER_SIDE);               // up, then down
    lit = (uint8_t)(ph < LED_PER_SIDE ? ph + 1 : 2 * LED_PER_SIDE - ph);
  } else if (g_ledPreview) {
    lit = LED_PER_SIDE;
  } else if (g_set.ledOn && g_set.sport && L.ecuConnected) {
    lit = ledLitCount(g_set, L.rpm, lim);
    if (lit >= LED_PER_SIDE && g_set.ledFlash && L.rpm >= lim && ((now / 70) & 1) == 0) lit = 0;
  }
  uint8_t frame[LED_TOTAL * 3];
  ledFrame(g_set, lit, frame);
  led_write(frame);
}

// ============================================================ Arduino entry points
void setup() {
  Serial.begin(115200);

  xGuiMutex = xSemaphoreCreateMutex();

  settingsLoad();
  fuelLoad();
  memset(&g_live, 0, sizeof(g_live));
  g_live.gear = -1;

  lvglInit();
  audio_init();
  led_begin();
  
  if (xSemaphoreTake(xGuiMutex, portMAX_DELAY) == pdTRUE) {
    uiBuildAll();
    xSemaphoreGive(xGuiMutex);
  }

  espNowSetup();
  kline_begin();
  kline_set_brand(g_set.brand);
  sendEcuSelect();

  xTaskCreatePinnedToCore(
    TaskLVGL,
    "TaskLVGL",
    8192,
    NULL,
    3,
    &g_lvglTaskHandle,
    1
  );
}

void loop() {
#if SHOW_FPS
  fpsPrint();
#endif
  sendHeartbeatIfDue();
  sendEcuSelectIfDue();
  ledService(millis());
  vTaskDelay(pdMS_TO_TICKS(5));
}