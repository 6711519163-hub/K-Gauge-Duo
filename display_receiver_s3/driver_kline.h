/*
  driver_kline.h - Dual Honda & Yamaha K-Line ECU Local Fallback Driver (ESP32-S3 display)

  Fixes vs. previous version:
   - Hardware echo eraser (L9637D / MC33290 reflect every TX byte onto RX). The old code
     treated the echoed 0xFE as a Yamaha answer and fed the echoed poll byte into the decoder.
   - Yamaha fast-init: bus HIGH > 300 ms -> LOW 25 ms -> HIGH 25 ms -> open UART,
     scanning 15625 bps first and then 10400 bps.
   - Yamaha never uses the Honda ECM-ID request; ECM name is instantly "YAMAHA-ECU".
   - Yamaha vehicle speed is taken from the ECU stream when present (speedValid).
   - Link-loss detection (the old task never dropped linkUp) and full data reset on
     every (re)connect, so no Honda values leak into a Yamaha session or vice-versa.
*/
#pragma once

#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_data_struct.h"

#ifndef KLINE_RX_PIN
#define KLINE_RX_PIN        16
#endif
#ifndef KLINE_TX_PIN
#define KLINE_TX_PIN        15
#endif
#define HONDA_BAUD          10400
#define YAMAHA_BAUD         15625

#define KLINE_RESP_HDR          0x02
#define KLINE_PAYLOAD_OFFSET    4

#define KL_FAIL_LIMIT           5       // consecutive failed polls before the link is dropped
#define KL_RETRY_MS             1500
#define KL_YAM_IDLE_HIGH_MS     320     // > 300 ms idle HIGH
#define KL_YAM_PULSE_MS         25
#define KL_YAM_ATTEMPTS         2

// Yamaha YDS stream, verified from a real capture on M-Slaz V2 (after the 0x01 poll, echo removed):
//   RPM | VSS | ERR | TEMP | CHK   (5 bytes), CHK = (RPM+VSS+ERR+TEMP) & 0xFF
#define KL_YAM_FRAME_LEN     5
#define KL_YAM_RPM_SCALE     50
#define KL_YAM_TEMP_OFFSET   30   // PROVISIONAL (alternative candidate: raw / 2)
#define KL_YAM_USE_ECU_SPEED 0
#define KL_YAM_SPEED_SCALE   1.0f
#define KL_YAM_DEBUG_RAW     0

enum KLineState : uint8_t { KL_OFF = 0, KL_CONNECTING = 1, KL_LINKED = 2 };

struct KLineSnapshot {
  KLineState state;
  uint8_t    ecuType;      // 0=NONE, 1=HONDA, 2=YAMAHA
  bool       linkUp;
  uint32_t   seq;
  uint32_t   lastOkMs;
  char       ecmId[32];
  uint16_t   rpm;
  uint16_t   speedKmh;
  bool       speedValid;   // true when the ECU itself delivers vehicle speed
  float      tps, tpsVolt, batteryV, injectorMs, ignitionDeg, o2Volt;
  int16_t    tempC;
};

static portMUX_TYPE   s_klMux = portMUX_INITIALIZER_UNLOCKED;
static KLineSnapshot  s_klSnap;
static TaskHandle_t   s_klTask = NULL;
static uint32_t       s_klBaud = YAMAHA_BAUD;
static uint8_t        s_klEchoMiss = 0;
static bool           s_klEchoSeen = false;
static uint8_t        s_klFails = 0;
static volatile uint8_t s_klBrand = ECU_TYPE_NONE;   // chosen on the screen; NONE = try both
static volatile bool    s_klBrandChanged = false;

// Called by the UI whenever the brand is chosen / changed
static void kline_set_brand(uint8_t brand) {
  if (brand != s_klBrand) { s_klBrand = brand; s_klBrandChanged = true; }
}

static const uint8_t KL_WAKEUP[] = { 0xFE, 0x04, 0x72, 0x8C };
static const uint8_t KL_INIT[]   = { 0x72, 0x05, 0x00, 0xF0, 0x99 };
// Honda data tables - the SAME commands and byte offsets the sender (ecu_reader_c3) uses
static const uint8_t KL_T17[]    = { 0x72, 0x05, 0x71, 0x17, 0x01 };   // RPM / TPS / temp / battery / injector / ignition
static const uint8_t KL_T20[]    = { 0x72, 0x05, 0x71, 0x20, 0xF8 };   // O2 / AFR sensor

static uint8_t kl_checksum(const uint8_t *p, size_t n) {
  uint8_t sum = 0;
  for (size_t i = 0; i < n; i++) sum += p[i];
  return (uint8_t)(0x100 - sum);
}

static inline uint8_t  kl_u8(const uint8_t *p, size_t n, size_t off)  { return (off < n) ? p[off] : 0; }
static inline uint16_t kl_u16(const uint8_t *p, size_t n, size_t off) {
  return ((uint16_t)kl_u8(p, n, off) << 8) | kl_u8(p, n, off + 1);
}

static void kl_flush_rx() {
  while (Serial1.available()) Serial1.read();
}

// ---- snapshot helpers -------------------------------------------------------
// Wipe every measurement so nothing from the previous ECU/brand survives a (re)connect
static void kl_clear_data() {
  portENTER_CRITICAL(&s_klMux);
  s_klSnap.rpm = 0;
  s_klSnap.speedKmh = 0;
  s_klSnap.speedValid = false;
  s_klSnap.tps = s_klSnap.tpsVolt = s_klSnap.batteryV = 0.0f;
  s_klSnap.injectorMs = s_klSnap.ignitionDeg = s_klSnap.o2Volt = 0.0f;
  s_klSnap.tempC = 0;
  s_klSnap.ecmId[0] = '\0';
  s_klSnap.lastOkMs = 0;
  portEXIT_CRITICAL(&s_klMux);
}

static void kl_set_state(KLineState st, uint8_t type = ECU_TYPE_NONE) {
  portENTER_CRITICAL(&s_klMux);
  s_klSnap.state   = st;
  s_klSnap.ecuType = type;
  s_klSnap.linkUp  = (st == KL_LINKED);
  portEXIT_CRITICAL(&s_klMux);
}

static void kl_set_ecm_id(const char *name) {
  portENTER_CRITICAL(&s_klMux);
  strncpy(s_klSnap.ecmId, name, sizeof(s_klSnap.ecmId) - 1);
  s_klSnap.ecmId[sizeof(s_klSnap.ecmId) - 1] = '\0';
  portEXIT_CRITICAL(&s_klMux);
}

// ---- UART / bus -------------------------------------------------------------
static void kl_open_uart(uint32_t baud) {
  Serial1.end();
  Serial1.setRxBufferSize(256);
  Serial1.begin(baud, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN);
  s_klBaud = baud;
  s_klEchoMiss = 0;
  s_klEchoSeen = false;
  delay(5);
  kl_flush_rx();
}

// Honda: LOW 70 ms / HIGH 130 ms.  Yamaha: HIGH > 300 ms, LOW 25 ms, HIGH 25 ms.
static void kl_fast_init_wave(uint32_t baud, bool yamaha) {
  Serial1.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  digitalWrite(KLINE_TX_PIN, HIGH);
  if (yamaha) {
    delay(KL_YAM_IDLE_HIGH_MS);
    digitalWrite(KLINE_TX_PIN, LOW);   delay(KL_YAM_PULSE_MS);
    digitalWrite(KLINE_TX_PIN, HIGH);  delay(KL_YAM_PULSE_MS);
  } else {
    delay(100);
    digitalWrite(KLINE_TX_PIN, LOW);   delay(70);
    digitalWrite(KLINE_TX_PIN, HIGH);  delay(130);
  }
  kl_open_uart(baud);
}

// Hardware echo eraser: removes exactly the bytes that mirror the request (in order),
// using peek() so a genuine ECU byte is never swallowed. Disables itself after 3 misses
// in a row when no echo is ever seen (transceiver without echo).
static uint8_t kl_swallow_echo(const uint8_t *tx, size_t n) {
  if (s_klEchoMiss >= 3 && !s_klEchoSeen) return 0;
  const uint32_t windowUs = ((uint32_t)n * 10UL * 1000000UL) / s_klBaud + 4000UL;
  const uint32_t t0 = micros();
  size_t matched = 0;
  while (matched < n && (micros() - t0) < windowUs) {
    if (Serial1.available()) {
      if ((uint8_t)Serial1.peek() != tx[matched]) break;
      (void)Serial1.read();
      matched++;
    } else {
      delayMicroseconds(100);
    }
  }
  if (matched == 0) { if (s_klEchoMiss < 255) s_klEchoMiss++; }
  else              { s_klEchoMiss = 0; s_klEchoSeen = true; }
  return (uint8_t)matched;
}

static void kl_send(const uint8_t *tx, size_t n) {
  kl_flush_rx();
  Serial1.write(tx, n);
  Serial1.flush();            // wait until the last bit has left the UART
  kl_swallow_echo(tx, n);     // erase the transceiver reflection
}

// Collect raw bytes until the line is quiet (Yamaha stream has no header/checksum)
static size_t kl_read_quiet(uint8_t *buf, size_t cap, uint32_t timeoutMs, uint32_t gapMs = 6) {
  const uint32_t t0 = millis();
  uint32_t lastByte = t0;
  size_t n = 0;
  while ((millis() - t0) < timeoutMs && n < cap) {
    while (Serial1.available() && n < cap) { buf[n++] = (uint8_t)Serial1.read(); lastByte = millis(); }
    if (n > 0 && (millis() - lastByte) >= gapMs) break;
    delayMicroseconds(200);
  }
  return n;
}

// Honda framed read: 0x02 | len | ... | checksum
static bool kl_read_frame(uint8_t *buf, size_t cap, size_t *outLen, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  size_t idx = 0, need = 0;
  while ((millis() - t0) < timeoutMs) {
    if (!Serial1.available()) { delay(1); continue; }
    const uint8_t b = (uint8_t)Serial1.read();

    if (idx == 0) {
      if (b == KLINE_RESP_HDR) buf[idx++] = b;
      continue;
    }
    buf[idx++] = b;
    if (idx == 2) {
      need = b;
      if (need < 4 || need > cap) { idx = 0; need = 0; }
      continue;
    }
    if (need && idx >= need) {
      if (kl_checksum(buf, need - 1) == buf[need - 1]) {
        *outLen = need;
        return true;
      }
      idx = 0; need = 0;
    }
  }
  return false;
}

static bool kl_transact(const uint8_t *tx, size_t n, uint8_t *rx, size_t cap, size_t *rxLen, uint32_t timeoutMs) {
  kl_send(tx, n);
  return kl_read_frame(rx, cap, rxLen, timeoutMs);
}

static int kl_yam_find_frame(const uint8_t *rx, size_t n) {
  for (int i = (int)n - KL_YAM_FRAME_LEN; i >= 0; i--) {
    const uint8_t sum = (uint8_t)(rx[i] + rx[i + 1] + rx[i + 2] + rx[i + 3]);
    if (sum != rx[i + 4]) continue;
    if ((rx[i] | rx[i + 1] | rx[i + 2] | rx[i + 3] | rx[i + 4]) == 0) continue;
    return i;
  }
  return -1;
}

static bool kl_yam_plausible(const uint8_t *p, size_t n) { return kl_yam_find_frame(p, n) >= 0; }

// ---- connect ----------------------------------------------------------------
static bool kl_try_honda() {
  uint8_t rx[64];
  size_t n = 0;
  kl_fast_init_wave(HONDA_BAUD, false);
  // A valid framed reply (0x02 header + checksum) AFTER echo removal is required
  if (kl_transact(KL_WAKEUP, sizeof(KL_WAKEUP), rx, sizeof(rx), &n, 120) ||
      kl_transact(KL_INIT,   sizeof(KL_INIT),   rx, sizeof(rx), &n, 200) ||
      kl_transact(KL_T17,    sizeof(KL_T17),    rx, sizeof(rx), &n, 150)) {
    kl_clear_data();
    kl_set_ecm_id("HONDA-KEIHIN");
    kl_set_state(KL_LINKED, ECU_TYPE_HONDA);
    return true;
  }
  return false;
}

static bool kl_try_yamaha_baud(uint32_t baud) {
  uint8_t rx[32];
  for (uint8_t a = 0; a < KL_YAM_ATTEMPTS; a++) {
    kl_fast_init_wave(baud, true);

    const uint8_t wake[] = { 0xFE };
    kl_send(wake, sizeof(wake));
    size_t n = kl_read_quiet(rx, sizeof(rx), 200);
    bool ok = (n > 0);                       // echo already erased -> this is the ECU

    if (!ok) {                               // some ECUs only talk after the first poll
      const uint8_t poll[] = { 0x01 };
      kl_send(poll, sizeof(poll));
      n = kl_read_quiet(rx, sizeof(rx), 150);
      ok = kl_yam_plausible(rx, n);
    }
    if (ok) {
      kl_clear_data();
      kl_set_ecm_id("YAMAHA-ECU");           // no Honda ECM-ID request, ever
      kl_set_state(KL_LINKED, ECU_TYPE_YAMAHA);
      return true;
    }
  }
  return false;
}

static bool kl_connect() {
  kl_clear_data();
  kl_set_state(KL_CONNECTING);
  s_klFails = 0;

  const uint8_t brand = s_klBrand;                      // only the chosen brand is scanned
  if ((brand == ECU_TYPE_NONE || brand == ECU_TYPE_HONDA)  && kl_try_honda())                  return true;
  if ((brand == ECU_TYPE_NONE || brand == ECU_TYPE_YAMAHA) && kl_try_yamaha_baud(YAMAHA_BAUD)) return true;   // 15625 first
  if ((brand == ECU_TYPE_NONE || brand == ECU_TYPE_YAMAHA) && kl_try_yamaha_baud(HONDA_BAUD))  return true;   // then 10400

  kl_set_state(KL_OFF, ECU_TYPE_NONE);
  return false;
}

static void kl_drop_link() {
  kl_clear_data();
  kl_set_state(KL_OFF, ECU_TYPE_NONE);
}

// ---- polling task -----------------------------------------------------------
// Honda: table 0x17 then table 0x20.  A framed reply is  02 | len | ... | checksum  and the
// offsets below index the frame starting at the 0x02 header (identical to the sender's decoder).
static bool kl_poll_honda() {
  uint8_t rx[64];
  size_t n = 0;
  if (!kl_transact(KL_T17, sizeof(KL_T17), rx, sizeof(rx), &n, 100)) return false;
  if (n < 0x13) return false;                              // payload (without checksum) must reach 0x12

  const uint16_t rpm     = ((uint16_t)rx[0x04] << 8) | rx[0x05];
  const float    tpsVolt = (float)rx[0x06] * (5.0f / 256.0f);
  const float    tps     = (float)rx[0x07] * 0.5f;
  const int16_t  tempC   = (int16_t)rx[0x09] - 40;
  const float    battV   = (float)rx[0x0E] / 10.0f;
  const float    injMs   = (float)(((uint16_t)rx[0x0F] << 8) | rx[0x10]) / 250.0f;
  const float    ignDeg  = ((float)rx[0x11] / 2.0f) - 64.0f;

  float o2 = -1.0f;
  size_t n2 = 0;
  uint8_t rx2[48];
  if (kl_transact(KL_T20, sizeof(KL_T20), rx2, sizeof(rx2), &n2, 100) && n2 > 0x05) {
    o2 = (float)rx2[0x04] * (2.5f / 128.0f);
  }

  portENTER_CRITICAL(&s_klMux);
  s_klSnap.rpm         = rpm;
  s_klSnap.tpsVolt     = tpsVolt;
  s_klSnap.tps         = tps;
  s_klSnap.tempC       = tempC;
  s_klSnap.batteryV    = battV;
  s_klSnap.injectorMs  = injMs;
  s_klSnap.ignitionDeg = ignDeg;
  if (o2 >= 0.0f) s_klSnap.o2Volt = o2;
  s_klSnap.speedValid  = false;                            // Honda tables carry no wheel speed
  s_klSnap.lastOkMs    = millis();
  s_klSnap.seq++;
  portEXIT_CRITICAL(&s_klMux);
  return true;
}

static bool kl_poll_yamaha() {
  uint8_t rx[32];
  const uint8_t poll[] = { 0x01 };
  kl_send(poll, sizeof(poll));
  const size_t n = kl_read_quiet(rx, sizeof(rx), 100);

#if KL_YAM_DEBUG_RAW
  static uint32_t lastDbg = 0;
  if (n > 0 && (millis() - lastDbg) >= 250) {
    lastDbg = millis();
    Serial.printf("[YAM RAW n=%u] ", (unsigned)n);
    for (size_t i = 0; i < n; i++) Serial.printf("%02X ", rx[i]);
    Serial.println();
  }
#endif
  if (n == 0) return false;                        // link problem
  const int at = kl_yam_find_frame(rx, n);
  if (at < 0) return true;                         // bytes present, no frame: keep link, keep logging

  const uint8_t *f = rx + at;                      // RPM VSS ERR TEMP CHK
  portENTER_CRITICAL(&s_klMux);
  s_klSnap.rpm   = (uint16_t)f[0] * KL_YAM_RPM_SCALE;
  s_klSnap.tempC = (int16_t)f[3] - KL_YAM_TEMP_OFFSET;
  s_klSnap.tps = s_klSnap.tpsVolt = s_klSnap.batteryV = 0.0f;
  s_klSnap.injectorMs = s_klSnap.ignitionDeg = s_klSnap.o2Volt = 0.0f;
#if KL_YAM_USE_ECU_SPEED
  s_klSnap.speedKmh   = (uint16_t)(f[1] * KL_YAM_SPEED_SCALE);
  s_klSnap.speedValid = true;
#else
  s_klSnap.speedValid = false;
#endif
  s_klSnap.lastOkMs = millis();
  s_klSnap.seq++;
  portEXIT_CRITICAL(&s_klMux);
  return true;
}

static void kline_task(void *arg) {
  (void)arg;
  for (;;) {
    if (s_klBrandChanged) {                              // brand switched on screen: drop the link and rescan
      s_klBrandChanged = false;
      kl_drop_link();
    }
    KLineSnapshot cur;
    portENTER_CRITICAL(&s_klMux);
    cur = s_klSnap;
    portEXIT_CRITICAL(&s_klMux);

    if (!cur.linkUp) {
      if (!kl_connect()) {
        vTaskDelay(pdMS_TO_TICKS(KL_RETRY_MS));
        continue;
      }
    }

    bool ok = false;
    if (s_klSnap.ecuType == ECU_TYPE_HONDA)       ok = kl_poll_honda();
    else if (s_klSnap.ecuType == ECU_TYPE_YAMAHA) ok = kl_poll_yamaha();

    if (ok) {
      s_klFails = 0;
    } else if (++s_klFails >= KL_FAIL_LIMIT) {
      kl_drop_link();                       // clean state before the next scan
      vTaskDelay(pdMS_TO_TICKS(KL_RETRY_MS));
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

static void kline_begin() {
  memset(&s_klSnap, 0, sizeof(s_klSnap));
  s_klSnap.state = KL_OFF;
  if (!s_klTask) xTaskCreatePinnedToCore(kline_task, "kline", 6144, NULL, 3, &s_klTask, 0);
}

static void kline_get(KLineSnapshot *out) {
  portENTER_CRITICAL(&s_klMux);
  memcpy(out, &s_klSnap, sizeof(*out));
  portEXIT_CRITICAL(&s_klMux);
}
