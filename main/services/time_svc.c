/*
 * Time service. See time_svc.h.
 */
#include <stdlib.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "main.h"
#include "core/app_sm.h"
#include "rtc/rtc_time.h"
#include "time_svc.h"

static const char *TAG = "time_svc";

static pcf85063_handle_t rtc_dev;
static bool sntp_started;

esp_err_t time_svc_init(pcf85063_handle_t rtc)
{
    setenv("TZ", APP_TIMEZONE, 1);
    tzset();

    rtc_dev = rtc;
    if (!rtc_dev) {
        return ESP_ERR_NOT_FOUND;
    }
    return rtc_time_set_build_time_if_invalid(rtc_dev);
}

bool time_svc_now(struct tm *now)
{
    return rtc_dev && pcf85063_get_time(rtc_dev, now) == ESP_OK;
}

// Runs in the network task when NTP has set the system clock (UTC)
static void sntp_synced(struct timeval *tv)
{
    struct tm local;
    localtime_r(&tv->tv_sec, &local);   // UTC -> APP_TIMEZONE
    if (rtc_dev && pcf85063_set_time(rtc_dev, &local) == ESP_OK) {
        ESP_LOGI(TAG, "RTC set from NTP: %04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
                 local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
    }
    app_sm_post(APP_EVT_TIME_SYNCED, 0);
}

void time_svc_sync_start(void)
{
    if (sntp_started) {
        esp_netif_sntp_start();     // WiFi came back: sync again
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_NTP_SERVER);
    cfg.sync_cb = sntp_synced;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        sntp_started = true;
        ESP_LOGI(TAG, "NTP sync started (%s)", APP_NTP_SERVER);
    }
}
