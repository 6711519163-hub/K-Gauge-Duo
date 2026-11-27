/*
  ui_core.h - LVGL 8.3 helpers: palette, ONE string table (English + Thai), widget builders, top bar
  (back / menu / link chip / shift lights / brand / clock), bottom bar (sport mode / ODO / trip),
  popup menu with the 6 s admin hold, numeric keypad, toast, over-rev flash overlay, gear change effect.

  Speed rules used everywhere: only opaque rectangles + text, every setter compares before it touches LVGL
  (a style setter repaints the widget even when the value did not change).
*/
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LV_FONT_DECLARE(font_thai_15);
LV_FONT_DECLARE(font_thai_18);
LV_FONT_DECLARE(font_thai_12);

#define SW    480
#define SH    272
#define TOPH  22          // top bar height
#define BOTY  248         // bottom bar top (home page only)

// ============================================================ palette (navy "cyber" theme)
struct Pal {
  uint32_t bg, top, panel, panel2, border, text, dim, track, mint, orange, pink, lime, cyan,
           amber, red, green, violet, yellow, ring, redzone;
};
//   dark theme = deep black, frame lines only (panel == bg), bright saturated accents
//                              bg        top       panel     panel2    border    text      dim       track     mint      orange
static const Pal PAL_DARK  = { 0x000000, 0x000000, 0x000000, 0x0B1424, 0x2E6FA8, 0xFFFFFF, 0xB4C3DB, 0x1A2B45, 0x00FFC6, 0xFF9F1A,
//                              pink      lime      cyan      amber     red       green     violet    yellow    ring      redzone
                               0xFF3DA5, 0xB6FF00, 0x00E8FF, 0xFFD000, 0xFF2D46, 0x2BFF7A, 0xC58BFF, 0xFFEE00, 0x000000, 0x3A0A12 };
static const Pal PAL_LIGHT = { 0xEEF1F5, 0xD3DAE2, 0xFFFFFF, 0xE3EAF1, 0x9FB3CC, 0x0B1220, 0x4B5A73, 0xD5DFE8, 0x0F9D84, 0xD9680B,
                               0xC4146A, 0x6C8A00, 0x0070C0, 0xB07A00, 0xD01E1E, 0x1B7F3A, 0x7A2FE0, 0x8F7500, 0xF8FAFC, 0xF3C9CB };
static const Pal *g_pal = &PAL_DARK;
#define PCOL(f) (g_pal->f)

// ============================================================ strings: EVERY visible word is here, English + Thai
// Rule: the Thai column is natural Thai. Only real proper nouns / protocol names stay in Latin
// (ESP-NOW, K-LINE, ECM, DTC, O2, AFR, TPS). Add a line here and S(S_NAME) gives the right language.
#define STR_LIST(X) \
  X(MENU,        "MENU", "MENU") \
  X(BACK,        "BACK", "ย้อนกลับ") \
  X(ONLINE,      "ONLINE", "ONLINE") \
  X(CONNECTING,  "CONNECTING", "กำลังเชื่อมต่อ") \
  X(OFFLINE,     "OFFLINE", "OFFLINE") \
  X(SPORT_MODE,  "SPORT MODE", "SPORT MODE") \
  X(SPORT_ON,    "Sport mode ON - shift alert enabled", "SPORT MODE: เปิด - เตือนรอบเกินทำงาน") \
  X(SPORT_OFF,   "Sport mode OFF - shift alert muted", "SPORT MODE: ปิด - ปิดเตือนรอบเกิน") \
  X(ODO,         "ODO", "ระยะรวม") \
  X(TRIP_A,      "TRIP A", "TRIP A") \
  X(TRIP_B,      "TRIP B", "TRIP B") \
  X(KM,          "km", "กม.") \
  X(KMH,         "km/h", "กม./ชม.") \
  X(HINT_TRIP,   "Hold the reset button to clear the trip", "กดปุ่มวงกลมค้างไว้เพื่อล้าง TRIP") \
  X(TRIP_A_RST,  "Trip A reset", "ล้าง TRIP A แล้ว") \
  X(TRIP_B_RST,  "Trip B reset", "ล้าง TRIP B แล้ว") \
  X(RPM,         "RPM", "รอบเครื่อง") \
  X(GEAR,        "GEAR", "GEAR") \
  X(SPEED,       "SPEED", "ความเร็ว") \
  X(PEAK_RPM,    "PEAK RPM", "รอบสูงสุด") \
  X(PEAK_RST,    "Peak RPM reset", "ล้างรอบสูงสุดแล้ว") \
  X(REDLINE,     "REDLINE", "เส้นแดง") \
  X(SHIFT_UP,    "SHIFT UP", "SHIFT UP") \
  X(TILE_AFR,    "AFR", "AFR") \
  X(TILE_BATT,   "BATTERY", "BATTERY") \
  X(TILE_COOL,   "COOLANT", "น้ำหล่อเย็น") \
  X(TILE_INJ,    "INJ DUTY", "หัวฉีด") \
  X(TILE_IGN,    "IGN ADV", "จุดระเบิด") \
  X(TILE_O2,     "O2 SENSOR", "O2 SENSOR") \
  X(B_STOICH,    "STOICH", "สมดุล") \
  X(B_RICH,      "RICH", "หนา") \
  X(B_LEAN,      "LEAN", "บาง") \
  X(DATA_TITLE,  "ALL ECU PARAMETERS", "ข้อมูล ECU ทั้งหมด") \
  X(D_RPM,       "ENGINE SPEED", "ความเร็วรอบเครื่อง") \
  X(D_SPD_RAW,   "SPEED (RAW)", "ความเร็วรถ (ค่าดิบ)") \
  X(D_SPD_CAL,   "SPEED (CALIB.)", "ความเร็วรถ (ปรับเทียบ)") \
  X(D_GEAR,      "GEAR", "GEAR") \
  X(D_AFR,       "AIR/FUEL (AFR)", "อากาศ/เชื้อเพลิง (AFR)") \
  X(D_TPS,       "THROTTLE (TPS)", "ตำแหน่งคันเร่ง (TPS)") \
  X(D_TPSV,      "TPS VOLTAGE", "แรงดันไฟ TPS") \
  X(D_COOL,      "COOLANT TEMP", "อุณหภูมิน้ำหล่อเย็น") \
  X(D_BATT,      "BATTERY VOLTAGE", "แรงดัน BATTERY") \
  X(D_INJ,       "INJECTION TIME", "เวลาฉีดเชื้อเพลิง") \
  X(D_IGN,       "IGNITION ADVANCE", "องศาจุดระเบิด") \
  X(D_O2,        "O2 SENSOR (AFR)", "O2 SENSOR (AFR)") \
  X(D_CPU,       "TX CPU LOAD/TEMP", "CPU ตัวส่ง (ใช้งาน/°C)") \
  X(D_LINK,      "DATA LINK", "ช่องทางรับข้อมูล") \
  X(LINK_ESPNOW, "ESP-NOW CH %d", "ESP-NOW ช่อง %d") \
  X(LINK_KLINE,  "K-LINE DIRECT", "K-LINE ต่อตรง") \
  X(DTC_TITLE,   "DIAGNOSTIC TROUBLE CODES (DTC)", "รหัสข้อผิดพลาดจากการวินิจฉัย (DTC)") \
  X(DTC_READING, "Reading codes...", "กำลังอ่านรหัส...") \
  X(DTC_CLEARING,"Clearing codes...", "กำลังลบรหัส...") \
  X(DTC_PROC,    "Processing...", "กำลังประมวลผล...") \
  X(DTC_ENGON,   "Engine is running!", "เครื่องยนต์ยังทำงานอยู่!") \
  X(DTC_ENGON_S, "Stop the engine before clearing codes", "ดับเครื่องยนต์ก่อนลบรหัส") \
  X(DTC_TIMEOUT, "No response from the ECU", "ไม่มีการตอบกลับจาก ECU") \
  X(DTC_TIMEOUT_S,"Check the connection and try again", "ตรวจสอบการเชื่อมต่อแล้วลองใหม่") \
  X(DTC_NONE,    "No fault codes stored", "ไม่พบรหัสข้อผิดพลาด") \
  X(DTC_FOUND,   "%u fault code(s) stored in the ECM memory", "พบรหัสข้อผิดพลาด %u รหัสในหน่วยความจำ ECM") \
  X(DTC_TAG,     "%u FAULT(S)", "%u รหัส") \
  X(DTC_IDLE,    "Press READ CODES to check", "กดอ่านรหัสเพื่อตรวจสอบ") \
  X(DTC_IDLE_S,  "Stop the engine before reading", "ควรดับเครื่องยนต์ก่อนอ่านรหัส") \
  X(BTN_READ,    "READ CODES", "อ่านรหัส") \
  X(BTN_CLEAR,   "CLEAR CODES", "ลบรหัส") \
  X(DTC_ACTIVE,  "ACTIVE", "พบอยู่") \
  X(DTC_CODE,    "Code", "รหัส") \
  X(DTC_UNKNOWN, "Unknown fault", "ไม่ทราบความหมายของรหัส") \
  X(DTC_LIST,    "CODE LIST", "รายการรหัส") \
  X(PG_HOME,     "HOME", "หน้าหลัก") \
  X(PG_DATA,     "LIVE DATA", "ข้อมูลสด") \
  X(PG_GRAPH,    "GRAPH", "GRAPH") \
  X(PG_DTC,      "FAULT CODES", "รหัสข้อผิดพลาด") \
  X(MENU_PAGES,  "PAGES", "หน้าจอ") \
  X(MENU_BRAND,  "ECU BRAND", "ยี่ห้อ ECU") \
  X(MENU_THEME,  "THEME", "THEME") \
  X(MENU_LANG,   "LANGUAGE", "ภาษา") \
  X(THEME_DARK,  "DARK", "มืด") \
  X(THEME_LIGHT, "LIGHT", "สว่าง") \
  X(CLOSE,       "CLOSE", "ปิด") \
  X(HOLD_HINT,   "Hold HOME %d / 6 s", "กดค้างหน้าหลัก %d / 6 วินาที") \
  X(BRAND_HONDA, "HONDA", "HONDA") \
  X(BRAND_YAMAHA,"YAMAHA", "YAMAHA") \
  X(OK,          "OK", "ตกลง") \
  X(CANCEL,      "CANCEL", "ยกเลิก") \
  X(CLEAR_KEY,   "CLEAR", "ล้าง") \
  X(OVERREV,     "OVER-REV", "รอบเกิน") \
  X(SIG_RPM,     "RPM", "รอบ") \
  X(SIG_SPEED,   "SPEED", "ความเร็ว") \
  X(SIG_TPS,     "TPS", "คันเร่ง") \
  X(SIG_TEMP,    "COOLANT", "น้ำหล่อเย็น") \
  X(SIG_BATT,    "BATT", "BATT") \
  X(SIG_INJ,     "INJ", "หัวฉีด") \
  X(SIG_IGN,     "IGN", "จุดระเบิด") \
  X(SIG_AFR,     "AFR", "AFR") \
  X(SIGNALS,     "SIGNALS", "สัญญาณ") \
  X(SEL_SIGNALS, "SELECT SIGNALS", "เลือกสัญญาณที่จะแสดง") \
  X(CLEAR,       "CLEAR", "ล้าง") \
  X(SEL_TITLE,   "Select ECU brand", "เลือกยี่ห้อ ECU") \
  X(SEL_SUB,     "Choose the bike connected to the K-Line (change it later in the menu)", "เลือกยี่ห้อรถที่ต่อกับสาย K-Line (เปลี่ยนภายหลังได้ในเมนู)") \
  X(SEL_H1,      "RPM  TPS  Coolant  Battery", "รอบ  คันเร่ง  น้ำหล่อเย็น  BATTERY") \
  X(SEL_H2,      "Injector  Ignition  O2  AFR", "หัวฉีด  จุดระเบิด  O2  AFR") \
  X(SEL_H3,      "Speed  Gear  Fault codes", "ความเร็ว  GEAR  รหัสข้อผิดพลาด") \
  X(SEL_Y1,      "RPM  Wheel speed  Coolant", "รอบ  ความเร็วล้อ  น้ำหล่อเย็น") \
  X(SEL_Y2,      "Fault codes (live)", "รหัสข้อผิดพลาด (อ่านสด)") \
  X(SEL_Y3,      "More values coming", "จะเพิ่มค่าอื่นเร็วๆ นี้") \
  X(SEL_BTN,     "SELECT", "เลือก") \
  X(SEL_FOOT,    "Tap a card to choose - remembered after restart", "แตะการ์ดเพื่อเลือก (จำค่าไว้แม้ปิดเครื่อง)") \
  X(PIN_TITLE,   "Enter admin PIN", "ใส่รหัสผู้ดูแลระบบ") \
  X(PIN_SUB,     "Enter the 6-digit PIN", "ใส่รหัส 6 หลักเพื่อเข้าโหมดผู้ดูแล") \
  X(PIN_WRONG,   "Wrong PIN", "รหัสไม่ถูกต้อง") \
  X(PIN_HINT,    "Default PIN: 202030", "รหัสเริ่มต้น: 202030") \
  X(ADMIN_MODE,  "ADMIN MODE", "ผู้ดูแลระบบ") \
  X(TAB_GENERAL, "GENERAL", "ทั่วไป") \
  X(TAB_RPM,     "RPM ALERT", "เตือนรอบ") \
  X(TAB_CLOCK,   "CLOCK", "นาฬิกา") \
  X(A_ODO_TITLE, "TOTAL DISTANCE (ODO)", "ระยะทางรวม (ODO)") \
  X(A_SET_ODO,   "SET ODO", "ตั้งระยะทางรวม") \
  X(A_RST_TRIPS, "RESET TRIPS", "ล้าง TRIP A/B") \
  X(A_CAL_TITLE, "SPEED CALIBRATION", "ปรับเทียบความเร็ว") \
  X(A_READING,   "Reading", "ค่าที่อ่านได้") \
  X(A_CAL_HELP,  "Ride at a steady speed, then enter the real speed (speedometer / GPS) and the correction is calculated for you.", "ขับด้วยความเร็วคงที่ แล้วกรอกความเร็วจริงจากมาตรวัดหรือ GPS ระบบจะคำนวณค่าปรับให้เอง") \
  X(A_ENTER_REAL,"ENTER REAL SPEED", "กรอกความเร็วจริง") \
  X(A_MAX_TITLE, "GAUGE MAX RPM", "รอบสูงสุดของมาตรวัด") \
  X(A_MODE_TITLE,"OVER-REV WARNING MODE", "วิธีเตือนรอบเกิน") \
  X(A_PER_GEAR,  "PER GEAR", "แยกตามเกียร์") \
  X(A_RPM_ONLY,  "RPM ONLY", "รอบเดียวทุกเกียร์") \
  X(A_ONLY_NOTE, "RPM ONLY ignores the gear", "ใช้รอบเดียวกับทุก GEAR") \
  X(A_GEAR_WARN, "Warning RPM for each gear", "รอบเตือนของแต่ละ GEAR") \
  X(A_ALL_WARN,  "Warning RPM (all gears)", "รอบเตือน (ทุก GEAR)") \
  X(A_ALL_NOTE,  "Above this RPM the screen flashes red and the alarm sounds", "เมื่อรอบเกินค่านี้ หน้าจอจะกะพริบแดงและมีเสียงเตือน") \
  X(A_GEAR_N,    "Gear", "GEAR") \
  X(A_SAVE,      "SAVE", "บันทึก") \
  X(A_EXIT,      "EXIT", "ออก") \
  X(A_TEST,      "TEST FLASH", "ทดสอบการกะพริบ") \
  X(A_ODO_OK,    "ODO updated (press SAVE)", "ตั้งระยะทางรวมแล้ว (กดบันทึก)") \
  X(A_TOO_SLOW,  "Too slow - ride at a steady speed", "ช้าเกินไป - ขับด้วยความเร็วคงที่") \
  X(A_CAL_OK,    "Speed calibrated (press SAVE)", "ปรับเทียบความเร็วแล้ว (กดบันทึก)") \
  X(A_TRIPS_OK,  "Trips reset (press SAVE)", "ล้าง TRIP แล้ว (กดบันทึก)") \
  X(A_NUM_ODO,   "Set ODO (km)", "ตั้งระยะทางรวม (กม.)") \
  X(A_NUM_SPD,   "Real speed (km/h)", "ความเร็วจริง (กม./ชม.)") \
  X(A_CLK_TITLE, "SET THE CLOCK", "ตั้งเวลานาฬิกา") \
  X(A_HOUR,      "HOUR", "ชั่วโมง") \
  X(A_MIN,       "MINUTE", "นาที") \
  X(A_CLK_NOTE,  "There is no clock battery: set the time after every power-up.", "นาฬิกาไม่มีแบตสำรอง ต้องตั้งเวลาใหม่ทุกครั้งที่เปิดเครื่อง") \
  X(TAB_LED,     "LED", "LED") \
  X(ON,          "ON", "เปิด") \
  X(OFF,         "OFF", "ปิด") \
  X(LED_TITLE,   "LED SHIFT LIGHTS", "ไฟ LED เตือนรอบ") \
  X(LED_ENABLE,  "LED", "LED") \
  X(LED_BRIGHT,  "BRIGHTNESS (%)", "ความสว่าง (%)") \
  X(LED_START,   "START AT (% OF REDLINE)", "เริ่มติดที่ (% ของเส้นแดง)") \
  X(LED_FLASH,   "FLASH", "กะพริบ") \
  X(LED_REV,     "REVERSE R", "กลับด้านขวา") \
  X(LED_COLORS,  "COLOUR OF EACH LED (BOTH SIDES)", "สีของแต่ละหลอด (ทั้งสองข้าง)") \
  X(LED_HINT,    "Tap a LED, then tap a colour", "แตะหลอด แล้วแตะสีที่ต้องการ") \
  X(LED_GRAD,    "GRADIENT", "ไล่สี") \
  X(LED_TEST,    "TEST", "ทดสอบ") \
  X(LED_PIN,     "Data pin: GPIO %d", "ขาสัญญาณ: GPIO %d") \
  X(LED_PIN_NONE,"Data pin: NOT SET (edit WS2812_PIN)", "ขาสัญญาณ: ยังไม่ได้ตั้ง (แก้ WS2812_PIN)") \
  X(PG_FUEL,     "FUEL MAP", "FUEL MAP") \
  X(FM_TAB_RT,   "RPM x TPS", "RPM x TPS") \
  X(FM_TAB_SG,   "SPD x GEAR", "SPD x GEAR") \
  X(FM_TAB_GS,   "G + SPD", "G + SPD") \
  X(FM_AUTO,     "AUTO TUNE", "AUTO TUNE") \
  X(FM_WARM,     "T>75\xC2\xB0", "T>75\xC2\xB0") \
  X(FM_CELL,     "CELL", "ช่อง") \
  X(FM_ACTUAL,   "ACTUAL AFR", "AFR จริง") \
  X(FM_TARGET,   "TARGET", "เป้า") \
  X(FM_TRIM,     "MAP TRIM", "MAP TRIM") \
  X(FM_SETTGT,   "SET TARGET", "จูนเป้า") \
  X(FM_OK,       "OK", "พอดี") \
  X(FM_RESET,    "RESET", "ล้าง") \
  X(FM_RESET_HINT,"Hold RESET to clear the learned map", "กดปุ่มล้างค้างไว้เพื่อลบข้อมูลที่เรียนรู้") \
  X(FM_RESET_DONE,"Learned map cleared", "ล้างข้อมูลที่เรียนรู้แล้ว") \
  X(FM_TGT_SET,  "Target set to %.1f", "ตั้งเป้าเป็น %.1f แล้ว") \
  X(FM_NODATA,   "CELL %s: no data yet - ride to learn", "ช่อง %s: ยังไม่มีข้อมูล - ขับเพื่อเรียนรู้") \
  X(FM_LEAN_MSG, "CELL %s: LEAN (%.1f) -> add +%d%% or SET TARGET", "ช่อง %s: บาง (%.1f) -> เพิ่ม +%d%% หรือจูนเป้า") \
  X(FM_RICH_MSG, "CELL %s: RICH (%.1f) -> cut %d%% or SET TARGET", "ช่อง %s: หนา (%.1f) -> ลด %d%% หรือจูนเป้า") \
  X(FM_OK_MSG,   "CELL %s: OK (%.1f), target %.1f", "ช่อง %s: พอดี (%.1f) เป้า %.1f") \
  X(FM_PAUSED,   "Learning paused: engine below 75\xC2\xB0" "C", "หยุดเรียนรู้: เครื่องยังไม่ถึง 75\xC2\xB0" "C") \
  X(FM_OFF,      "AUTO TUNE is off - nothing is learned", "AUTO TUNE ปิดอยู่ - ไม่เรียนรู้ค่าใหม่") \
  X(FA_1,        "1. AIR/FUEL MIX", "1. ส่วนผสม AFR") \
  X(FA_2,        "2. FUEL ADVICE", "2. แนะนำปรับน้ำมัน") \
  X(FA_3,        "3. ECONOMY", "3. อัตราประหยัด") \
  X(FA_TGT,      "target %.1f", "เป้า %.1f") \
  X(FA_INJ,      "INJ %.1f -> %.1f ms", "หัวฉีด %.1f -> %.1f ms") \
  X(FA_KPL,      "km/L", "กม./ลิตร") \
  X(FA_LPH,      "%.1f L/h", "%.1f ลิตร/ชม.") \
  X(FA_SETINJ,   "set injector flow in admin", "ตั้งค่าหัวฉีดในโหมดผู้ดูแล") \
  X(TAB_FUEL,    "FUEL", "เชื้อเพลิง") \
  X(A_INJ_TITLE, "INJECTOR (FOR km/L)", "หัวฉีด (คำนวณ กม./ลิตร)") \
  X(A_INJ_CC,    "FLOW (cc/min)", "อัตราไหล (cc/นาที)") \
  X(A_CYL,       "CYLINDERS", "จำนวนสูบ") \
  X(A_INJ_NOTE,  "0 = km/L hidden. The flow is printed on the injector.", "0 = ไม่แสดง กม./ลิตร ค่านี้ดูได้จากตัวหัวฉีด")

enum StrId : uint16_t {
#define STR_X(id, en, th) S_##id,
  STR_LIST(STR_X)
#undef STR_X
  S__COUNT
};
static const char *const STR_TAB[S__COUNT][2] = {
#define STR_X(id, en, th) { en, th },
  STR_LIST(STR_X)
#undef STR_X
};
static inline const char *S(StrId id) { return STR_TAB[id][g_set.lang ? 1 : 0]; }
static inline const char *T(const char *en, const char *th) { return g_set.lang ? th : en; }   // one-off texts

static inline const lv_font_t *fSm() { return g_set.lang ? &font_thai_12 : &lv_font_montserrat_12; }
static inline const lv_font_t *fMd() { return g_set.lang ? &font_thai_15 : &lv_font_montserrat_14; }
// always the Thai font (it also contains Latin): used where Thai and English words must both be drawable at once
static inline const lv_font_t *fTh() { return &font_thai_15; }
// y shift of a Thai 12 px label compared with a Latin 12 px label (Thai lines are 3 px taller, baseline 2 px lower)
#define TOFF (g_set.lang ? -2 : 0)
static inline const lv_font_t *fLatin(int px) {            // digits / latin only
  switch (px) { case 10: return &lv_font_montserrat_10; case 12: return &lv_font_montserrat_12; case 14: return &lv_font_montserrat_14;
                case 18: return &lv_font_montserrat_18; case 22: return &lv_font_montserrat_22; case 28: return &lv_font_montserrat_28;
                case 36: return &lv_font_montserrat_36; default: return &lv_font_montserrat_48; }
}

// ============================================================ widget helpers
static lv_obj_t *mkBox(lv_obj_t *p, int x, int y, int w, int h, uint32_t bg, int r, uint32_t bd = 0, int bw = 0) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, r, 0);
  if (bw) {
    lv_obj_set_style_border_color(o, lv_color_hex(bd), 0);
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
  }
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t *mkLbl(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col, int x, int y,
                       int w = 0, lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_obj_set_style_text_align(l, al, 0);
  if (w) lv_obj_set_width(l, w);
  lv_obj_set_pos(l, x, y);
  lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
  return l;
}
// label that never wraps and never grows (cuts the text instead)
static lv_obj_t *mkLblClip(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t col, int x, int y,
                           int w, lv_text_align_t al = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *l = mkLbl(p, t, f, col, x, y, w, al);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  return l;
}

static void setTxt(lv_obj_t *l, const char *t) {                // only touches LVGL when the text changed
  if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}
// LVGL 8: every style setter invalidates (= repaints) the widget even when the value is unchanged, so all
// colour setters compare first. Compared as lv_color_t (.full): with 16-bit colour a hex value is rounded
// to RGB565, so comparing against the 24-bit hex would never match.
static void setCol(lv_obj_t *l, uint32_t c) {
  if (l && lv_obj_get_style_text_color(l, LV_PART_MAIN).full != lv_color_hex(c).full)
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
}
static void setBg(lv_obj_t *o, uint32_t c) {
  if (o && lv_obj_get_style_bg_color(o, LV_PART_MAIN).full != lv_color_hex(c).full)
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}
static void setBdr(lv_obj_t *o, uint32_t c) {
  if (o && lv_obj_get_style_border_color(o, LV_PART_MAIN).full != lv_color_hex(c).full)
    lv_obj_set_style_border_color(o, lv_color_hex(c), 0);
}
static void setW(lv_obj_t *o, int w) { if (o && lv_obj_get_width(o) != w) lv_obj_set_width(o, w); }

static void show(lv_obj_t *o, bool v) {
  if (!o) return;
  if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void clickSoundCb(lv_event_t *e) { (void)e; audio_click(); }

// rounded button with centred label; cb gets id as user data
static lv_obj_t *mkBtn(lv_obj_t *p, int x, int y, int w, int h, const char *txt, const lv_font_t *f,
                       uint32_t bg, uint32_t fg, uint32_t outline, lv_event_cb_t cb, intptr_t id) {
  lv_obj_t *b = mkBox(p, x, y, w, h, bg, h / 2 > 14 ? 14 : h / 2, outline, outline ? 2 : 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_t *l = mkLbl(b, txt, f, fg, 0, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, clickSoundCb, LV_EVENT_CLICKED, NULL);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)id);
  return b;
}
static void btnText(lv_obj_t *b, const char *t) { setTxt(lv_obj_get_child(b, 0), t); lv_obj_center(lv_obj_get_child(b, 0)); }

static lv_obj_t *mkScreen() {
  lv_obj_t *s = lv_obj_create(NULL);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, lv_color_hex(PCOL(bg)), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(s, lv_color_hex(PCOL(text)), 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

// card: rounded panel, accent bar next to the title, underline in the accent colour (admin pages)
struct CardRef { lv_obj_t *card, *ul; uint32_t accent; };
static CardRef g_cards[24];
static uint8_t g_cardCount = 0;

static lv_obj_t *mkCard(lv_obj_t *p, int x, int y, int w, int h, const char *title, uint32_t accent) {
  lv_obj_t *c = mkBox(p, x, y, w, h, PCOL(panel), 11, PCOL(border), 1);
  if (title) {
    mkBox(c, 8, 8, 3, 11, accent, 2);
    mkLbl(c, title, fSm(), PCOL(dim), 15, 3);
  }
  lv_obj_t *ul = mkBox(c, 8, h - 7, w - 16, 3, accent, 2);
  if (g_cardCount < 24) g_cards[g_cardCount++] = { c, ul, accent };
  return c;
}
// level: 0 normal, 1 warning (amber), 2 danger (red), 3 cold (cyan)
static uint32_t lvlColor(uint8_t lvl, uint32_t normal) {
  switch (lvl) { case 1: return PCOL(amber); case 2: return PCOL(red); case 3: return PCOL(cyan); default: return normal; }
}

// ============================================================ clock (no RTC on the board: set in admin mode)
static bool     g_clkSet = false;
static uint16_t g_clkBaseMin = 0;
static uint32_t g_clkBaseMs = 0;
static int clockMinutes() {                                  // -1 = not set
  if (!g_clkSet) return -1;
  return (int)((g_clkBaseMin + (millis() - g_clkBaseMs) / 60000UL) % 1440UL);
}
static void clockSet(int hh, int mm) {
  g_clkSet = true;
  g_clkBaseMin = (uint16_t)(((hh % 24 + 24) % 24) * 60 + ((mm % 60 + 60) % 60));
  g_clkBaseMs = millis();
}
static void clockText(char *out, size_t cap) {               // English 3:17 PM, Thai 15:17 น.
  const int m = clockMinutes();
  if (m < 0) { snprintf(out, cap, "--:--"); return; }
  const int h = m / 60, mi = m % 60;
  if (g_set.lang) snprintf(out, cap, "%02d:%02d น.", h, mi);
  else            snprintf(out, cap, "%d:%02d %s", (h % 12) ? (h % 12) : 12, mi, h >= 12 ? "PM" : "AM");
}

// ============================================================ pages / forward decls
static void uiShowPage(int page);
static void menuOpen();
static void menuClose();
static void pinOpen();

// ============================================================ top bar (every page)
// [back] [menu] * page-name                                   | brand | clock      (dot = ECU link state)
#define N_PAGES 5
struct TopBar { lv_obj_t *dot, *brandMark, *brandTxt, *clk, *back; };
static TopBar g_top[N_PAGES];

static void menuBtnCb(lv_event_t *e) { (void)e; menuOpen(); }
static void backCb(lv_event_t *e) { (void)e; if (g_page != 0) uiShowPage(0); }

static void buildTopBar(lv_obj_t *scr, int idx) {
  TopBar &t = g_top[idx];
  mkBox(scr, 0, 0, SW, TOPH, PCOL(top), 0);
  mkBox(scr, 0, TOPH, SW, 1, PCOL(border), 0);

  t.back = mkBox(scr, 6, 3, 24, 16, PCOL(panel), 6, PCOL(border), 1);
  lv_obj_add_flag(t.back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t.back, clickSoundCb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(t.back, backCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *bi = mkLbl(t.back, LV_SYMBOL_LEFT, &lv_font_montserrat_12, idx == 0 ? PCOL(border) : PCOL(text), 0, 0);
  lv_obj_center(bi);

  lv_obj_t *mb = mkBox(scr, 34, 3, 24, 16, PCOL(panel), 6, PCOL(orange), 2);
  lv_obj_add_flag(mb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(mb, clickSoundCb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(mb, menuBtnCb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *ic = mkLbl(mb, LV_SYMBOL_LIST, &lv_font_montserrat_12, PCOL(orange), 0, 0);
  lv_obj_center(ic);

  t.dot = mkBox(scr, 68, 7, 8, 8, PCOL(red), LV_RADIUS_CIRCLE);
  static const StrId pg[N_PAGES] = { S_PG_HOME, S_PG_DATA, S_PG_GRAPH, S_PG_DTC, S_PG_FUEL };
  mkLblClip(scr, S(pg[idx]), fSm(), PCOL(dim), 82, (4 + TOFF), 200);

  t.brandMark = mkBox(scr, 318, 5, 3, 12, PCOL(dim), 1);
  t.brandTxt = mkLblClip(scr, "--", fSm(), PCOL(dim), 325, (4 + TOFF), 62);
  mkBox(scr, 389, 4, 1, 14, PCOL(border), 0);
  t.clk = mkLblClip(scr, "--:--", fSm(), PCOL(text), 394, (4 + TOFF), 80, LV_TEXT_ALIGN_RIGHT);
}

static void topBarUpdate() {
  const uint32_t cc = g_conn == CONN_ON ? PCOL(green) : (g_conn == CONN_BUSY ? PCOL(amber) : PCOL(red));
  const char *bt = g_set.brand == BRAND_HONDA ? S(S_BRAND_HONDA) : (g_set.brand == BRAND_YAMAHA ? S(S_BRAND_YAMAHA) : "--");
  const uint32_t bc = g_set.brand == BRAND_HONDA ? 0xFF3B30 : (g_set.brand == BRAND_YAMAHA ? PCOL(cyan) : PCOL(dim));
  char ck[20]; clockText(ck, sizeof(ck));
  for (int i = 0; i < N_PAGES; i++) {
    TopBar &t = g_top[i];
    if (!t.dot) continue;
    setBg(t.dot, cc);
    setTxt(t.brandTxt, bt); setCol(t.brandTxt, bc); setBg(t.brandMark, bc);
    setTxt(t.clk, ck);
  }
}

// ============================================================ toast
static lv_obj_t   *g_toast = nullptr;
static lv_obj_t   *g_toastLbl = nullptr;
static lv_timer_t *g_toastTimer = nullptr;
static void toastTimerCb(lv_timer_t *t) { show(g_toast, false); lv_timer_pause(t); }
static void toast(const char *msg, uint32_t col) {
  if (!g_toast) return;
  setTxt(g_toastLbl, msg); setCol(g_toastLbl, col);
  lv_obj_center(g_toastLbl);
  show(g_toast, true);
  lv_obj_move_foreground(g_toast);
  lv_timer_reset(g_toastTimer);
  lv_timer_resume(g_toastTimer);
}
static void buildToast(lv_obj_t *layer) {
  g_toast = mkBox(layer, 60, 214, 360, 26, PCOL(panel2), 13, PCOL(border), 1);
  g_toastLbl = mkLbl(g_toast, "", fSm(), PCOL(text), 0, 0, 350, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(g_toastLbl);
  show(g_toast, false);
  if (!g_toastTimer) g_toastTimer = lv_timer_create(toastTimerCb, 1800, NULL);
  lv_timer_pause(g_toastTimer);
}

// ============================================================ bottom bar (HOME page only): ODO | TRIP + reset
struct BotBar { lv_obj_t *odoVal, *tripLbl, *tripVal; };
static BotBar  g_bot;
static uint8_t g_tripSel = 0;                      // 0 = trip A, 1 = trip B

static void tripSwapCb(lv_event_t *e) { (void)e; g_tripSel = g_tripSel ? 0 : 1; }
static void tripRstClickCb(lv_event_t *e) { (void)e; toast(S(S_HINT_TRIP), PCOL(dim)); }
static void tripRstHoldCb(lv_event_t *e) {
  (void)e;
  if (g_tripSel) { g_set.tripB = 0; toast(S(S_TRIP_B_RST), PCOL(pink)); }
  else           { g_set.tripA = 0; toast(S(S_TRIP_A_RST), PCOL(orange)); }
  settingsSave();
}

static void buildBottomBar(lv_obj_t *scr) {
  BotBar &b = g_bot;
  const int y = BOTY + 2, h = 20;
  mkBox(scr, 6, y, 210, h, PCOL(panel), 8, PCOL(border), 1);
  mkLbl(scr, S(S_ODO), fSm(), PCOL(dim), 14, y + 3 + TOFF);
  b.odoVal = mkLblClip(scr, "0.0", fLatin(14), PCOL(text), 70, y + 2, 104, LV_TEXT_ALIGN_RIGHT);
  mkLbl(scr, S(S_KM), fSm(), PCOL(dim), 182, y + 3 + TOFF);

  lv_obj_t *tb = mkBox(scr, 222, y, 210, h, PCOL(panel), 8, PCOL(border), 1);
  lv_obj_add_flag(tb, LV_OBJ_FLAG_CLICKABLE);                      // tap = switch between trip A and trip B
  lv_obj_add_event_cb(tb, clickSoundCb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(tb, tripSwapCb, LV_EVENT_CLICKED, NULL);
  b.tripLbl = mkLbl(tb, S(S_TRIP_A), fSm(), PCOL(orange), 8, 1 + TOFF, 56);
  b.tripVal = mkLblClip(tb, "0.0", fLatin(14), PCOL(text), 64, 1, 104, LV_TEXT_ALIGN_RIGHT);
  mkLbl(tb, S(S_KM), fSm(), PCOL(dim), 176, 1 + TOFF);

  lv_obj_t *rb = mkBox(scr, 438, y, 36, h, PCOL(panel), 9, PCOL(border), 1);
  lv_obj_add_flag(rb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(rb, tripRstClickCb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(rb, tripRstHoldCb, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_t *ri = mkLbl(rb, LV_SYMBOL_REFRESH, &lv_font_montserrat_12, PCOL(dim), 0, 0);
  lv_obj_center(ri);
}

static void bottomBarUpdate() {
  BotBar &b = g_bot;
  if (g_page != 0 || !b.odoVal) return;
  char s[24];
  formatKm(s, sizeof(s), g_set.odoKm); setTxt(b.odoVal, s);
  formatKm(s, sizeof(s), g_tripSel ? g_set.tripB : g_set.tripA); setTxt(b.tripVal, s);
  setTxt(b.tripLbl, g_tripSel ? S(S_TRIP_B) : S(S_TRIP_A));
  setCol(b.tripLbl, g_tripSel ? PCOL(pink) : PCOL(orange));
}

// ============================================================ numeric keypad (number entry: ODO, real speed)
// key ids: 0..9 digit, 10 '.', 11 backspace, 12 OK, 13 cancel
static void buildKeypad(lv_obj_t *parent, int x, int y, bool dot, lv_event_cb_t cb) {
  const int kw = 54, kh = 46, gap = 5;
  const char *lab[12] = { "7", "8", "9", "4", "5", "6", "1", "2", "3", LV_SYMBOL_BACKSPACE, "0", "." };
  const int   id[12]  = { 7, 8, 9, 4, 5, 6, 1, 2, 3, 11, 0, 10 };
  for (int i = 0; i < 12; i++) {
    if (i == 11 && !dot) continue;
    mkBtn(parent, x + (i % 3) * (kw + gap), y + (i / 3) * (kh + gap), kw, kh, lab[i], fLatin(22),
          PCOL(panel2), id[i] == 11 ? PCOL(amber) : PCOL(text), 0, cb, id[i]);
  }
  const int ox = x + 3 * (kw + gap);
  mkBtn(parent, ox, y, 86, 2 * kh + gap + 24, S(S_OK), fMd(), PCOL(mint), PCOL(bg), 0, cb, 12);
  mkBtn(parent, ox, y + 2 * kh + gap + 29, 86, 2 * kh - 24 + gap, S(S_CANCEL), fMd(), PCOL(red), 0xFFFFFF, 0, cb, 13);
}

typedef void (*NumCb)(float);
static lv_obj_t *g_num = nullptr;
static lv_obj_t *g_numTitle = nullptr, *g_numVal = nullptr;
static char      g_numBuf[14] = "";
static bool      g_numDot = true;
static NumCb     g_numCb = nullptr;

static void numKeyCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  const size_t n = strlen(g_numBuf);
  if (id <= 9) { if (n < 10) { g_numBuf[n] = (char)('0' + id); g_numBuf[n + 1] = 0; } }
  else if (id == 10) { if (g_numDot && n < 10 && !strchr(g_numBuf, '.')) { if (n == 0) { g_numBuf[0] = '0'; g_numBuf[1] = '.'; g_numBuf[2] = 0; } else { g_numBuf[n] = '.'; g_numBuf[n + 1] = 0; } } }
  else if (id == 11) { if (n) g_numBuf[n - 1] = 0; }
  else if (id == 12) {
    const float v = (float)atof(g_numBuf);
    NumCb cb = g_numCb;
    show(g_num, false);
    if (cb && n) cb(v);
    return;
  }
  else if (id == 13) { show(g_num, false); return; }
  setTxt(g_numVal, g_numBuf[0] ? g_numBuf : "0");
}
static void numOpen(const char *title, bool dot, NumCb cb) {
  g_numBuf[0] = 0; g_numDot = dot; g_numCb = cb;
  setTxt(g_numTitle, title); setTxt(g_numVal, "0");
  show(g_num, true);
  lv_obj_move_foreground(g_num);
}
static void buildNumModal(lv_obj_t *layer) {
  g_num = mkBox(layer, 0, 0, SW, SH, PCOL(bg), 0);
  lv_obj_add_flag(g_num, LV_OBJ_FLAG_CLICKABLE);                // swallow touches behind the modal
  lv_obj_t *card = mkBox(g_num, 8, 32, 180, 232, PCOL(panel), 12, PCOL(border), 1);
  mkBox(card, 10, 12, 3, 13, PCOL(orange), 2);
  g_numTitle = mkLbl(card, "", fMd(), PCOL(text), 18, 8, 150);
  mkBox(card, 10, 70, 160, 54, PCOL(panel2), 10);
  g_numVal = mkLbl(card, "0", fLatin(28), PCOL(mint), 14, 80, 152, LV_TEXT_ALIGN_RIGHT);
  buildKeypad(g_num, 196, 34, true, numKeyCb);
  show(g_num, false);
}

// ============================================================ popup menu (compact)
static lv_obj_t   *g_menu = nullptr;
static lv_obj_t   *g_pageBtn[N_PAGES];
static lv_obj_t   *g_holdBar = nullptr, *g_holdHint = nullptr;
static lv_obj_t   *g_seg[3][2];                      // 0 brand, 1 theme, 2 language
static lv_obj_t   *g_sportBtn = nullptr;
static lv_timer_t *g_holdTimer = nullptr;
static HoldTracker g_hold;
static bool        g_holdFired = false;

static void segPaint() {
  const int sel[3] = { g_set.brand == BRAND_YAMAHA ? 1 : 0, g_set.dark ? 0 : 1, g_set.lang ? 1 : 0 };
  const uint32_t col[3] = { PCOL(cyan), PCOL(green), PCOL(pink) };
  for (int r = 0; r < 3; r++) {
    for (int k = 0; k < 2; k++) {
      lv_obj_t *b = g_seg[r][k];
      const bool on = (k == sel[r]) && !(r == 0 && g_set.brand == BRAND_NONE);
      setBg(b, on ? col[r] : PCOL(panel2));
      setCol(lv_obj_get_child(b, 0), on ? PCOL(bg) : PCOL(dim));
    }
  }
  if (g_sportBtn) {                                              // SPORT MODE switch lives in the menu now
    char t[40]; snprintf(t, sizeof(t), "%s: %s", S(S_SPORT_MODE), g_set.sport ? S(S_ON) : S(S_OFF));
    setTxt(lv_obj_get_child(g_sportBtn, 0), t);
    lv_obj_center(lv_obj_get_child(g_sportBtn, 0));
    setBg(g_sportBtn, g_set.sport ? PCOL(amber) : PCOL(panel2));
    setCol(lv_obj_get_child(g_sportBtn, 0), g_set.sport ? PCOL(bg) : PCOL(dim));
  }
}
static void segCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);        // row*2 + k
  const int row = id / 2, k = id % 2;
  if (row == 0) {
    const uint8_t nb = k == 0 ? BRAND_HONDA : BRAND_YAMAHA;
    if (nb != g_set.brand) { g_set.brand = nb; settingsSave(); onBrandChanged(); }
    segPaint();
  } else if (row == 1) {
    const uint8_t nd = k == 0 ? 1 : 0;
    if (nd != g_set.dark) { g_set.dark = nd; settingsSave(); g_rebuildUi = true; g_reopenMenu = true; }
  } else {
    const uint8_t nl = k == 0 ? 0 : 1;
    if (nl != g_set.lang) { g_set.lang = nl; settingsSave(); g_rebuildUi = true; g_reopenMenu = true; }
  }
}
static void sportCb(lv_event_t *e) {
  (void)e;
  g_set.sport = g_set.sport ? 0 : 1;
  settingsSave();
  segPaint();
  toast(g_set.sport ? S(S_SPORT_ON) : S(S_SPORT_OFF), g_set.sport ? PCOL(green) : PCOL(amber));
}

static void holdVisual(float p) {
  lv_obj_set_width(g_holdBar, (int)(112.0f * p));
  show(g_holdHint, p > 0.05f);
  if (p > 0.05f) {
    char b[48];
    snprintf(b, sizeof(b), S(S_HOLD_HINT), (int)(p * 6.0f));
    setTxt(lv_obj_get_child(g_holdHint, 0), b);
  }
}
static void holdTimerCb(lv_timer_t *t) {
  const uint32_t now = millis();
  if (!g_hold.active) { holdVisual(0); lv_timer_pause(t); return; }
  holdVisual(g_hold.progress(now, ADMIN_HOLD_MS));
  if (g_hold.done(now, ADMIN_HOLD_MS)) {
    g_holdFired = true;
    g_hold.stop();
    lv_timer_pause(t);
    holdVisual(0);
    menuClose();
    pinOpen();
  }
}
static void homeHoldCb(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) {
    g_holdFired = false;
    g_hold.start(millis());
    lv_timer_resume(g_holdTimer);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    g_hold.stop();
    holdVisual(0);
  }
}
static void pageBtnCb(lv_event_t *e) {
  const int id = (int)(intptr_t)lv_event_get_user_data(e);
  if (id == 0 && g_holdFired) { g_holdFired = false; return; }    // the long hold already opened the admin PIN
  menuClose();
  uiShowPage(id);
}
static void menuCloseCb(lv_event_t *e) { (void)e; menuClose(); }

static void menuOpen() {
  if (!g_menu) return;
  segPaint();
  holdVisual(0);
  show(g_menu, true);
  lv_obj_move_foreground(g_menu);
}
static void menuClose() {
  g_hold.stop();
  if (g_holdTimer) lv_timer_pause(g_holdTimer);
  show(g_menu, false);
}

// 316 x 212 card in the middle: pages on the left, settings on the right
static void buildMenu(lv_obj_t *layer) {
  g_menu = mkBox(layer, 0, 0, SW, SH, 0x000000, 0);
  lv_obj_set_style_bg_opa(g_menu, LV_OPA_70, 0);
  lv_obj_add_flag(g_menu, LV_OBJ_FLAG_CLICKABLE);
  const int w = 316, h = 214, x0 = (SW - w) / 2, y0 = 26;
  lv_obj_t *d = mkBox(g_menu, x0, y0, w, h, PCOL(panel), 14, PCOL(orange), 2);
  mkLbl(d, S(S_MENU), fMd(), PCOL(text), 12, g_set.lang ? 3 : 4);
  g_holdHint = mkBox(d, 76, 5, 150, 18, PCOL(panel2), 9, PCOL(orange), 1);
  mkLbl(g_holdHint, "", fSm(), PCOL(orange), 0, 0, 148, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(lv_obj_get_child(g_holdHint, 0));
  show(g_holdHint, false);
  mkBtn(d, w - 64, 4, 52, 20, S(S_CLOSE), fSm(), PCOL(panel2), PCOL(text), PCOL(border), menuCloseCb, 0);

  const StrId names[N_PAGES] = { S_PG_HOME, S_PG_DATA, S_PG_GRAPH, S_PG_DTC, S_PG_FUEL };
  const char *icons[N_PAGES] = { LV_SYMBOL_HOME, LV_SYMBOL_LIST, LV_SYMBOL_SHUFFLE, LV_SYMBOL_WARNING, LV_SYMBOL_CHARGE };
  for (int i = 0; i < N_PAGES; i++) {
    char txt[48]; snprintf(txt, sizeof(txt), "%s  %s", icons[i], S(names[i]));
    g_pageBtn[i] = mkBtn(d, 12, 32 + i * 35, 128, 31, txt, fSm(), PCOL(panel2), PCOL(text), 0, pageBtnCb, i);
    mkBox(g_pageBtn[i], 8, 27, 112, 2, PCOL(border), 1);
  }
  // the HOME button doubles as the hidden admin entry: hold it for 6 s
  g_holdBar = mkBox(g_pageBtn[0], 8, 27, 0, 2, PCOL(orange), 1);
  lv_obj_add_event_cb(g_pageBtn[0], homeHoldCb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g_pageBtn[0], homeHoldCb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(g_pageBtn[0], homeHoldCb, LV_EVENT_PRESS_LOST, NULL);

  const int sx = 152, sw = 152;
  const char *sec[3] = { S(S_MENU_BRAND), S(S_MENU_THEME), S(S_MENU_LANG) };
  const char *opt[3][2] = { { S(S_BRAND_HONDA), S(S_BRAND_YAMAHA) }, { S(S_THEME_DARK), S(S_THEME_LIGHT) }, { "ENGLISH", "ไทย" } };
  const uint32_t sc[3] = { PCOL(cyan), PCOL(green), PCOL(pink) };
  const int sy[3] = { 30, 74, 118 };
  for (int r = 0; r < 3; r++) {
    mkBox(d, sx, sy[r] + 3, 3, 10, sc[r], 1);
    mkLbl(d, sec[r], fSm(), PCOL(text), sx + 8, sy[r] + TOFF + 1);
    for (int k = 0; k < 2; k++) {
      g_seg[r][k] = mkBtn(d, sx + k * (sw / 2 + 2), sy[r] + 17, sw / 2 - 2, 22, opt[r][k], r == 2 ? fTh() : fSm(), PCOL(panel2), PCOL(dim), 0, segCb, r * 2 + k);
    }
  }
  g_sportBtn = mkBtn(d, sx, 166, sw, 28, "", fSm(), PCOL(panel2), PCOL(dim), PCOL(amber), sportCb, 0);
  if (!g_holdTimer) g_holdTimer = lv_timer_create(holdTimerCb, 50, NULL);
  lv_timer_pause(g_holdTimer);
  show(g_menu, false);
}

// ============================================================ over-rev flash overlay (on top of every screen)
static lv_obj_t   *g_flashBar[4] = { nullptr, nullptr, nullptr, nullptr };   // red frame: 4 thin opaque bars
static lv_obj_t   *g_flashBanner = nullptr, *g_flashBannerTxt = nullptr;
static lv_timer_t *g_flashTimer = nullptr;
static bool        g_flashActive = false, g_flashPhase = false;

static void flashTimerCb(lv_timer_t *t) {
  (void)t;
  g_flashPhase = !g_flashPhase;
  for (int i = 0; i < 4; i++) show(g_flashBar[i], g_flashPhase);
}
static void buildFlash(lv_obj_t *layer) {
  // Four thin opaque bars instead of a full-screen overlay: LVGL repaints only these strips when they blink.
  const int T_ = 6;
  g_flashBar[0] = mkBox(layer, 0, 0, SW, T_, 0xE01010, 0);
  g_flashBar[1] = mkBox(layer, 0, SH - T_, SW, T_, 0xE01010, 0);
  g_flashBar[2] = mkBox(layer, 0, T_, T_, SH - 2 * T_, 0xE01010, 0);
  g_flashBar[3] = mkBox(layer, SW - T_, T_, T_, SH - 2 * T_, 0xE01010, 0);
  for (int i = 0; i < 4; i++) show(g_flashBar[i], false);
  g_flashBanner = mkBox(layer, 150, 34, 180, 22, 0xE01010, 11);
  g_flashBannerTxt = mkLbl(g_flashBanner, "", fSm(), 0xFFFFFF, 0, 0, 178, LV_TEXT_ALIGN_CENTER);
  lv_obj_center(g_flashBannerTxt);
  show(g_flashBanner, false);
  if (!g_flashTimer) g_flashTimer = lv_timer_create(flashTimerCb, 200, NULL);
  lv_timer_pause(g_flashTimer);
  g_flashActive = false;
}
static void flashSet(bool on, uint16_t rpm) {
  if (on) {
    char b[40];
    snprintf(b, sizeof(b), "%s  %u", S(S_OVERREV), (unsigned)rpm);
    setTxt(g_flashBannerTxt, b);
    lv_obj_center(g_flashBannerTxt);
  }
  if (on == g_flashActive) return;
  g_flashActive = on;
  show(g_flashBanner, on);
  if (on) { for (int i = 0; i < 4; i++) lv_obj_move_foreground(g_flashBar[i]); lv_obj_move_foreground(g_flashBanner); lv_timer_resume(g_flashTimer); }
  else    { lv_timer_pause(g_flashTimer); for (int i = 0; i < 4; i++) show(g_flashBar[i], false); g_flashPhase = false; }
}

// ============================================================ gear change effect (colour pulse only - no layers)
static lv_obj_t *g_gearLbl = nullptr;

static void gearAnimCb(void *var, int32_t v) {
  setCol((lv_obj_t *)var, v < 55 ? 0xFFFFFF : PCOL(cyan));
}
static void gearEffect() {
  if (!g_gearLbl) return;
  lv_anim_del(g_gearLbl, gearAnimCb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, g_gearLbl);
  lv_anim_set_exec_cb(&a, gearAnimCb);
  lv_anim_set_values(&a, 0, 100);
  lv_anim_set_time(&a, 300);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}
