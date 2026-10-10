/*
 * WiFi service (station mode). See wifi_svc.h.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "core/app_sm.h"
#include "wifi_svc.h"

static const char *TAG = "wifi_svc";

#define WIFI_RETRY_DELAY_US     (5 * 1000 * 1000)

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static wifi_svc_status_t status;            // guarded by status_lock
static esp_timer_handle_t retry_timer;
static bool scanning;
static bool radio_on = true;              // false = turned off from the quick panel

static void set_state(wifi_svc_state_t state, const char *reason)
{
    portENTER_CRITICAL(&status_lock);
    status.state = state;
    status.fail_reason = reason;
    if (state != WIFI_SVC_CONNECTED) {
        status.ip[0] = '\0';
    }
    portEXIT_CRITICAL(&status_lock);
}

static bool has_saved_network(void)
{
    wifi_config_t cfg;
    return esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0';
}

static const char *reason_text(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Wrong password?";
    case WIFI_REASON_NO_AP_FOUND:
        return "Network not found";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Signal lost";
    default:
        return "Disconnected";
    }
}

static void retry_cb(void *arg)
{
    // The disconnect done by wifi_svc_connect() also arms this timer: don't reconnect if already up
    if (radio_on && has_saved_network() && !scanning && status.state != WIFI_SVC_CONNECTED) {
        set_state(WIFI_SVC_CONNECTING, NULL);
        esp_wifi_connect();
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        retry_cb(NULL);
        break;
    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *ev = data;
        bool was_connected = status.state == WIFI_SVC_CONNECTED;
        ESP_LOGW(TAG, "disconnected, reason %d", ev->reason);
        if (!radio_on) {
            set_state(WIFI_SVC_OFF, NULL);
        } else if (has_saved_network()) {
            set_state(WIFI_SVC_FAILED, reason_text(ev->reason));
            esp_timer_stop(retry_timer);
            esp_timer_start_once(retry_timer, WIFI_RETRY_DELAY_US);
        } else {
            set_state(WIFI_SVC_NOT_SET, NULL);
        }
        if (was_connected) {
            app_sm_post(APP_EVT_WIFI_DOWN, 0);
        }
        break;
    }
    case WIFI_EVENT_SCAN_DONE:
        scanning = false;
        app_sm_post(APP_EVT_WIFI_SCAN_DONE, 0);
        break;
    default:
        break;
    }
}

static void ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *ev = data;
    wifi_ap_record_t ap;
    bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;

    portENTER_CRITICAL(&status_lock);
    status.state = WIFI_SVC_CONNECTED;
    status.fail_reason = NULL;
    snprintf(status.ip, sizeof(status.ip), IPSTR, IP2STR(&ev->ip_info.ip));
    if (have_ap) {
        strlcpy(status.ssid, (const char *)ap.ssid, sizeof(status.ssid));
        status.rssi = ap.rssi;
        snprintf(status.bssid, sizeof(status.bssid), MACSTR, MAC2STR(ap.bssid));
    }
    portEXIT_CRITICAL(&status_lock);

    ESP_LOGI(TAG, "connected, IP " IPSTR ", access point %s", IP2STR(&ev->ip_info.ip), status.bssid);
    app_sm_post(APP_EVT_WIFI_UP, 0);
}

esp_err_t wifi_svc_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop failed");
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), TAG, "");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, NULL), TAG, "");

    const esp_timer_create_args_t timer_args = { .callback = retry_cb, .name = "wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &retry_timer), TAG, "timer create failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");

    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0]) {
        strlcpy(status.ssid, (const char *)cfg.sta.ssid, sizeof(status.ssid));
        ESP_LOGI(TAG, "saved network: %s", status.ssid);
    } else {
        ESP_LOGI(TAG, "no saved network, use Settings > WiFi");
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start failed");     // STA_START then connects
    // No power saving: the board is USB powered, and many WiFi extenders drop packets for
    // sleeping clients, which makes the board unreachable from a laptop (OTA page, ping)
    return esp_wifi_set_ps(WIFI_PS_NONE);
}

esp_err_t wifi_svc_connect(const char *ssid, const char *password)
{
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    esp_timer_stop(retry_timer);
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config failed");   // saved in NVS

    portENTER_CRITICAL(&status_lock);
    strlcpy(status.ssid, ssid, sizeof(status.ssid));
    portEXIT_CRITICAL(&status_lock);
    set_state(WIFI_SVC_CONNECTING, NULL);
    ESP_LOGI(TAG, "connecting to %s", ssid);
    return esp_wifi_connect();
}

esp_err_t wifi_svc_forget(void)
{
    wifi_config_t cfg = { 0 };
    esp_timer_stop(retry_timer);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "set config failed");
    esp_wifi_disconnect();
    portENTER_CRITICAL(&status_lock);
    status.ssid[0] = '\0';
    portEXIT_CRITICAL(&status_lock);
    set_state(WIFI_SVC_NOT_SET, NULL);
    return ESP_OK;
}

esp_err_t wifi_svc_set_enabled(bool enabled)
{
    if (enabled == radio_on) {
        return ESP_OK;
    }
    radio_on = enabled;
    if (!enabled) {
        bool was_connected = status.state == WIFI_SVC_CONNECTED;
        esp_timer_stop(retry_timer);
        esp_wifi_stop();
        set_state(WIFI_SVC_OFF, NULL);
        ESP_LOGI(TAG, "off");
        if (was_connected) {
            app_sm_post(APP_EVT_WIFI_DOWN, 0);
        }
        return ESP_OK;
    }
    set_state(WIFI_SVC_NOT_SET, NULL);
    ESP_LOGI(TAG, "on");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start failed");    // STA_START reconnects
    return esp_wifi_set_ps(WIFI_PS_NONE);
}

bool wifi_svc_is_enabled(void)
{
    return radio_on;
}

esp_err_t wifi_svc_scan_start(void)
{
    if (!radio_on) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_timer_stop(retry_timer);    // a pending reconnect would make the scan fail
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    scanning = err == ESP_OK;
    return err;
}

int wifi_svc_scan_results(wifi_svc_ap_t *out, int max)
{
    static wifi_ap_record_t records[WIFI_SVC_MAX_APS];
    uint16_t n = WIFI_SVC_MAX_APS;
    if (esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) {
        return 0;
    }
    // Records come sorted by signal strength; keep the first of each name
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        const char *ssid = (const char *)records[i].ssid;
        bool dup = ssid[0] == '\0';     // hidden networks are skipped too
        for (int j = 0; j < count && !dup; j++) {
            dup = strcmp(out[j].ssid, ssid) == 0;
        }
        if (dup) {
            continue;
        }
        strlcpy(out[count].ssid, ssid, sizeof(out[count].ssid));
        out[count].rssi = records[i].rssi;
        out[count].secure = records[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    // Resume reconnecting to the saved network if we are not connected
    if (status.state != WIFI_SVC_CONNECTED && has_saved_network()) {
        esp_timer_start_once(retry_timer, 100 * 1000);
    }
    return count;
}

void wifi_svc_get_status(wifi_svc_status_t *out)
{
    portENTER_CRITICAL(&status_lock);
    *out = status;
    portEXIT_CRITICAL(&status_lock);
    if (out->state == WIFI_SVC_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
        }
    }
}
