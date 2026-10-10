/*
 * Keeping the RTC time sensible.
 */
#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "rtc_time.h"

static const char *TAG = "rtc_time";

// __DATE__ is "Mmm dd yyyy", __TIME__ is "hh:mm:ss"
static void build_time(struct tm *t)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4];

    *t = (struct tm) { 0 };
    sscanf(__DATE__, "%3s %d %d", mon, &t->tm_mday, &t->tm_year);
    sscanf(__TIME__, "%d:%d:%d", &t->tm_hour, &t->tm_min, &t->tm_sec);
    t->tm_mon = (strstr(months, mon) - months) / 3;
    t->tm_year -= 1900;
    mktime(t);      // fills in tm_wday
}

esp_err_t rtc_time_set_build_time_if_invalid(pcf85063_handle_t rtc)
{
    bool valid;
    ESP_RETURN_ON_ERROR(pcf85063_is_time_valid(rtc, &valid), TAG, "read RTC status failed");
    if (valid) {
        return ESP_OK;
    }

    struct tm t;
    build_time(&t);
    ESP_RETURN_ON_ERROR(pcf85063_set_time(rtc, &t), TAG, "set RTC failed");
    ESP_LOGW(TAG, "RTC was not set, using build time %s %s", __DATE__, __TIME__);
    return ESP_OK;
}
