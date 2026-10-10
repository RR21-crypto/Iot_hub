/*
 * I2C bus scanner: finds every device that answers on the board I2C bus.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"

/**
 * @brief Probe addresses 0x08..0x77 and log each device found
 *
 * @param found     array that receives the addresses found (may be NULL)
 * @param max_found size of the array
 * @return number of devices found
 */
size_t i2c_scan(i2c_master_bus_handle_t bus, uint8_t *found, size_t max_found);

/**
 * @brief Name of the chip at this address on the ESP32-S3-Touch-LCD-4 V3 board
 */
const char *i2c_scan_device_name(uint8_t addr);

/**
 * @brief Write the device list as text, one "0xNN  name" per line ("None" if empty)
 */
void i2c_scan_format(const uint8_t *devices, size_t count, char *buf, size_t buf_len);
