/*
 * Time service: the one place the app asks "what time is it?".
 * The PCF85063 RTC keeps local time (APP_TIMEZONE) across resets. When WiFi is up,
 * the time is fetched from the internet (NTP) and written into the RTC.
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "pcf85063.h"

/**
 * @brief Start the service
 *
 * @param rtc RTC handle, or NULL if there is no RTC (time_svc_now() then returns false)
 */
esp_err_t time_svc_init(pcf85063_handle_t rtc);

/**
 * @brief Current local date and time
 *
 * @return false if no valid time is available
 */
bool time_svc_now(struct tm *now);

/**
 * @brief Ask the internet for the time (call when WiFi is up); posts APP_EVT_TIME_SYNCED when done
 */
void time_svc_sync_start(void);
