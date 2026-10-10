/*
 * Keeping the RTC time sensible.
 */
#pragma once

#include "esp_err.h"
#include "pcf85063.h"

/**
 * @brief If the RTC lost its time (first power-up, battery out), set it to the firmware build time
 *
 * The build time is when main/rtc/rtc_time.c was last compiled, so it can be a little behind.
 */
esp_err_t rtc_time_set_build_time_if_invalid(pcf85063_handle_t rtc);
