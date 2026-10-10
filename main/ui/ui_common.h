/*
 * Shared look and building blocks for all screens.
 * Call every function with the display lock held.
 */
#pragma once

#include "lvgl.h"

#define UI_SCREEN_W         480
#define UI_SCREEN_H         480
#define UI_STATUSBAR_H      32      // always on top (lv_layer_top)
#define UI_TITLEBAR_H       48
#define UI_CONTENT_H        (UI_SCREEN_H - UI_STATUSBAR_H - UI_TITLEBAR_H)

#define UI_COLOR_BG         0x12101A
#define UI_COLOR_SURFACE    0x221E2E
#define UI_COLOR_ACCENT     0xB388FF
#define UI_COLOR_TEXT_DIM   0x9E9AA8

typedef void (*ui_back_cb_t)(void);

/**
 * @brief Apply the dark theme to the default display
 */
void ui_common_init(void);

/**
 * @brief Empty screen with the app background
 */
lv_obj_t *ui_screen_create(void);

/**
 * @brief Screen for an app: title bar with a back button, and a content area under it
 *
 * @param[out] content the area the app fills (480 x UI_CONTENT_H)
 */
lv_obj_t *ui_app_screen_create(const char *title, ui_back_cb_t on_back, lv_obj_t **content);

/**
 * @brief Screen that only shows an error message
 */
lv_obj_t *ui_error_screen_create(const char *msg);

/**
 * @brief Text with fixed-width digits, for clocks and counters
 *
 * Montserrat digits have different widths ("1" is narrow, "0" is wide), so a normal label
 * changes width and jumps around when the numbers change. This puts every character in its
 * own cell; all digits get the width of the widest one. Only cells that change are redrawn.
 */
lv_obj_t *ui_digits_create(lv_obj_t *parent, const lv_font_t *font);

void ui_digits_set_text(lv_obj_t *digits, const char *text);

/**
 * @brief Show or hide LVGL's FPS / CPU overlay (bottom left). Hidden after ui_common_init().
 */
void ui_perf_monitor_set(bool show);

bool ui_perf_monitor_get(void);

/**
 * @brief Show a short message near the bottom of the screen for a few seconds (above everything)
 */
void ui_toast_show(const char *text);

/**
 * @brief Rounded panel ("card") that stacks its children vertically
 */
lv_obj_t *ui_card_create(lv_obj_t *parent, const char *title);
