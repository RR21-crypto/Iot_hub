idf_component_register(SRCS "main.c"
                            "core/app_registry.c"
                            "core/app_sm.c"
                            "ui/launcher.c"
                            "ui/quick_panel.c"
                            "ui/statusbar.c"
                            "ui/ui_common.c"
                            "apps/gallery/gallery_app.c"
                            "apps/notes/notes_app.c"
                            "apps/notes/notes_store.c"
                            "apps/settings/settings_app.c"
                            "apps/watch/watch_app.c"
                            "apps/weather/weather_app.c"
                            "assets/weather_icons.c"
                            "services/bt_svc.c"
                            "services/ota_svc.c"
                            "services/storage.c"
                            "services/time_svc.c"
                            "services/weather_svc.c"
                            "services/wifi_svc.c"
                            "i2c/i2c_scan.c"
                            "rtc/rtc_time.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp32_s3_touch_lcd_4 esp_driver_i2c esp_timer spiffs nvs_flash pcf85063
                                esp_wifi esp_netif esp_event esp_http_server esp_http_client mbedtls
                                app_update esp_app_format bt)

# data/gallery -> "gallery" SPIFFS partition, written by every normal flash
spiffs_create_partition_image(gallery ${PROJECT_DIR}/data/gallery FLASH_IN_PROJECT)
