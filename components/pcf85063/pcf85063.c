/*
 * Minimal driver for the NXP PCF85063 real-time clock.
 * Datasheet: registers 0x04..0x0A hold seconds..years in BCD.
 */
#include <stdlib.h>
#include "esp_check.h"
#include "pcf85063.h"

static const char *TAG = "pcf85063";

#define PCF85063_REG_CTRL1      0x00
#define PCF85063_REG_SECONDS    0x04    // seconds..years follow (7 registers)
#define PCF85063_OS_FLAG        0x80    // bit 7 of SECONDS: oscillator stopped, time not reliable
#define PCF85063_CTRL1_12H      0x02    // 1 = 12-hour mode
#define PCF85063_I2C_SPEED_HZ   400000
#define PCF85063_TIMEOUT_MS     100

struct pcf85063_t {
    i2c_master_dev_handle_t dev;
};

static uint8_t bcd_to_dec(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

static uint8_t dec_to_bcd(int dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

static esp_err_t read_regs(pcf85063_handle_t rtc, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(rtc->dev, &reg, 1, data, len, PCF85063_TIMEOUT_MS);
}

static esp_err_t write_reg(pcf85063_handle_t rtc, uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(rtc->dev, buf, sizeof(buf), PCF85063_TIMEOUT_MS);
}

esp_err_t pcf85063_new(i2c_master_bus_handle_t bus, uint8_t addr, pcf85063_handle_t *ret_rtc)
{
    ESP_RETURN_ON_FALSE(bus && ret_rtc, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    pcf85063_handle_t rtc = calloc(1, sizeof(*rtc));
    ESP_RETURN_ON_FALSE(rtc, ESP_ERR_NO_MEM, TAG, "no memory");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = PCF85063_I2C_SPEED_HZ,
    };
    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_cfg, &rtc->dev);
    ESP_GOTO_ON_ERROR(ret, err_free, TAG, "add device failed");

    // Read CTRL1 to make sure the chip answers, and force 24-hour mode
    uint8_t ctrl1;
    ESP_GOTO_ON_ERROR(read_regs(rtc, PCF85063_REG_CTRL1, &ctrl1, 1), err_dev, TAG, "no answer at 0x%02X", addr);
    if (ctrl1 & PCF85063_CTRL1_12H) {
        ESP_GOTO_ON_ERROR(write_reg(rtc, PCF85063_REG_CTRL1, ctrl1 & ~PCF85063_CTRL1_12H), err_dev, TAG, "set 24h failed");
    }

    *ret_rtc = rtc;
    return ESP_OK;

err_dev:
    i2c_master_bus_rm_device(rtc->dev);
err_free:
    free(rtc);
    return ret;
}

esp_err_t pcf85063_del(pcf85063_handle_t rtc)
{
    ESP_RETURN_ON_FALSE(rtc, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    i2c_master_bus_rm_device(rtc->dev);
    free(rtc);
    return ESP_OK;
}

esp_err_t pcf85063_get_time(pcf85063_handle_t rtc, struct tm *time)
{
    ESP_RETURN_ON_FALSE(rtc && time, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    uint8_t r[7];
    ESP_RETURN_ON_ERROR(read_regs(rtc, PCF85063_REG_SECONDS, r, sizeof(r)), TAG, "read time failed");

    *time = (struct tm) {
        .tm_sec  = bcd_to_dec(r[0] & 0x7F),
        .tm_min  = bcd_to_dec(r[1] & 0x7F),
        .tm_hour = bcd_to_dec(r[2] & 0x3F),
        .tm_mday = bcd_to_dec(r[3] & 0x3F),
        .tm_wday = r[4] & 0x07,
        .tm_mon  = bcd_to_dec(r[5] & 0x1F) - 1,
        .tm_year = bcd_to_dec(r[6]) + 100,      // struct tm counts from 1900, chip from 2000
    };
    return ESP_OK;
}

esp_err_t pcf85063_set_time(pcf85063_handle_t rtc, const struct tm *time)
{
    ESP_RETURN_ON_FALSE(rtc && time, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(time->tm_year >= 100 && time->tm_year < 200, ESP_ERR_INVALID_ARG, TAG, "year must be 2000..2099");

    const uint8_t buf[8] = {
        PCF85063_REG_SECONDS,
        dec_to_bcd(time->tm_sec),       // OS flag = 0: clears "oscillator stopped"
        dec_to_bcd(time->tm_min),
        dec_to_bcd(time->tm_hour),
        dec_to_bcd(time->tm_mday),
        (uint8_t)(time->tm_wday & 0x07),
        dec_to_bcd(time->tm_mon + 1),
        dec_to_bcd(time->tm_year - 100),
    };
    return i2c_master_transmit(rtc->dev, buf, sizeof(buf), PCF85063_TIMEOUT_MS);
}

esp_err_t pcf85063_is_time_valid(pcf85063_handle_t rtc, bool *valid)
{
    ESP_RETURN_ON_FALSE(rtc && valid, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    uint8_t sec;
    ESP_RETURN_ON_ERROR(read_regs(rtc, PCF85063_REG_SECONDS, &sec, 1), TAG, "read failed");
    *valid = !(sec & PCF85063_OS_FLAG);
    return ESP_OK;
}
