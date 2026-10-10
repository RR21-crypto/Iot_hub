/*
 * Status bar on the top layer (visible above every screen): clock, WiFi and Bluetooth icons.
 * Tap it or swipe down on it to open the quick panel.
 * Call every function with the display lock held.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

void statusbar_create(void);

/**
 * @brief Show the time (NULL shows "--:--")
 */
void statusbar_set_time(const struct tm *now);

void statusbar_set_wifi(bool connected);

void statusbar_set_bt(bool enabled, bool connected);

/**
 * @brief Show a short message instead of the app name (NULL restores the name)
 */
void statusbar_set_message(const char *text);
