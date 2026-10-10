/*
 * Bluetooth Low Energy service (NimBLE). The ESP32-S3 has BLE only, no Classic Bluetooth.
 *
 * When enabled, the board advertises as APP_DISPLAY_NAME and accepts one connection, e.g. from
 * the "nRF Connect" phone app. It offers one GATT service with two characteristics:
 *   Info     (read)   firmware version, time and uptime as text
 *   Message  (write)  UTF-8 text that is shown on the screen (APP_EVT_BT_MESSAGE)
 * Connection changes are reported with APP_EVT_BT_CHANGED.
 * The BLE stack starts on the first bt_svc_set_enabled(true), so it costs no RAM until used.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define BT_SVC_MESSAGE_LEN  100

typedef struct {
    bool enabled;           // advertising / connectable
    bool connected;
    char peer[18];          // address of the connected phone
} bt_svc_status_t;

esp_err_t bt_svc_set_enabled(bool enabled);

void bt_svc_get_status(bt_svc_status_t *status);

/**
 * @brief Copy the last message written by the phone
 *
 * @return length of the message (0 if none)
 */
size_t bt_svc_get_message(char *buf, size_t len);
