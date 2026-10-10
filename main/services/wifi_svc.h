/*
 * WiFi service (station mode).
 * The chosen network is remembered by the WiFi driver in NVS and reconnected after every boot.
 * Results are reported to the state machine: APP_EVT_WIFI_UP / WIFI_DOWN / WIFI_SCAN_DONE.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WIFI_SVC_SSID_LEN   33
#define WIFI_SVC_MAX_APS    20

typedef enum {
    WIFI_SVC_NOT_SET,       // no network saved yet
    WIFI_SVC_CONNECTING,
    WIFI_SVC_CONNECTED,
    WIFI_SVC_FAILED,        // last attempt failed, retrying in the background
    WIFI_SVC_OFF,           // radio turned off from the quick panel
} wifi_svc_state_t;

typedef struct {
    wifi_svc_state_t state;
    char ssid[WIFI_SVC_SSID_LEN];
    char ip[16];
    char bssid[18];             // MAC of the access point (router or extender) we are on
    int8_t rssi;
    const char *fail_reason;    // human-readable reason when state is FAILED
} wifi_svc_status_t;

typedef struct {
    char ssid[WIFI_SVC_SSID_LEN];
    int8_t rssi;
    bool secure;
} wifi_svc_ap_t;

/**
 * @brief Start WiFi and connect to the saved network, if any (NVS must be initialized)
 */
esp_err_t wifi_svc_init(void);

/**
 * @brief Save a network and connect to it
 */
esp_err_t wifi_svc_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect and forget the saved network
 */
esp_err_t wifi_svc_forget(void);

/**
 * @brief Turn the WiFi radio on or off (the saved network is kept)
 */
esp_err_t wifi_svc_set_enabled(bool enabled);

bool wifi_svc_is_enabled(void);

/**
 * @brief Start scanning for networks (takes ~2 s, then APP_EVT_WIFI_SCAN_DONE is posted)
 */
esp_err_t wifi_svc_scan_start(void);

/**
 * @brief Networks found by the last scan, strongest first, without duplicates
 *
 * @return number written to `out`
 */
int wifi_svc_scan_results(wifi_svc_ap_t *out, int max);

void wifi_svc_get_status(wifi_svc_status_t *status);
