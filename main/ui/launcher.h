/*
 * Home screen: a grid of app icons built from the app registry.
 * Call every function with the display lock held.
 */
#pragma once

#include "lvgl.h"

typedef void (*launcher_open_cb_t)(int app_index);

/**
 * @brief Build the home screen (not shown yet)
 *
 * @param on_open called from the LVGL task when an icon is tapped
 */
lv_obj_t *launcher_create(launcher_open_cb_t on_open);

lv_obj_t *launcher_screen(void);
