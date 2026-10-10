/*
 * Quick panel: slides down from the top (tap or swipe down on the status bar).
 * WiFi and Bluetooth on/off tiles with their status. Close it by tapping outside,
 * swiping up on it, or with its arrow button.
 * Call every function with the display lock held.
 */
#pragma once

void quick_panel_create(void);

void quick_panel_open(void);

void quick_panel_close(void);

/**
 * @brief Update tiles and texts (call when WiFi/Bluetooth state changes)
 */
void quick_panel_refresh(void);
