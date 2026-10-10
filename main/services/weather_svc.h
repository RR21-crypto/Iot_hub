/*
 * Weather service: current weather and a 3-day forecast from Open-Meteo (HTTPS, no API key).
 *
 * Location:
 *   auto    the city is guessed from the internet connection (IP geolocation, ip-api.com);
 *           this can be off by a city or two, because it is where the provider's network is
 *   manual  a city picked with weather_svc_search() (Open-Meteo geocoding)
 * The choice is saved in NVS. If auto fails, WEATHER_DEFAULT_* from main.h is used.
 *
 * Network work runs in its own task and ends with APP_EVT_WEATHER_UPDATED (arg 0 = ok, -1 = failed)
 * or APP_EVT_WEATHER_SEARCH_DONE (arg = number of results, -1 = failed).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define WEATHER_FORECAST_DAYS   3
#define WEATHER_NAME_LEN        48
#define WEATHER_MAX_RESULTS     6

typedef struct {
    int code;               // WMO weather code
    float temp_max;
    float temp_min;
    int wday;               // 0 = Sunday
} weather_day_t;

typedef struct {
    float temp;             // degrees Celsius
    int humidity;           // percent
    float wind;             // km/h
    int code;               // WMO weather code
    float today_max;
    float today_min;
    weather_day_t days[WEATHER_FORECAST_DAYS];     // the next days, tomorrow first
    int64_t fetched_us;     // esp_timer time of the fetch
} weather_t;

typedef struct {
    bool is_auto;
    char name[WEATHER_NAME_LEN];    // e.g. "Batam, Riau Islands"
    float lat;
    float lon;
} weather_location_t;

typedef enum {
    WEATHER_ICON_CLEAR,
    WEATHER_ICON_PARTLY,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_FOG,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_THUNDER,
    WEATHER_ICON_SNOW,
} weather_icon_t;

/**
 * @brief Load the saved location (NVS must be initialized)
 */
esp_err_t weather_svc_init(void);

/**
 * @brief Start fetching new data in the background (needs WiFi). Does nothing if one is running.
 */
esp_err_t weather_svc_refresh(void);

/**
 * @return false if no data was fetched yet for the current location
 */
bool weather_svc_get(weather_t *out);

void weather_svc_get_location(weather_location_t *out);

/**
 * @brief Use the automatic location from now on (saved), and fetch new data
 */
esp_err_t weather_svc_set_auto(void);

/**
 * @brief Use this place from now on (saved), and fetch new data
 */
esp_err_t weather_svc_set_location(const weather_location_t *loc);

/**
 * @brief Search cities by name in the background (APP_EVT_WEATHER_SEARCH_DONE when done)
 */
esp_err_t weather_svc_search(const char *query);

/**
 * @return number of places found by the last search, written to `out`
 */
int weather_svc_search_results(weather_location_t *out, int max);

/**
 * @brief Short English description of a WMO weather code ("Clear", "Rain"...)
 */
const char *weather_svc_describe(int code);

weather_icon_t weather_svc_icon(int code);
