/*
 * Application-wide settings for the Anna Hub.
 */
#pragma once

#define APP_DISPLAY_NAME    "Anna Hub"              // shown in the status bar and on the OTA page

#define APP_TIMEZONE        "WIB-7"                 // POSIX TZ: UTC+7 (WITA-8, WIT-9)
#define APP_NTP_SERVER      "pool.ntp.org"

// Weather app: place used when the automatic location fails (change it in the app)
#define WEATHER_DEFAULT_CITY        "Jakarta"
#define WEATHER_DEFAULT_LATITUDE    -6.2088f
#define WEATHER_DEFAULT_LONGITUDE   106.8456f

#define I2C_MAX_DEVICES     8                       // most devices listed in Settings
#define I2C_TEXT_LEN        (I2C_MAX_DEVICES * 32)  // buffer for the formatted device list
