#ifndef SHARED_DATA_STRUCT_H
#define SHARED_DATA_STRUCT_H

// IMPORTANT: this file must be IDENTICAL in both sketch folders (sender_ecu and display_receiver_s3).

#include <Arduino.h>

#define PKT_TYPE_GAUGE_DATA 0x01
#define PKT_TYPE_HEARTBEAT  0x02
#define PKT_TYPE_DTC_DATA   0x03
#define PKT_TYPE_DTC_CMD    0x04
#define PKT_TYPE_ECM_ID     0x05   
#define PKT_TYPE_ECU_SELECT 0x06   // display -> sender: which ECU brand to scan

#define ESPNOW_WIFI_CHANNEL 1

// ---- ECU Brand Types ----
#define ECU_TYPE_NONE   0
#define ECU_TYPE_HONDA  1
#define ECU_TYPE_YAMAHA 2

// ---- Sent from Sender to Display ----
typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_GAUGE_DATA
  uint8_t  ecuType;        // 0=None, 1=Honda Keihin, 2=Yamaha K-Line
  uint16_t rpm;
  float    speedKmh;       // wheel speed (km/h)
  float    tps;
  float    tpsVolt;
  int16_t  tempC;
  float    batteryV;
  float    injectorMs;
  float    ignitionDeg;
  float    o2Volt;
  uint8_t  gear;
  bool     ecuConnected;
  bool     t17Ok;
  bool     t20Ok;
  uint8_t  senderCpuLoad;  // sender CPU load (0-100%)
  int8_t   senderCpuTemp;  // sender CPU temperature (C)
  uint32_t seq;
} GaugeDataPacket;

// ---- Sent from Display to Sender ----
typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_HEARTBEAT
  uint32_t seq;
} HeartbeatPacket;

typedef struct __attribute__((packed)) {
  uint8_t mainCode;
  uint8_t subCode;
} DtcItem;

typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_DTC_DATA
  uint8_t  count;
  DtcItem  items[12];
  bool     busy;
} DtcDataPacket;

typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_DTC_CMD
  uint8_t  cmd;
} DtcCommandPacket;

// ---- ECM ID ----
typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_ECM_ID
  bool     valid;          // true once ECM ID read
  char     text[20];       // NUL-terminated hex string
} EcmIdPacket;

// ---- Brand selection (Display -> Sender) ----
// brand: ECU_TYPE_HONDA / ECU_TYPE_YAMAHA / ECU_TYPE_NONE (= not chosen, scan both).
// chk = brand ^ 0xA5 so a stray packet cannot switch the brand by accident.
typedef struct __attribute__((packed)) {
  uint8_t  packetType;     // = PKT_TYPE_ECU_SELECT
  uint8_t  brand;
  uint8_t  chk;
  uint8_t  magic;          // always 0x5A
} EcuSelectPacket;

#endif