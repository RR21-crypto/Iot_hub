/*
 * File storage on the internal flash (SPIFFS). See storage.h.
 */
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "storage.h"

static const char *TAG = "storage";

#define STORAGE_MAX_OPEN_FILES  4

static const struct {
    const char *label;      // partition name in partitions.csv
    const char *path;       // mount point
    bool format_if_empty;
} areas[] = {
    [STORAGE_GALLERY] = { "gallery", STORAGE_GALLERY_PATH, false },
    [STORAGE_USER]    = { "user",    STORAGE_USER_PATH,    true },
};

static esp_err_t mount(storage_area_t area)
{
    const esp_vfs_spiffs_conf_t conf = {
        .base_path = areas[area].path,
        .partition_label = areas[area].label,
        .max_files = STORAGE_MAX_OPEN_FILES,
        .format_if_mount_failed = areas[area].format_if_empty,
    };
    if (areas[area].format_if_empty) {
        ESP_LOGI(TAG, "Mounting %s (first boot formats it, this can take a few seconds)", conf.base_path);
    }
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&conf), TAG, "mount %s failed", conf.base_path);

    size_t total, used;
    if (storage_get_usage(area, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "%s: %u KB used of %u KB", conf.base_path, (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
    return ESP_OK;
}

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition layout changed or full: start with an empty NVS
        ESP_LOGW(TAG, "erasing NVS");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t storage_init(void)
{
    ESP_RETURN_ON_ERROR(nvs_init(), TAG, "NVS init failed");
    esp_err_t gallery = mount(STORAGE_GALLERY);
    esp_err_t user = mount(STORAGE_USER);
    return gallery != ESP_OK ? gallery : user;
}

esp_err_t storage_get_usage(storage_area_t area, size_t *total, size_t *used)
{
    return esp_spiffs_info(areas[area].label, total, used);
}
