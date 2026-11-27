#pragma GCC optimize ("O2")
#pragma GCC optimize ("fast-math")

// ============================================================================
// PROJECT: DUAL ECU K-LINE SENDER (HONDA WAVE & YAMAHA K-LINE SUPPORT)
// Pins: K-Line RX=1 TX=2 | Gear N=3 G1=4 G2=5 G3=6 G4=7 | Wheel speed=13 | NeoPixel=48
// Sends GaugeDataPacket (incl. speedKmh & ecuType) + EcmIdPacket over ESP-NOW
// ============================================================================

#include <Arduino.h>
#include <string.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "driver/temperature_sensor.h"
#include "shared_data_struct.h"

#define KLINE_RX_PIN 1      
#define KLINE_TX_PIN 2      
#define HONDA_BAUD   10400
#define YAMAHA_BAUD  15625

#define GEAR_N_PIN  3       
#define GEAR_1_PIN  4       
#define GEAR_2_PIN  5      
#define GEAR_3_PIN  6      
#define GEAR_4_PIN  7      

// ---- Wheel speed sensor (GPIO 13) ----
#define SPEED_PIN               13
#define SPEED_PULSES_PER_REV    2
#define WHEEL_CIRCUMFERENCE_M   1.82f
#define SPEED_MIN_PULSE_US      2500UL
#define SPEED_TIMEOUT_US        2000000UL

// ---- ECM ID & Gap Timing ----
#define SG_ECM_FRAME_MAX        32
#define SG_ECM_TIMEOUT_MS       150
#define SG_ECM_MAX_TRIES        5

// ปรับเร่งความเร็ว Gap และลด Latency การตอบสนอง
#define SG_RESPONSE_GAP_MS 2     // ลดลงเหลือ 2ms
#define SG_KLINE_GAP_MS 0        // ปรับเป็น 0ms ยิงคำสั่งถัดไปทันทีที่คำสั่งเดิมตอบกลับสำเร็จ
#define SG_T17_TIMEOUT_MS 100
#define SG_T20_TIMEOUT_MS 100
#define SG_DTC_TIMEOUT_MS 300
#define SG_CONNECT_RETRY_MS 2000
#define SG_RECONNECT_PAUSE_MS 2500
#define SG_GEAR_DEBOUNCE_MS 30   

// ---- Yamaha K-Line timing / stream layout ----
#define SG_YAM_IDLE_HIGH_MS     320   
#define SG_YAM_PULSE_LOW_MS     25
#define SG_YAM_PULSE_HIGH_MS    25
#define SG_YAM_SCAN_ATTEMPTS    2     
#define SG_YAM_WAKE_TIMEOUT_MS  200
#define SG_YAM_POLL_TIMEOUT_MS  100
#define SG_ECU_SPEED_FRESH_MS   1000  

#define YAM_FRAME_LEN     5
#define YAM_RPM_SCALE     50        
#define YAM_TEMP_OFFSET   30        
#define YAM_USE_ECU_SPEED 0         
#define YAM_SPEED_SCALE   1.0f
#define YAM_DEBUG_RAW     0         

// ---- Frame & Payload Buffer Sizes ----
#define SG_MAIN_PAYLOAD_MAX 32
#define SG_AUX_PAYLOAD_MAX  16
#define SG_T17_FRAME_MAX    32
#define SG_T20_FRAME_MAX    24
#define SG_DTC_FRAME_MAX    32

struct GaugeData {
  uint8_t ecuType = ECU_TYPE_NONE;
  uint16_t rpm = 0;
  float speedKmh = 0.0f;
  float tps = 0.0f;
  float tpsVolt = 0.0f;
  int16_t tempC = 0;
  float batteryV = 0.0f;
  float injectorMs = 0.0f;
  float ignitionDeg = 0.0f;
  float o2Volt = 0.0f;
  uint8_t gear = 255;       
  bool ecuConnected = false;
  bool t17Ok = false;
  bool t20Ok = false;
  uint8_t senderCpuLoad = 0;
  int8_t senderCpuTemp = 0;
};

struct DtcData {
  char lines[12][96];
  uint8_t count = 0;
  bool busy = false;
};

struct EcmData {
  bool valid = false;
  char text[20] = "";
  uint8_t tries = 0;
};

HardwareSerial KLINE(1);
EcmData sgEcm;
GaugeData sgGauge;
DtcData sgDtc;

uint8_t sgMainPayload[SG_MAIN_PAYLOAD_MAX];
uint8_t sgAuxPayload[SG_AUX_PAYLOAD_MAX];
uint8_t sgMainPayloadLen = 0;
uint8_t sgAuxPayloadLen = 0;

uint32_t sgConsecutiveFail = 0;
uint32_t sgLastConnectTryMs = 0;
uint32_t sgLastKLineCmdDoneMs = 0;
uint32_t sgReconnectNotBeforeMs = 0;
uint8_t sgPollStep = 0;
bool sgReconnectPending = false;

uint8_t sgLastRawGear = 255;
uint32_t sgLastGearChangeMs = 0;

volatile uint8_t sgPendingDtcCmd = 0;

volatile uint8_t sgBrand = ECU_TYPE_NONE;      
volatile bool    sgBrandChanged = false;
volatile uint8_t sgBrandToSave = 0xFF;

uint8_t  sgYamErr        = 0;      
bool     sgEcuSpeedValid = false;
uint32_t sgEcuSpeedMs    = 0;

uint32_t sgActiveBaud       = YAMAHA_BAUD;
bool     sgEchoHardwareSeen = false;
uint8_t  sgEchoMiss         = 0;

void sgSendGaugeDataNow();
void sgSendGaugeDataPeriodic();
void sgSendEcmIdNow();

int8_t sgReadCpuTemperature() {
  static temperature_sensor_handle_t temp_sensor = NULL;
  if (temp_sensor == NULL) {
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    temperature_sensor_enable(temp_sensor);
  }
  float tsens_out = 0.0;
  if (temperature_sensor_get_celsius(temp_sensor, &tsens_out) == ESP_OK) {
    return (int8_t)tsens_out;
  }
  return 0;
}

uint8_t sgReadGear() {
  if (digitalRead(GEAR_N_PIN) == HIGH) return 0;  
  if (digitalRead(GEAR_1_PIN) == HIGH) return 1;  
  if (digitalRead(GEAR_2_PIN) == HIGH) return 2;  
  if (digitalRead(GEAR_3_PIN) == HIGH) return 3;  
  if (digitalRead(GEAR_4_PIN) == HIGH) return 4;  
  return 255;                                     
}

uint8_t sgChecksum(const uint8_t *data, uint8_t count) {
  if (data == nullptr || count == 0) return 0;
  uint16_t sum = 0;
  for (uint8_t i = 0; i < count; ++i) sum += data[i];
  return (uint8_t)(0x100U - (sum & 0xFFU));
}

void sgKLineFlushRx() {
  while (KLINE.available()) (void)KLINE.read();
}

void sgKLineBegin(uint32_t baud) {
  KLINE.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  digitalWrite(KLINE_TX_PIN, HIGH);
  pinMode(KLINE_RX_PIN, INPUT);
  KLINE.setRxBufferSize(256);
  KLINE.begin(baud, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN);
  sgActiveBaud = baud;
  sgEchoMiss = 0;
  sgEchoHardwareSeen = false;
  delay(10);
  sgKLineFlushRx();
}

void sgPump(uint32_t ms) {
  const uint32_t t0 = millis();
  while ((millis() - t0) < ms) {
    delay(5);
    sgSendGaugeDataPeriodic();
  }
}

uint8_t sgKLineReadUntilQuiet(uint8_t *out, uint8_t maxLen, uint16_t timeoutMs) {
  if (out == nullptr || maxLen == 0) return 0;
  uint8_t n = 0;
  const uint32_t beginMs = millis();
  uint32_t lastByteMs = beginMs;
  while ((millis() - beginMs) < timeoutMs && n < maxLen) {
    while (KLINE.available() && n < maxLen) {
      out[n++] = (uint8_t)KLINE.read();
      lastByteMs = millis();
    }
    if (n > 0 && (millis() - lastByteMs) >= SG_RESPONSE_GAP_MS) break;
    delayMicroseconds(50); // เร่งความเร็ว Polling จาก 200us เป็น 50us
  }
  return n;
}

uint8_t sgKLineSwallowEcho(const uint8_t *tx, uint8_t txLen) {
  if (sgEchoMiss >= 3 && !sgEchoHardwareSeen) return 0;
  const uint32_t windowUs = ((uint32_t)txLen * 10UL * 1000000UL) / sgActiveBaud + 4000UL;
  const uint32_t t0 = micros();
  uint8_t matched = 0;
  while (matched < txLen && (micros() - t0) < windowUs) {
    if (KLINE.available()) {
      if ((uint8_t)KLINE.peek() != tx[matched]) break;   
      (void)KLINE.read();
      matched++;
    } else {
      delayMicroseconds(50);
    }
  }
  if (matched == 0) { if (sgEchoMiss < 255) sgEchoMiss++; }
  else              { sgEchoMiss = 0; sgEchoHardwareSeen = true; }
  return matched;
}

uint8_t sgKLineSendStripEcho(const uint8_t *tx, uint8_t txLen, uint8_t *rx, uint8_t rxMax, uint16_t timeoutMs) {
  if (tx == nullptr || txLen == 0 || rx == nullptr || rxMax == 0) return 0;
  sgKLineFlushRx();
  KLINE.write(tx, txLen);
  KLINE.flush();
  sgKLineSwallowEcho(tx, txLen);
  return sgKLineReadUntilQuiet(rx, rxMax, timeoutMs);
}

uint8_t sgKLineReadHondaFrame(uint8_t *out, uint8_t maxLen, uint16_t timeoutMs, uint8_t echoLen) {
  if (out == nullptr || maxLen == 0) return 0;
  uint8_t n = 0;
  const uint32_t beginMs = millis();
  uint32_t lastByteMs = beginMs;
  while ((millis() - beginMs) < timeoutMs && n < maxLen) {
    while (KLINE.available() && n < maxLen) {
      out[n++] = (uint8_t)KLINE.read();
      lastByteMs = millis();
    }
    if (n >= (uint8_t)(echoLen + 2) && out[echoLen] == 0x02) {
      const uint8_t len = out[echoLen + 1];
      if (len >= 4 && (uint16_t)echoLen + len <= maxLen && n >= (uint8_t)(echoLen + len)) break;   
    }
    if (n > 0 && (millis() - lastByteMs) >= SG_RESPONSE_GAP_MS) break;
    delayMicroseconds(50); // เร่งความเร็วการจับจุดสิ้นสุดเฟรม
  }
  return n;
}

bool sgValidateReply(const uint8_t *full, uint8_t fullLen, uint8_t echoLen) {
  if (full == nullptr) return false;
  if (fullLen <= echoLen + 1) return false;
  const uint8_t replyLen = fullLen - echoLen;
  if (replyLen < 2) return false;
  const uint8_t *reply = full + echoLen;
  return sgChecksum(reply, replyLen - 1) == reply[replyLen - 1];
}

uint8_t sgKLineSendCapture(const uint8_t *tx, uint8_t txLen, uint8_t *rx, uint8_t rxMax, uint16_t timeoutMs) {
  if (tx == nullptr || txLen == 0 || rx == nullptr || rxMax == 0) return 0;
  sgKLineFlushRx();
  KLINE.write(tx, txLen);
  KLINE.flush();
  return sgKLineReadUntilQuiet(rx, rxMax, timeoutMs);
}

const uint8_t SG_WAKEUP_FE[]  = { 0xFE, 0x04, 0x72, 0x8C };
const uint8_t SG_CONNECT_TN[] = { 0x2C, 0x54, 0x4E, 0x23 };
const uint8_t SG_INIT_ECU[]   = { 0x72, 0x05, 0x00, 0xF0, 0x99 };   
const uint8_t SG_T17_CMD[]    = { 0x72, 0x05, 0x71, 0x17, 0x01 };
const uint8_t SG_T20_CMD[]    = { 0x72, 0x05, 0x71, 0x20, 0xF8 };
const uint8_t SG_ECM_CMD[]    = { 0x72, 0x05, 0x71, 0x00, 0x18 };

void sgResetLiveFields() {
  sgGauge.rpm = 0;
  sgGauge.speedKmh = 0.0f;
  sgGauge.tps = 0.0f;
  sgGauge.tpsVolt = 0.0f;
  sgGauge.tempC = 0;
  sgGauge.batteryV = 0.0f;
  sgGauge.injectorMs = 0.0f;
  sgGauge.ignitionDeg = 0.0f;
  sgGauge.o2Volt = 0.0f;
  sgGauge.t17Ok = false;
  sgGauge.t20Ok = false;
  memset(sgMainPayload, 0, sizeof(sgMainPayload));
  memset(sgAuxPayload, 0, sizeof(sgAuxPayload));
  sgMainPayloadLen = 0;
  sgAuxPayloadLen = 0;
  sgEcuSpeedValid = false;
  sgEcuSpeedMs = 0;
  sgYamErr = 0;
  sgConsecutiveFail = 0;
  sgPollStep = 0;
}

bool sgHondaFrameAt(const uint8_t *rx, uint8_t n, uint8_t off) {
  if (rx == nullptr || n < (uint8_t)(off + 4)) return false;
  const uint8_t *f = rx + off;
  if (f[0] != 0x02) return false;
  const uint8_t len = f[1];
  if (len < 4 || (uint16_t)off + len > n) return false;
  return sgChecksum(f, len - 1) == f[len - 1];
}

bool sgHondaReplyOk(const uint8_t *rx, uint8_t n, uint8_t echoLen) {
  return sgHondaFrameAt(rx, n, echoLen) || sgHondaFrameAt(rx, n, 0);
}

bool sgKLineWakeupHonda() {
  KLINE.end();
  pinMode(KLINE_TX_PIN, OUTPUT);
  digitalWrite(KLINE_TX_PIN, LOW);
  delay(70);
  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(130);
  sgKLineBegin(HONDA_BAUD);

  uint8_t rx[48];
  uint8_t n;
  bool ok = false;

  n = sgKLineSendCapture(SG_WAKEUP_FE, sizeof(SG_WAKEUP_FE), rx, sizeof(rx), 150);
  ok = sgHondaReplyOk(rx, n, sizeof(SG_WAKEUP_FE));
  sgSendGaugeDataPeriodic();

  if (!ok) {
    delay(5);
    n = sgKLineSendCapture(SG_INIT_ECU, sizeof(SG_INIT_ECU), rx, sizeof(rx), 200);
    ok = sgHondaReplyOk(rx, n, sizeof(SG_INIT_ECU));
    sgSendGaugeDataPeriodic();
  }

  if (!ok) {
    n = sgKLineSendCapture(SG_CONNECT_TN, sizeof(SG_CONNECT_TN), rx, sizeof(rx), 200);
    ok = sgHondaReplyOk(rx, n, sizeof(SG_CONNECT_TN));
    if (!ok) {
      delay(5);
      n = sgKLineSendCapture(SG_INIT_ECU, sizeof(SG_INIT_ECU), rx, sizeof(rx), 200);
      ok = sgHondaReplyOk(rx, n, sizeof(SG_INIT_ECU));
    }
    sgSendGaugeDataPeriodic();
  }

  if (!ok) {
    n = sgKLineSendCapture(SG_T17_CMD, sizeof(SG_T17_CMD), rx, sizeof(rx), 150);
    ok = sgHondaReplyOk(rx, n, sizeof(SG_T17_CMD));
  }

  if (ok) {
    sgResetLiveFields();
    sgGauge.ecuType = ECU_TYPE_HONDA;
    sgGauge.ecuConnected = true;
    sgEcm.valid = false;
    sgEcm.tries = 0;
    return true;
  }
  return false;
}

int sgYamFindFrame(const uint8_t *rx, uint8_t n) {
  for (int i = (int)n - YAM_FRAME_LEN; i >= 0; i--) {
    const uint8_t sum = (uint8_t)(rx[i] + rx[i + 1] + rx[i + 2] + rx[i + 3]);
    if (sum != rx[i + 4]) continue;
    if ((rx[i] | rx[i + 1] | rx[i + 2] | rx[i + 3] | rx[i + 4]) == 0) continue;
    return i;
  }
  return -1;
}

bool sgPlausibleYamahaFrame(const uint8_t *rx, uint8_t n) {
  return sgYamFindFrame(rx, n) >= 0;
}

bool sgKLineWakeupYamahaBaud(uint32_t baud) {
  uint8_t rx[32];
  for (uint8_t attempt = 0; attempt < SG_YAM_SCAN_ATTEMPTS; attempt++) {
    KLINE.end();
    pinMode(KLINE_TX_PIN, OUTPUT);
    digitalWrite(KLINE_TX_PIN, HIGH);
    sgPump(SG_YAM_IDLE_HIGH_MS);
    digitalWrite(KLINE_TX_PIN, LOW);
    delay(SG_YAM_PULSE_LOW_MS);
    digitalWrite(KLINE_TX_PIN, HIGH);
    delay(SG_YAM_PULSE_HIGH_MS);

    sgKLineBegin(baud);

    const uint8_t ymWakeup[] = { 0xFE };
    uint8_t n = sgKLineSendStripEcho(ymWakeup, sizeof(ymWakeup), rx, sizeof(rx), SG_YAM_WAKE_TIMEOUT_MS);
    bool ok = (n > 0);

    if (!ok) {   
      const uint8_t poll[] = { 0x01 };
      n = sgKLineSendStripEcho(poll, sizeof(poll), rx, sizeof(rx), SG_YAM_POLL_TIMEOUT_MS + 50);
      ok = sgPlausibleYamahaFrame(rx, n);
    }

    if (ok) {
      sgResetLiveFields();
      sgGauge.ecuType = ECU_TYPE_YAMAHA;
      sgGauge.ecuConnected = true;
      snprintf(sgEcm.text, sizeof(sgEcm.text), "YAMAHA-ECU");
      sgEcm.valid = true;
      sgEcm.tries = 0;
      Serial.printf("[K-LINE] Yamaha linked @ %u bps (echo %s)\n", (unsigned)baud,
                    sgEchoHardwareSeen ? "filtered" : "none");
      return true;
    }
  }
  return false;
}

bool sgKLineWakeupYamaha() {
  if (sgKLineWakeupYamahaBaud(YAMAHA_BAUD)) return true;
  if (sgKLineWakeupYamahaBaud(HONDA_BAUD))  return true;
  return false;
}

bool sgKLineWakeupAuto() {
  const uint8_t brand = sgBrand;
  Serial.printf("[K-LINE] Scanning ECU (%s)...\n", brand == ECU_TYPE_HONDA ? "Honda" : (brand == ECU_TYPE_YAMAHA ? "Yamaha" : "Honda / Yamaha"));
  sgResetLiveFields();
  sgEcm.valid = false;
  sgEcm.text[0] = '\0';
  if ((brand == ECU_TYPE_NONE || brand == ECU_TYPE_HONDA) && sgKLineWakeupHonda()) {
    Serial.println("[ECU CONNECTED] Honda K-Line ECU Detected");
    return true;
  }
  if ((brand == ECU_TYPE_NONE || brand == ECU_TYPE_YAMAHA) && sgKLineWakeupYamaha()) {
    Serial.println("[ECU CONNECTED] Yamaha K-Line ECU Detected");
    return true;
  }
  sgGauge.ecuType = ECU_TYPE_NONE;
  sgGauge.ecuConnected = false;
  return false;
}

uint8_t espnowBroadcastMac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

void sgScheduleReconnect(const char *reason) {
  Serial.printf("[K-LINE] Reconnecting... (%s)\n", reason ? reason : "UNKNOWN");
  sgGauge.ecuConnected = false;
  sgGauge.ecuType = ECU_TYPE_NONE;
  sgEcm.valid = false;
  sgEcm.text[0] = '\0';
  sgEcm.tries = 0;
  sgResetLiveFields();

  sgReconnectPending = true;
  sgReconnectNotBeforeMs = millis() + SG_RECONNECT_PAUSE_MS;
  sgLastConnectTryMs = millis();

  sgSendGaugeDataNow();
  sgSendEcmIdNow();
}

void sgEnsureConnected() {
  const uint32_t now = millis();
  if (sgGauge.ecuConnected) return;
  if (sgReconnectPending) {
    if ((int32_t)(now - sgReconnectNotBeforeMs) < 0) return;
    sgReconnectPending = false;
  }
  if (now - sgLastConnectTryMs < SG_CONNECT_RETRY_MS) return;
  sgLastConnectTryMs = now;
  sgKLineWakeupAuto();
}

bool sgDecodeWaveMain() {
  if (sgMainPayloadLen < 0x12) return false;
  sgGauge.rpm = ((uint16_t)sgMainPayload[0x04] << 8) | sgMainPayload[0x05];
  sgGauge.tpsVolt = (float)sgMainPayload[0x06] * (5.0f / 256.0f);
  sgGauge.tps = (float)sgMainPayload[0x07] * 0.5f;
  sgGauge.tempC = (int16_t)sgMainPayload[0x09] - 40;
  sgGauge.batteryV = (float)sgMainPayload[0x0E] / 10.0f;
  const uint16_t rawInj = ((uint16_t)sgMainPayload[0x0F] << 8) | sgMainPayload[0x10];
  sgGauge.injectorMs = (float)rawInj / 250.0f;
  sgGauge.ignitionDeg = ((float)sgMainPayload[0x11] / 2.0f) - 64.0f;
  return true;
}

bool sgDecodeWaveAux() {
  if (sgAuxPayloadLen <= 0x04) return false;
  const uint8_t afrRaw = sgAuxPayload[0x04];
  sgGauge.o2Volt = (float)afrRaw * (2.5f / 128.0f);
  return true;
}

bool sgReadTable17() {
  uint8_t frame[SG_T17_FRAME_MAX];
  sgKLineFlushRx();
  KLINE.write(SG_T17_CMD, sizeof(SG_T17_CMD));
  KLINE.flush();
  const uint8_t echoLen = sizeof(SG_T17_CMD);
  const uint8_t n = sgKLineReadHondaFrame(frame, sizeof(frame), SG_T17_TIMEOUT_MS, echoLen);

  if (n < echoLen + 2 || !sgValidateReply(frame, n, echoLen)) {
    if (++sgConsecutiveFail >= 5) sgScheduleReconnect("T17 FAIL");
    return false;
  }
  const uint8_t declaredLen = frame[echoLen + 1];
  if (declaredLen < 2) return false;
  const uint8_t replyLen = declaredLen - 1;
  if (replyLen > SG_MAIN_PAYLOAD_MAX) return false;

  memcpy(sgMainPayload, frame + echoLen, replyLen);
  sgMainPayloadLen = replyLen;

  if (!sgDecodeWaveMain()) return false;
  sgGauge.t17Ok = true;
  sgConsecutiveFail = 0;
  return true;
}

bool sgReadTable20() {
  uint8_t frame[SG_T20_FRAME_MAX];
  sgKLineFlushRx();
  KLINE.write(SG_T20_CMD, sizeof(SG_T20_CMD));
  KLINE.flush();
  const uint8_t echoLen = sizeof(SG_T20_CMD);
  const uint8_t n = sgKLineReadHondaFrame(frame, sizeof(frame), SG_T20_TIMEOUT_MS, echoLen);

  if (n < echoLen + 2 || !sgValidateReply(frame, n, echoLen)) {
    if (++sgConsecutiveFail >= 5) sgScheduleReconnect("T20 FAIL");
    return false;
  }
  const uint8_t declaredLen = frame[echoLen + 1];
  if (declaredLen < 2) return false;
  const uint8_t replyLen = declaredLen - 1;
  if (replyLen > SG_AUX_PAYLOAD_MAX) return false;

  memcpy(sgAuxPayload, frame + echoLen, replyLen);
  sgAuxPayloadLen = replyLen;

  if (!sgDecodeWaveAux()) return false;
  sgGauge.t20Ok = true;
  sgConsecutiveFail = 0;
  return true;
}

bool sgReadYamahaStream() {
  const uint8_t cmd[] = { 0x01 };
  uint8_t rx[32];
  const uint8_t n = sgKLineSendStripEcho(cmd, sizeof(cmd), rx, sizeof(rx), SG_YAM_POLL_TIMEOUT_MS);

#if YAM_DEBUG_RAW
  static uint32_t lastDbgMs = 0;
  if (n > 0 && (millis() - lastDbgMs) >= 250) {
    lastDbgMs = millis();
    Serial.printf("[YAM RAW n=%u] ", n);
    for (uint8_t i = 0; i < n; i++) Serial.printf("%02X ", rx[i]);
    Serial.println();
  }
#endif

  if (n == 0) {                                   
    if (++sgConsecutiveFail >= 5) sgScheduleReconnect("YAMAHA POLL FAIL");
    return false;
  }
  const int at = sgYamFindFrame(rx, n);
  if (at < 0) return false;                       

  const uint8_t *f = rx + at;                     

#if YAM_DEBUG_RAW
  static uint8_t vssRing[8] = {0};
  static uint8_t vssIdx = 0;
  vssRing[vssIdx++ & 7] = f[1];
  static uint32_t lastCalMs = 0;
  if ((millis() - lastCalMs) >= 500) {
    lastCalMs = millis();
    uint16_t sum8 = 0;
    for (uint8_t i = 0; i < 8; i++) sum8 += vssRing[i];
    Serial.printf("[YAM CAL] rpmRaw=%u vss=%u vssSum8=%u gpioKmh=%.1f tempRaw=%u (0x%02X) err=%u\n",
                  f[0], f[1], sum8, sgGauge.speedKmh, f[3], f[3], f[2]);
  }
#endif
  sgYamErr      = f[2];
  sgGauge.rpm   = (uint16_t)f[0] * YAM_RPM_SCALE;
  sgGauge.tempC = (int16_t)f[3] - YAM_TEMP_OFFSET;
  sgGauge.tps = 0; sgGauge.tpsVolt = 0; sgGauge.batteryV = 0;
  sgGauge.injectorMs = 0; sgGauge.ignitionDeg = 0; sgGauge.o2Volt = 0;

#if YAM_USE_ECU_SPEED
  sgGauge.speedKmh = (float)f[1] * YAM_SPEED_SCALE;
  sgEcuSpeedValid = true;
  sgEcuSpeedMs = millis();
#endif

  sgGauge.t17Ok = true;
  sgGauge.t20Ok = true;
  sgConsecutiveFail = 0;
  return true;
}

bool sgReadEcmId() {
  if (sgGauge.ecuType == ECU_TYPE_YAMAHA) {      
    snprintf(sgEcm.text, sizeof(sgEcm.text), "YAMAHA-ECU");
    sgEcm.valid = true;
    return true;
  }

  uint8_t frame[SG_ECM_FRAME_MAX];
  const uint8_t echoLen = sizeof(SG_ECM_CMD);
  const uint8_t n = sgKLineSendCapture(SG_ECM_CMD, sizeof(SG_ECM_CMD), frame, sizeof(frame), SG_ECM_TIMEOUT_MS);

  // รองรับทั้งวงจรที่มี Echo (offset = echoLen) และไม่มี Echo (offset = 0)
  uint8_t replyOff = 0;
  if (sgHondaFrameAt(frame, n, echoLen)) {
    replyOff = echoLen;
  } else if (sgHondaFrameAt(frame, n, 0)) {
    replyOff = 0;
  } else {
    return false;
  }

  const uint8_t *reply = frame + replyOff;
  const uint8_t declaredLen = reply[1];
  if (declaredLen < 5) return false;

  const uint8_t idEnd = declaredLen - 1;
  char text[sizeof(sgEcm.text)] = "";
  size_t pos = 0;
  for (uint8_t i = 4; i < idEnd && pos + 2 < sizeof(text); i++) {
    pos += snprintf(text + pos, sizeof(text) - pos, "%02X", reply[i]);
  }
  if (pos == 0) return false;

  memcpy(sgEcm.text, text, sizeof(sgEcm.text));
  sgEcm.valid = true;
  return true;
}

void sgSendEcmIdNow() {
  EcmIdPacket pkt = {};
  pkt.packetType = PKT_TYPE_ECM_ID;
  pkt.valid = sgEcm.valid;
  memcpy(pkt.text, sgEcm.text, sizeof(pkt.text));
  pkt.text[sizeof(pkt.text) - 1] = '\0';
  esp_now_send(espnowBroadcastMac, (uint8_t *)&pkt, sizeof(pkt));
}

void sgSendEcmIdPeriodic() {
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs >= 2000) {
    lastSendMs = now;
    sgSendEcmIdNow();
  }
}

// ---------------- Wheel speed (GPIO 13) ----------------
portMUX_TYPE sgSpeedMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t sgSpLastUs = 0;
volatile uint32_t sgSpPeriodUs = 0;

void IRAM_ATTR sgSpeedIsr() {
  const uint32_t now = micros();
  portENTER_CRITICAL_ISR(&sgSpeedMux);
  const uint32_t dt = now - sgSpLastUs;
  if (dt >= SPEED_MIN_PULSE_US) {
    sgSpLastUs = now;
    if (dt >= SPEED_TIMEOUT_US)  sgSpPeriodUs = 0;
    else if (sgSpPeriodUs == 0)  sgSpPeriodUs = dt;
    else                         sgSpPeriodUs = (sgSpPeriodUs * 3 + dt) / 4;
  }
  portEXIT_CRITICAL_ISR(&sgSpeedMux);
}

void sgSpeedBegin() {
  pinMode(SPEED_PIN, INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(SPEED_PIN), sgSpeedIsr, RISING);
}

void sgUpdateSpeed() {
  static uint32_t lastMs = 0;
  const uint32_t nowMs = millis();
  if (nowMs - lastMs < 30) return; // ปรับรอบเวลาการคำนวณความเร็วล้อจาก 50ms เป็น 30ms
  lastMs = nowMs;

  if (sgGauge.ecuConnected && sgGauge.ecuType == ECU_TYPE_YAMAHA &&
      sgEcuSpeedValid && (nowMs - sgEcuSpeedMs) < SG_ECU_SPEED_FRESH_MS) {
    return;
  }

  uint32_t lastUs, periodUs;
  portENTER_CRITICAL(&sgSpeedMux);
  lastUs = sgSpLastUs;
  periodUs = sgSpPeriodUs;
  portEXIT_CRITICAL(&sgSpeedMux);

  const uint32_t sinceUs = micros() - lastUs;
  float kmh = 0.0f;
  if (periodUs != 0 && lastUs != 0 && sinceUs < SPEED_TIMEOUT_US) {
    const float metersPerPulse = WHEEL_CIRCUMFERENCE_M / SPEED_PULSES_PER_REV;
    kmh = (metersPerPulse / ((float)periodUs / 1.0e6f)) * 3.6f;
    if (kmh > 300.0f) kmh = 300.0f;
  }
  sgGauge.speedKmh = kmh;
}

void sgDtcRead() {
  if (!sgGauge.ecuConnected || sgGauge.rpm > 0) {
    DtcDataPacket resultPkt = {};
    resultPkt.packetType = PKT_TYPE_DTC_DATA;
    resultPkt.busy = false;
    resultPkt.count = 0;
    esp_now_send(espnowBroadcastMac, (uint8_t *)&resultPkt, sizeof(resultPkt));
    return;
  }

  sgDtc.busy = true;
  sgKLineFlushRx();

  DtcDataPacket busyPkt = {};
  busyPkt.packetType = PKT_TYPE_DTC_DATA;
  busyPkt.busy = true;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&busyPkt, sizeof(busyPkt));

  sgDtc.count = 0;
  DtcDataPacket resultPkt = {};
  resultPkt.packetType = PKT_TYPE_DTC_DATA;
  resultPkt.busy = false;

  if (sgGauge.ecuType == ECU_TYPE_HONDA) {
    uint8_t cmd[5] = { 0x72, 0x05, 0x74, 0x01, 0x00 };
    cmd[4] = sgChecksum(cmd, 4);
    uint8_t rx[SG_DTC_FRAME_MAX];
    const uint8_t n = sgKLineSendCapture(cmd, sizeof(cmd), rx, sizeof(rx), SG_DTC_TIMEOUT_MS);

    // รองรับทั้งวงจรที่มี Echo (offset = sizeof(cmd)) และไม่มี Echo (offset = 0)
    uint8_t replyOff = 0;
    bool validReply = false;
    if (sgHondaFrameAt(rx, n, sizeof(cmd))) {
      replyOff = sizeof(cmd);
      validReply = true;
    } else if (sgHondaFrameAt(rx, n, 0)) {
      replyOff = 0;
      validReply = true;
    }

    if (validReply) {
      const uint8_t *payload = rx + replyOff;
      const uint8_t payloadLen = n - replyOff;

      for (uint8_t pos = 3; pos <= 7; pos += 2) {
        if ((uint16_t)pos + 1 >= payloadLen) break;
        const uint8_t dtcMain = payload[pos];
        const uint8_t dtcSub = payload[pos + 1];

        if (dtcMain != 0 && dtcMain != 0xFF && sgDtc.count < 12) {
          resultPkt.items[sgDtc.count].mainCode = dtcMain;
          resultPkt.items[sgDtc.count].subCode = dtcSub;
          sgDtc.count++;
        }
      }
    }
  } else if (sgGauge.ecuType == ECU_TYPE_YAMAHA) {
    if ((sgYamErr & 0x80) && sgDtc.count < 12) {
      resultPkt.items[sgDtc.count].mainCode = sgYamErr & 0x7F;
      resultPkt.items[sgDtc.count].subCode = 0;
      sgDtc.count++;
    }
  }

  resultPkt.count = sgDtc.count;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&resultPkt, sizeof(resultPkt));
  sgDtc.busy = false;
}

void sgDtcClear() {
  if (!sgGauge.ecuConnected || sgGauge.rpm > 0) {
    DtcDataPacket resultPkt = {};
    resultPkt.packetType = PKT_TYPE_DTC_DATA;
    resultPkt.busy = false;
    resultPkt.count = 0;
    esp_now_send(espnowBroadcastMac, (uint8_t *)&resultPkt, sizeof(resultPkt));
    return;
  }

  sgDtc.busy = true;
  sgKLineFlushRx();

  if (sgGauge.ecuType == ECU_TYPE_HONDA) {
    uint8_t cmd[5] = { 0x72, 0x05, 0x60, 0x03, 0x00 };
    cmd[4] = sgChecksum(cmd, 4);
    uint8_t rx[16];
    sgKLineSendCapture(cmd, sizeof(cmd), rx, sizeof(rx), SG_DTC_TIMEOUT_MS);
  }

  sgDtc.busy = false;
  DtcDataPacket resultPkt = {};
  resultPkt.packetType = PKT_TYPE_DTC_DATA;
  resultPkt.busy = false;
  resultPkt.count = 0;
  esp_now_send(espnowBroadcastMac, (uint8_t *)&resultPkt, sizeof(resultPkt));
}

uint32_t sgGaugeSeq = 0;

void sgSendGaugeDataNow() {
  GaugeDataPacket pkt = {};
  pkt.packetType    = PKT_TYPE_GAUGE_DATA;
  pkt.ecuType       = sgGauge.ecuType;
  pkt.rpm           = sgGauge.rpm;
  pkt.speedKmh      = sgGauge.speedKmh;
  pkt.tps           = sgGauge.tps;
  pkt.tpsVolt       = sgGauge.tpsVolt;
  pkt.tempC         = sgGauge.tempC;
  pkt.batteryV      = sgGauge.batteryV;
  pkt.injectorMs    = sgGauge.injectorMs;
  pkt.ignitionDeg   = sgGauge.ignitionDeg;
  pkt.o2Volt        = sgGauge.o2Volt;
  pkt.gear          = sgGauge.gear;         
  pkt.ecuConnected  = sgGauge.ecuConnected;
  pkt.t17Ok         = sgGauge.t17Ok;
  pkt.t20Ok         = sgGauge.t20Ok;
  pkt.senderCpuLoad = sgGauge.senderCpuLoad;
  pkt.senderCpuTemp = sgGauge.senderCpuTemp;
  pkt.seq           = sgGaugeSeq++;

  esp_now_send(espnowBroadcastMac, (uint8_t *)&pkt, sizeof(pkt));
}

void sgSendGaugeDataPeriodic() {
  static uint32_t lastSendMs = 0;
  const uint32_t now = millis();
  if (now - lastSendMs >= 16) { // ปรับช่วงเวลารันสำรองจาก 33ms เป็น 16ms (~60 Hz)
    lastSendMs = now;
    sgSendGaugeDataNow();
  }
}

void sgEngineTask() {
  const uint32_t now = millis();
  sgEnsureConnected();
  if (!sgGauge.ecuConnected) return;
  if (SG_KLINE_GAP_MS > 0 && (now - sgLastKLineCmdDoneMs < SG_KLINE_GAP_MS)) return;

  if (sgGauge.ecuType == ECU_TYPE_YAMAHA) {
    if (!sgEcm.valid) {
      snprintf(sgEcm.text, sizeof(sgEcm.text), "YAMAHA-ECU");
      sgEcm.valid = true;
      sgSendEcmIdNow();
    }
  } else if (!sgEcm.valid && sgEcm.tries < SG_ECM_MAX_TRIES) {
    sgEcm.tries++;
    if (sgReadEcmId()) sgSendEcmIdNow();
    sgLastKLineCmdDoneMs = millis();
    return;
  }

  if (sgGauge.ecuType == ECU_TYPE_HONDA) {
    // อ่าน Table 17 (RPM/TPS) 10 รอบ แล้วอ่าน Table 20 (O2) 1 รอบ
    if (sgPollStep < 10) {
      if (sgReadTable17()) sgSendGaugeDataNow(); // ยิงแพ็กเกจใหม่เข้าจอทันทีเมื่อได้ค่า
    } else {
      if (sgReadTable20()) sgSendGaugeDataNow();
    }
    sgPollStep = (sgPollStep + 1) % 11;
  } 
  else if (sgGauge.ecuType == ECU_TYPE_YAMAHA) {
    if (sgReadYamahaStream()) sgSendGaugeDataNow();
  }
  
  sgLastKLineCmdDoneMs = millis();
}

void sgUpdateCpuLoad(uint32_t activeUs) {
  static uint32_t accumulatedActiveUs = 0;
  static uint32_t lastCheckMs = 0;
  
  accumulatedActiveUs += activeUs;
  const uint32_t now = millis();
  const uint32_t elapsedMs = now - lastCheckMs;
  
  if (elapsedMs >= 500) {
    const uint32_t totalUs = elapsedMs * 1000;
    if (totalUs > 0) {
      uint32_t load = (accumulatedActiveUs * 100) / totalUs;
      if (load > 100) load = 100;
      sgGauge.senderCpuLoad = (uint8_t)load;
      sgGauge.senderCpuTemp = sgReadCpuTemperature();
    }
    accumulatedActiveUs = 0;
    lastCheckMs = now;
  }
}

#define STATUS_LED_PIN 48
volatile uint32_t sgLastHeartbeatMs = 0;

void sgBrandLoad() {
  Preferences pr;
  pr.begin("sender", true);
  const uint8_t b = pr.getUChar("brand", ECU_TYPE_NONE);
  pr.end();
  sgBrand = (b == ECU_TYPE_HONDA || b == ECU_TYPE_YAMAHA) ? b : ECU_TYPE_NONE;
}

void sgBrandSaveIfNeeded() {            
  if (sgBrandToSave == 0xFF) return;
  Preferences pr;
  pr.begin("sender", false);
  pr.putUChar("brand", sgBrandToSave);
  pr.end();
  sgBrandToSave = 0xFF;
}

void sgLedSetColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(STATUS_LED_PIN, r, g, b);
}

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
void sgOnEspNowRecv(const esp_now_recv_info_t *recvInfo, const uint8_t *incomingData, int len) {
#else
void sgOnEspNowRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
#endif
  if (len == sizeof(HeartbeatPacket)) {
    HeartbeatPacket hb;
    memcpy(&hb, incomingData, sizeof(hb));
    if (hb.packetType == PKT_TYPE_HEARTBEAT) sgLastHeartbeatMs = millis();
  } 
  else if (len == sizeof(EcuSelectPacket)) {
    EcuSelectPacket sel;
    memcpy(&sel, incomingData, sizeof(sel));
    if (sel.packetType == PKT_TYPE_ECU_SELECT && sel.magic == 0x5A && sel.chk == (uint8_t)(sel.brand ^ 0xA5) &&
        (sel.brand == ECU_TYPE_HONDA || sel.brand == ECU_TYPE_YAMAHA || sel.brand == ECU_TYPE_NONE)) {
      if (sel.brand != sgBrand) {
        sgBrand = sel.brand;
        sgBrandToSave = sel.brand;
        sgBrandChanged = true;
      }
    }
  }
  else if (len == sizeof(DtcCommandPacket)) {
    DtcCommandPacket dtcCmd;
    memcpy(&dtcCmd, incomingData, sizeof(dtcCmd));
    if (dtcCmd.packetType == PKT_TYPE_DTC_CMD) {
      sgPendingDtcCmd = dtcCmd.cmd;
    }
  }
}

void sgHandlePendingDtc() {
  if (sgPendingDtcCmd == 0x01) {
    sgPendingDtcCmd = 0;
    sgDtcRead();
  } else if (sgPendingDtcCmd == 0x02) {
    sgPendingDtcCmd = 0;
    sgDtcClear();
  }
}

void sgEspNowSetup() {
  sgLedSetColor(0, 0, 0);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  WiFi.setTxPower(WIFI_POWER_11dBm);
  esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(sgOnEspNowRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, espnowBroadcastMac, 6);
  peerInfo.channel = ESPNOW_WIFI_CHANNEL;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
}

void sgUpdateStatusLed() {
  const bool displayLinkOk = (millis() - sgLastHeartbeatMs) < 3000;
  const bool ecuOk = sgGauge.ecuConnected;

  int currentState = 0;
  if (ecuOk && displayLinkOk) {
    currentState = (sgGauge.ecuType == ECU_TYPE_YAMAHA) ? 3 : 2; 
  } else if (ecuOk && !displayLinkOk) {
    currentState = 1;
  } else {
    currentState = ((millis() / 300) % 2 == 0) ? 0 : -1;
  }

  static int lastState = -99;
  if (currentState != lastState) {
    lastState = currentState;
    switch (currentState) {
      case 3:  sgLedSetColor(0, 5, 5);   break; 
      case 2:  sgLedSetColor(0, 5, 0);   break; 
      case 1:  sgLedSetColor(0, 0, 5);   break; 
      case 0:  sgLedSetColor(5, 1, 0);   break; 
      default: sgLedSetColor(0, 0, 0);   break;
    }
  }
}

void setup() {
  setCpuFrequencyMhz(240); 
  Serial.begin(115200);

  pinMode(GEAR_N_PIN, INPUT_PULLDOWN);
  pinMode(GEAR_1_PIN, INPUT_PULLDOWN);
  pinMode(GEAR_2_PIN, INPUT_PULLDOWN);
  pinMode(GEAR_3_PIN, INPUT_PULLDOWN);
  pinMode(GEAR_4_PIN, INPUT_PULLDOWN);

  memset(&sgGauge, 0, sizeof(sgGauge));
  sgGauge.gear = 255;
  sgLastRawGear = 255;
  sgLastGearChangeMs = millis();
  sgBrandLoad();
  sgSpeedBegin();
  sgEspNowSetup();
}

void loop() {
  const uint32_t tStart = micros();

  // ตรวจจับการเปลี่ยนเกียร์พร้อม Debounce 30ms ก่อนยิงสัญญาณออก
  const uint8_t rawGear = sgReadGear();
  if (rawGear != sgLastRawGear) {
    sgLastRawGear = rawGear;
    sgLastGearChangeMs = millis();
  } else if (rawGear != sgGauge.gear && (millis() - sgLastGearChangeMs) >= SG_GEAR_DEBOUNCE_MS) {
    sgGauge.gear = rawGear;
    sgSendGaugeDataNow(); 
  }

  sgBrandSaveIfNeeded();
  if (sgBrandChanged) {                       
    sgBrandChanged = false;
    sgScheduleReconnect("BRAND CHANGE");
  }

  sgUpdateSpeed();
  sgEngineTask();               
  sgSendGaugeDataPeriodic();    
  sgSendEcmIdPeriodic();
  sgUpdateStatusLed();
  sgHandlePendingDtc();

  if (Serial.available()) {
    const char cmd = (char)Serial.read();
    if (cmd == 'r' || cmd == 'R') sgDtcRead();
    else if (cmd == 'c' || cmd == 'C') sgDtcClear();
  }

  const uint32_t tActive = micros() - tStart;
  sgUpdateCpuLoad(tActive);

  yield(); // เปลี่ยนจาก delay(1) เพื่อลด Micro-delay สะดุดในลูปประมวลผล K-Line
}