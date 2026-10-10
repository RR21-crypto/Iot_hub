idf_component_register(
    SRCS "esp32_s3_touch_lcd_4.c" ${SRC_VER}
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_driver_i2c  esp_driver_gpio esp_lcd esp_driver_sdmmc
    # ESP-IDF v6 split the legacy "driver" component; list the pieces this BSP uses
    PRIV_REQUIRES esp_timer spiffs esp_psram fatfs esp_driver_ledc
)


# Initializer macros from esp_lcd_st7701 trigger these warnings under ESP-IDF v6's stricter flags
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-braces -Wno-override-init)
