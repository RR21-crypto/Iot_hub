/*
 * Application state machine (event-driven, table-based).
 *
 *            INIT_DONE           APP_OPEN(id)
 *   [BOOT] ------------> [HOME] -------------> [APP]
 *      |                   ^  <-------------    |
 *      | INIT_FAILED       |     APP_CLOSE      |
 *      v                   +-- TICK      TICK --+   (status bar clock, app on_tick)
 *   [ERROR]
 *
 *   In any state: WIFI_UP/WIFI_DOWN (status bar, NTP, OTA server), BT_CHANGED (status bar),
 *   BT_MESSAGE (toast), WIFI_SCAN_DONE, TIME_SYNCED, OTA_PROGRESS and WEATHER_* are
 *   handled and also forwarded to the open app.
 *
 * Events are posted to a queue from any task (touch callbacks, timer, services) and handled
 * one at a time by the state machine task, with the display lock held, so state is never shared.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    APP_STATE_BOOT,
    APP_STATE_HOME,
    APP_STATE_APP,
    APP_STATE_ERROR,
    APP_STATE_COUNT,
    APP_STATE_ANY = APP_STATE_COUNT,    // transition table only: matches every state / stays
} app_state_t;

typedef enum {
    APP_EVT_INIT_DONE,
    APP_EVT_INIT_FAILED,
    APP_EVT_APP_OPEN,       // arg = index in the app registry
    APP_EVT_APP_CLOSE,
    APP_EVT_TICK,           // once per second
    APP_EVT_WIFI_UP,        // got an IP address
    APP_EVT_WIFI_DOWN,
    APP_EVT_WIFI_SCAN_DONE,
    APP_EVT_TIME_SYNCED,    // NTP time written to the RTC
    APP_EVT_OTA_PROGRESS,   // arg = 0..100 percent, -1 = failed
    APP_EVT_WEATHER_UPDATED,    // arg = 0 ok, -1 failed
    APP_EVT_WEATHER_SEARCH_DONE,    // arg = number of places found, -1 failed
    APP_EVT_BT_CHANGED,     // Bluetooth turned on/off or a phone connected/disconnected
    APP_EVT_BT_MESSAGE,     // the phone wrote a text (bt_svc_get_message)
    APP_EVT_COUNT,
} app_event_t;

/**
 * @brief Create the event queue, the 1 s tick timer and the state machine task (starts in BOOT)
 */
esp_err_t app_sm_start(void);

/**
 * @brief Queue an event for the state machine (any task, not from an ISR)
 *
 * @param arg event data (see app_event_t), 0 if unused
 * @return false if the queue is full and the event was dropped
 */
bool app_sm_post(app_event_t event, int arg);

/**
 * @brief Open an app from the home screen (launcher callback): posts APP_OPEN
 */
void app_sm_request_open(int app_index);
