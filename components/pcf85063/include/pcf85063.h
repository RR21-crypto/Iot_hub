/*
 * Minimal driver for the NXP PCF85063 real-time clock (I2C, address 0x51).
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCF85063_I2C_ADDR   0x51

typedef struct pcf85063_t *pcf85063_handle_t;

/**
 * @brief Attach a PCF85063 to an existing I2C master bus and check that it answers
 */
esp_err_t pcf85063_new(i2c_master_bus_handle_t bus, uint8_t addr, pcf85063_handle_t *ret_rtc);

/**
 * @brief Detach the RTC from the bus and free the handle
 */
esp_err_t pcf85063_del(pcf85063_handle_t rtc);

/**
 * @brief Read the current date and time (24-hour, years 2000..2099)
 */
esp_err_t pcf85063_get_time(pcf85063_handle_t rtc, struct tm *time);

/**
 * @brief Set the date and time; also clears the "oscillator stopped" flag
 */
esp_err_t pcf85063_set_time(pcf85063_handle_t rtc, const struct tm *time);

/**
 * @brief Check whether the clock kept running since it was last set
 *
 * @param[out] valid false after the first power-up or a power loss (time must be set again)
 */
esp_err_t pcf85063_is_time_valid(pcf85063_handle_t rtc, bool *valid);

#ifdef __cplusplus
}
#endif
