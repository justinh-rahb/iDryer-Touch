// LVGL 8.x configuration for iDryer Touch (ESP32 CYD, 320x240).
//
// Only overrides live here — lv_conf_internal.h supplies an #ifndef default for
// every option, so there is no need to vendor the full 900-line template.
//
// Reached via -DLV_CONF_INCLUDE_SIMPLE plus -Iinclude (see platformio.ini).

#pragma once

// LVGL checks for this macro to decide whether the include actually landed, and
// warns "Possible failure to include lv_conf.h" without it — #pragma once alone
// is not enough. The include was working regardless, but the warning is noise.
#define LV_CONF_H

#include <stdint.h>

// ── Colour ───────────────────────────────────────────────────────────────────
// LovyanGFX is handed an explicit lgfx::rgb565_t source in the flush callback,
// so it performs the byte order conversion the SPI panel needs. LVGL must NOT
// pre-swap as well, or the two cancel out and colours come back wrong.
#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      0

// ── Memory ───────────────────────────────────────────────────────────────────
// Use the heap rather than a static pool: a static LV_MEM_SIZE lands in .bss,
// which is the exact pressure that made the cloud firmware fail to link on this
// chip (see TouchApp.cpp).
#define LV_MEM_CUSTOM         1
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc

// Arduino's millis() drives LVGL's tick, so no separate timer task is needed.
#define LV_TICK_CUSTOM              1
#define LV_TICK_CUSTOM_INCLUDE      "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DPI_DEF            130

// ── Diagnostics off ──────────────────────────────────────────────────────────
#define LV_USE_LOG            0
#define LV_USE_ASSERT_NULL    0
#define LV_USE_ASSERT_MALLOC  0
#define LV_USE_PERF_MONITOR   0
#define LV_USE_MEM_MONITOR    0
#define LV_BUILD_EXAMPLES     0

// ── Fonts ────────────────────────────────────────────────────────────────────
// Each face costs flash, so enable exactly the sizes the UI asks for:
// 12 captions, 14 body, 16 buttons, 28 stepper values, 36 the big readouts.
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

// ── Widgets ──────────────────────────────────────────────────────────────────
// The UI is labels, buttons and one bar. Everything below defaults to enabled,
// so switching the unused ones off is worth a sizeable chunk of flash. Text
// entry is deliberately absent — see docs/ARCHITECTURE.md.
#define LV_USE_ANIMIMG        0
#define LV_USE_ARC            0
#define LV_USE_CALENDAR       0
#define LV_USE_CANVAS         0
#define LV_USE_CHART          0
#define LV_USE_CHECKBOX       0
#define LV_USE_COLORWHEEL     0
#define LV_USE_DROPDOWN       0
#define LV_USE_IMGBTN         0
#define LV_USE_KEYBOARD       0
#define LV_USE_LED            0
#define LV_USE_LINE           0
#define LV_USE_LIST           0
#define LV_USE_MENU           0
#define LV_USE_METER          0
#define LV_USE_MSGBOX         0
#define LV_USE_ROLLER         0
#define LV_USE_SLIDER         0
#define LV_USE_SPAN           0
#define LV_USE_SPINBOX        0
#define LV_USE_SPINNER        0
#define LV_USE_SWITCH         0
#define LV_USE_TABLE          0
#define LV_USE_TABVIEW        0
#define LV_USE_TEXTAREA       0
#define LV_USE_TILEVIEW       0
#define LV_USE_WIN            0

// Keep: LV_USE_BAR, LV_USE_BTN, LV_USE_IMG, LV_USE_LABEL (all default 1).

// ── Theme ────────────────────────────────────────────────────────────────────
// The UI styles every widget explicitly, so the simple theme is enough and
// costs less than the default one.
#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_THEME_BASIC    0
