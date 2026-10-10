/*
 * Firmware update over WiFi (OTA) from a web browser.
 *
 * Open http://<board IP>/ on a computer in the same WiFi, pick build/anna_display.bin,
 * type the 4-digit code shown in Settings > Firmware update and press Upload. The new firmware is
 * written to the other OTA slot and the board restarts into it. If the new firmware does not
 * reach the home screen, the bootloader goes back to the previous one (rollback).
 * Progress is reported with APP_EVT_OTA_PROGRESS.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Start the web server (call when WiFi is up; calling it again does nothing)
 */
esp_err_t ota_svc_start(void);

/**
 * @brief The code the web page asks for, so only someone who sees the screen can update
 */
const char *ota_svc_code(void);

/**
 * @brief Tell the bootloader this firmware works (cancels the rollback). Call once the UI is up.
 */
void ota_svc_mark_valid(void);
