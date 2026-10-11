/*
 * Anna Hub for the Waveshare ESP32-S3-Touch-LCD-4 (V3.0).
 *
 * Boot: storage -> RTC/time -> display -> state machine -> UI -> INIT_DONE -> WiFi.
 * After that everything is driven by core/app_sm.c; apps live in apps/ (see core/app_registry.h).
 * This file only wires the modules together.
 */
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "pcf85063.h"
#include "main.h"
#include "core/app_sm.h"
#include "services/ota_svc.h"
#include "services/storage.h"
#include "services/time_svc.h"
#include "services/weather_svc.h"
#include "services/wifi_svc.h"
#include "ui/launcher.h"
#include "ui/quick_panel.h"
#include "ui/statusbar.h"
#include "ui/ui_common.h"

static const char *TAG = "main";

void app_main(void)
{
    // 1. NVS + files: gallery (read-only) and user data. The app still runs without the files.
    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "storage not fully available");
    }
    weather_svc_init();     // loads the saved weather location from NVS

    // 2. RTC + time service (optional: the clock shows --:-- without it)
    pcf85063_handle_t rtc = NULL;
    if (pcf85063_new(bsp_i2c_get_handle(), PCF85063_I2C_ADDR, &rtc) != ESP_OK) {
        ESP_LOGW(TAG, "RTC not found, clock disabled");
    }
    time_svc_init(rtc);

    // 3. Display + touch
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
    };
    disp_cfg.lvgl_port_cfg.task_stack = LVGL_TASK_STACK;
    if (bsp_display_start_with_config(&disp_cfg) == NULL) {
        ESP_LOGE(TAG, "Display init failed - check board revision (this BSP is for V3.0)");
        return;
    }
    bsp_display_backlight_on();

    // 4. State machine first, so it is ready to receive the result of the UI init
    ESP_ERROR_CHECK(app_sm_start());

    // 5. UI: theme, status bar, quick panel, home screen (shown when the state machine enters HOME)
    bsp_display_lock(0);
    ui_common_init();
    statusbar_create();
    quick_panel_create();
    bool ui_ok = launcher_create(app_sm_request_open) != NULL;
    bsp_display_unlock();

    app_sm_post(ui_ok ? APP_EVT_INIT_DONE : APP_EVT_INIT_FAILED, 0);
    if (!ui_ok) {
        return;
    }

    // 6. We got this far, so this firmware works: cancel the OTA rollback
    ota_svc_mark_valid();

    // 7. WiFi last: it reports WIFI_UP to the state machine, which then starts NTP and the OTA page
    if (wifi_svc_init() != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed");
    }
}
