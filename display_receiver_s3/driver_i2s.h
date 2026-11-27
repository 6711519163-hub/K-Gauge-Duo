/*
  driver_i2s.h - Non-blocking I2S audio feedback for JC4827W543 (ESP32-S3)

  Wiring (I2S amplifier on the board):
      BCLK = GPIO 42    LRCLK/WS = GPIO 2    DIN (ESP DOUT) = GPIO 41

  Requires ESP32 Arduino core 3.x (ESP-IDF 5.x "i2s_std" driver, the same core
  that provides ledcAttach() used elsewhere in this project).

  API (all safe to call from the LVGL / loop() context, none of them block):
      audio_init();                 // call once in setup()
      audio_click();                // 1000 Hz, ~40 ms beep
      audio_set_shift_alarm(bool);  // dual-tone 2000/2500 Hz alarm on/off
      audio_set_overheat_alarm(bool);// triple 1500 Hz beep, repeats every 2 s while on

  A dedicated FreeRTOS task owns the I2S channel and synthesises the audio.

  NOTE: the file is deliberately NOT called "driver/i2s.h" - that name collides
  with the legacy ESP-IDF header of the same name and can break the build.
*/
#pragma once

#include <Arduino.h>
#include <math.h>
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef I2S_BCLK_PIN
#define I2S_BCLK_PIN   42
#endif
#ifndef I2S_LRCLK_PIN
#define I2S_LRCLK_PIN  2
#endif
#ifndef I2S_DIN_PIN
#define I2S_DIN_PIN    41
#endif

#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_VOLUME        8000     // peak amplitude, 0..32767 (raise/lower to taste)
#define AUDIO_CHUNK_FRAMES  128      // 8 ms per DMA write

#define AUDIO_CLICK_HZ      1000
#define AUDIO_CLICK_MS      40
#define AUDIO_ALARM_HZ_A    2000
#define AUDIO_ALARM_HZ_B    2500
#define AUDIO_ALARM_STEP_MS 80       // tone alternates every 80 ms
#define AUDIO_HEAT_HZ       1500     // overheat warning: 3 beeps (120 ms on / 80 ms off) every 2 s
#define AUDIO_HEAT_BEEP_ON_MS   120
#define AUDIO_HEAT_BEEP_MS      200
#define AUDIO_HEAT_CYCLE_MS     2000

static i2s_chan_handle_t  s_i2sTx        = NULL;
static TaskHandle_t       s_audioTask    = NULL;
static volatile bool      s_clickPending = false;
static volatile bool      s_alarmOn      = false;
static volatile bool      s_heatOn       = false;

static void audio_task(void *arg) {
  (void)arg;
  int16_t buf[AUDIO_CHUNK_FRAMES * 2];            // stereo, 16-bit
  const float twoPi     = 6.28318530718f;
  const float ampStep   = (float)AUDIO_VOLUME / (AUDIO_SAMPLE_RATE * 0.003f); // 3 ms ramp (no pops)
  const uint32_t stepSamples  = (AUDIO_SAMPLE_RATE * AUDIO_ALARM_STEP_MS) / 1000;
  const uint32_t heatCycle    = (AUDIO_SAMPLE_RATE * AUDIO_HEAT_CYCLE_MS) / 1000;
  const uint32_t heatBeep     = (AUDIO_SAMPLE_RATE * AUDIO_HEAT_BEEP_MS) / 1000;
  const uint32_t heatBeepOn   = (AUDIO_SAMPLE_RATE * AUDIO_HEAT_BEEP_ON_MS) / 1000;

  float    phase = 0.0f;
  float    amp   = 0.0f;
  int32_t  clickLeft  = 0;
  uint32_t alarmCount = 0;
  uint32_t heatCount  = 0;

  for (;;) {
    if (s_clickPending) {
      s_clickPending = false;
      clickLeft = (AUDIO_SAMPLE_RATE * AUDIO_CLICK_MS) / 1000;
    }

    // Fully idle: sleep until a click / alarm wakes us (20 ms timeout as safety net)
    if (clickLeft <= 0 && !s_alarmOn && !s_heatOn && amp <= 0.0f) {
      alarmCount = 0;
      heatCount = 0;
      phase = 0.0f;
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      continue;
    }

    for (int i = 0; i < AUDIO_CHUNK_FRAMES; i++) {
      const bool click = (clickLeft > 0);
      const bool alarm = s_alarmOn;

      bool heatTone = false;
      if (s_heatOn) {
        const uint32_t p = heatCount % heatCycle;
        heatCount++;
        heatTone = (p < 3 * heatBeep) && ((p % heatBeep) < heatBeepOn);   // 3 beeps then silence
      } else {
        heatCount = 0;
      }

      // Priority: button click > shift alarm > overheat warning
      float freq = AUDIO_ALARM_HZ_A;
      bool active = true;
      if (click) {
        freq = AUDIO_CLICK_HZ;
        clickLeft--;
      } else if (alarm) {
        freq = ((alarmCount / stepSamples) & 1) ? AUDIO_ALARM_HZ_B : AUDIO_ALARM_HZ_A;
        alarmCount++;
      } else if (heatTone) {
        freq = AUDIO_HEAT_HZ;
      } else {
        active = false;
      }
      if (!alarm) alarmCount = 0;

      const float target = active ? (float)AUDIO_VOLUME : 0.0f;
      if (amp < target)      { amp += ampStep; if (amp > target) amp = target; }
      else if (amp > target) { amp -= ampStep; if (amp < target) amp = target; }

      phase += twoPi * freq / AUDIO_SAMPLE_RATE;
      if (phase >= twoPi) phase -= twoPi;

      const int16_t smp = (int16_t)(amp * sinf(phase));
      buf[i * 2]     = smp;   // left
      buf[i * 2 + 1] = smp;   // right
    }

    size_t written = 0;
    i2s_channel_write(s_i2sTx, buf, sizeof(buf), &written, 100);   // timeout in ms
  }
}

static bool audio_init() {
  if (s_i2sTx) return true;

  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num  = 4;
  chan_cfg.dma_frame_num = AUDIO_CHUNK_FRAMES;
  chan_cfg.auto_clear    = true;                  // DMA outputs silence when starved

  if (i2s_new_channel(&chan_cfg, &s_i2sTx, NULL) != ESP_OK) {
    Serial.println("[AUDIO] i2s_new_channel failed");
    s_i2sTx = NULL;
    return false;
  }

  i2s_std_config_t std_cfg = {};
  std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK_PIN;
  std_cfg.gpio_cfg.ws   = (gpio_num_t)I2S_LRCLK_PIN;
  std_cfg.gpio_cfg.dout = (gpio_num_t)I2S_DIN_PIN;
  std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
  std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
  std_cfg.gpio_cfg.invert_flags.ws_inv   = false;

  if (i2s_channel_init_std_mode(s_i2sTx, &std_cfg) != ESP_OK ||
      i2s_channel_enable(s_i2sTx) != ESP_OK) {
    Serial.println("[AUDIO] I2S init/enable failed");
    i2s_del_channel(s_i2sTx);
    s_i2sTx = NULL;
    return false;
  }

  xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 5, &s_audioTask, 0);
  return true;
}

static void audio_click() {
  s_clickPending = true;
  if (s_audioTask) xTaskNotifyGive(s_audioTask);
}

static void audio_set_shift_alarm(bool on) {
  if (on == s_alarmOn) return;
  s_alarmOn = on;
  if (on && s_audioTask) xTaskNotifyGive(s_audioTask);
}

// Overheat warning: triple beep every 2 s while on (lower priority than clicks and the shift alarm)
static void audio_set_overheat_alarm(bool on) {
  if (on == s_heatOn) return;
  s_heatOn = on;
  if (on && s_audioTask) xTaskNotifyGive(s_audioTask);
}
