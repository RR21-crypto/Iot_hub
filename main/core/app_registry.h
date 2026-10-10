/*
 * The list of apps shown on the home screen.
 *
 * To add an app:
 *   1. create main/apps/<name>/<name>_app.c/.h that defines a `const app_desc_t <name>_app`
 *   2. add it to the `apps[]` table in app_registry.c
 *   3. add the .c file to main/CMakeLists.txt
 *
 * All callbacks run with the display lock held, so they may use LVGL directly.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "app_sm.h"

typedef struct {
    const char *name;                   // shown under the icon and in the title bar
    const char *icon;                   // an LV_SYMBOL_... string
    const lv_image_dsc_t *icon_img;     // optional: a picture instead of `icon` (square, any size)
    uint32_t color;                     // icon background, 0xRRGGBB
    void (*create)(lv_obj_t *parent);   // build the UI inside `parent` (the area under the title bar)
    void (*destroy)(void);              // optional: free what LVGL does not own (buffers, timers)
    void (*on_tick)(void);              // optional: called once per second while the app is open
    void (*on_event)(app_event_t event, int arg);   // optional: WIFI_*, TIME_SYNCED, OTA_PROGRESS
} app_desc_t;

size_t app_registry_count(void);

/**
 * @return the app at this index, or NULL if out of range
 */
const app_desc_t *app_registry_get(size_t index);
