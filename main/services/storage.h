/*
 * Storage on the internal flash: NVS (small settings, e.g. the WiFi network) and SPIFFS files:
 *   /gallery  photos and GIFs, rebuilt from data/gallery on every flash (read-only use)
 *   /user     notes and other user data, kept across flashes
 * LVGL opens files with the "S:" drive letter, e.g. "S:/gallery/anna.jpg".
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#define STORAGE_GALLERY_PATH    "/gallery"
#define STORAGE_USER_PATH       "/user"
#define STORAGE_LVGL_PREFIX     "S:"

typedef enum {
    STORAGE_GALLERY,
    STORAGE_USER,
} storage_area_t;

/**
 * @brief Init NVS and mount both SPIFFS partitions (the user partition is formatted on first use)
 *
 * @return ESP_OK if both are mounted; on failure the other one may still be usable
 */
esp_err_t storage_init(void);

/**
 * @brief Total and used bytes of a partition
 */
esp_err_t storage_get_usage(storage_area_t area, size_t *total, size_t *used);
