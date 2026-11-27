/*
  driver_led.h - 14 x WS2812B on ONE data pin (2 sides x 7 LEDs wired in series), ESP32-S3, Arduino core 3.x.

  Wiring:   ESP32 GPIO --[330 ohm]--> DIN of LED 1 ... DOUT -> DIN of the next LED ... (all 14 in one chain)
            5 V and GND of the strips from a proper 5 V supply (14 LEDs can draw ~1 A at full white),
            GND of the supply connected to the board GND, a 470..1000 uF capacitor across 5 V / GND near the LEDs.
            WS2812B wants a 5 V data level: a 74AHCT125 / 74HCT245 level shifter is recommended (3.3 V often works
            on short wires, but not reliably).

  Pin:      set WS2812_PIN (in the sketch, before the #includes). -1 = LED output disabled.
            Do NOT use a pin the board already uses:  1 2 3 4 8 15 16 21 38 39 40 41 42 45 47 48
            (backlight, touch, K-line, LCD, I2S) and 0, 19, 20, 26..37 (boot / USB / flash / PSRAM).

  Colour order on the wire is GRB (WS2812B).  The RMT peripheral generates the exact timing, the CPU is not
  tied up; a 14-LED frame takes ~0.5 ms.  A frame is only sent when it differs from the last one.
*/
#pragma once
#include <Arduino.h>

#ifndef WS2812_PIN
#define WS2812_PIN  18
#endif

#define LED_RMT_HZ  10000000            // 10 MHz -> 100 ns per tick

static rmt_data_t s_ledBits[LED_TOTAL * 24];
static uint8_t    s_ledLast[LED_TOTAL * 3];
static bool       s_ledOk = false;
static bool       s_ledSent = false;

static bool led_pin_set() { return WS2812_PIN >= 0; }

// Push LED_TOTAL x (R,G,B) to the strip. Does nothing when the frame is unchanged.
static void led_write(const uint8_t *rgb) {
  if (!s_ledOk) return;
  if (s_ledSent && memcmp(rgb, s_ledLast, sizeof(s_ledLast)) == 0) return;
  memcpy(s_ledLast, rgb, sizeof(s_ledLast));
  s_ledSent = true;
  int bit = 0;
  for (int i = 0; i < LED_TOTAL; i++) {
    const uint8_t grb[3] = { rgb[i * 3 + 1], rgb[i * 3], rgb[i * 3 + 2] };
    for (int c = 0; c < 3; c++) {
      for (int m = 7; m >= 0; m--) {
        const bool one = (grb[c] >> m) & 1;
        s_ledBits[bit].level0    = 1;
        s_ledBits[bit].duration0 = one ? 8 : 4;      // T1H 0.8 us / T0H 0.4 us
        s_ledBits[bit].level1    = 0;
        s_ledBits[bit].duration1 = one ? 4 : 8;      // T1L 0.4 us / T0L 0.8 us
        bit++;
      }
    }
  }
  rmtWrite(WS2812_PIN, s_ledBits, LED_TOTAL * 24, RMT_WAIT_FOR_EVER);
}

static void led_off() {
  uint8_t z[LED_TOTAL * 3];
  memset(z, 0, sizeof(z));
  led_write(z);
}

static bool led_begin() {
  if (!led_pin_set()) {
    Serial.println("[LED] disabled: WS2812_PIN is -1 (set the data pin in the sketch)");
    return false;
  }
  if (!rmtInit(WS2812_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, LED_RMT_HZ)) {
    Serial.println("[LED] rmtInit failed - check the pin");
    return false;
  }
  s_ledOk = true;
  s_ledSent = false;
  led_off();
  Serial.printf("[LED] %d x WS2812B on GPIO %d\n", (int)LED_TOTAL, (int)WS2812_PIN);
  return true;
}
