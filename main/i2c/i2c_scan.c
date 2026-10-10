/*
 * I2C bus scanner: finds every device that answers on the board I2C bus.
 */
#include <stdio.h>
#include "esp_log.h"
#include "i2c_scan.h"

static const char *TAG = "i2c_scan";

#define I2C_SCAN_FIRST_ADDR     0x08
#define I2C_SCAN_LAST_ADDR      0x77
#define I2C_SCAN_TIMEOUT_MS     50

// Known chips on the Waveshare ESP32-S3-Touch-LCD-4 (V3.0)
static const struct {
    uint8_t addr;
    const char *name;
} known_devices[] = {
    { 0x14, "GT911 touch" },
    { 0x20, "TCA9554 IO expander" },
    { 0x51, "PCF85063 RTC" },
    { 0x5D, "GT911 touch" },
};

const char *i2c_scan_device_name(uint8_t addr)
{
    for (size_t i = 0; i < sizeof(known_devices) / sizeof(known_devices[0]); i++) {
        if (known_devices[i].addr == addr) {
            return known_devices[i].name;
        }
    }
    return "unknown";
}

size_t i2c_scan(i2c_master_bus_handle_t bus, uint8_t *found, size_t max_found)
{
    size_t count = 0;
    for (uint16_t addr = I2C_SCAN_FIRST_ADDR; addr <= I2C_SCAN_LAST_ADDR; addr++) {
        if (i2c_master_probe(bus, addr, I2C_SCAN_TIMEOUT_MS) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "I2C device found at 0x%02X (%s)", addr, i2c_scan_device_name(addr));
        if (found && count < max_found) {
            found[count] = addr;
        }
        count++;
    }
    return count;
}

void i2c_scan_format(const uint8_t *devices, size_t count, char *buf, size_t buf_len)
{
    if (count == 0) {
        snprintf(buf, buf_len, "None");
        return;
    }
    size_t len = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < count && len < buf_len; i++) {
        len += snprintf(buf + len, buf_len - len, "%s0x%02X  %s",
                        i ? "\n" : "", devices[i], i2c_scan_device_name(devices[i]));
    }
}
