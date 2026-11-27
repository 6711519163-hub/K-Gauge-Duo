/*
  lv_conf.h for LVGL 8.3.11
*/
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Memory Manager Configuration */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
  #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
  #define LV_MEM_CUSTOM_ALLOC   malloc
  #define LV_MEM_CUSTOM_FREE    free
  #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/* Custom Tick Source */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
  #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif
#define LV_DISP_DEF_REFR_PERIOD 16  /* 16 ms = ~60 FPS (ค่า default เดิมคือ 33 ms = ~30 FPS) */
#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_ASSERT_NULL 0
#define LV_USE_ASSERT_MALLOC 0

/* Fonts */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* Widgets */
#define LV_USE_ARC    1
#define LV_USE_BAR    1
#define LV_USE_BTN    1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 1
#define LV_USE_CHART  1
#define LV_USE_LABEL  1
#define LV_USE_SLIDER 1

#endif /* LV_CONF_H */