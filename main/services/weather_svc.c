/*
 * Weather service. See weather_svc.h.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "main.h"
#include "core/app_sm.h"
#include "weather_svc.h"

static const char *TAG = "weather";

#define FORECAST_URL_FMT    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f" \
                            "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m" \
                            "&daily=weather_code,temperature_2m_max,temperature_2m_min" \
                            "&timezone=auto&forecast_days=4"
#define SEARCH_URL_FMT      "https://geocoding-api.open-meteo.com/v1/search?count=%d&language=en&format=json&name=%s"
// Free IP geolocation; the free tier is plain HTTP only
#define GEOIP_URL           "http://ip-api.com/json/?fields=status,city,regionName,lat,lon"

#define URL_LEN             384
#define BUF_SIZE            6144
#define TASK_STACK          8192    // TLS needs a big stack
#define HTTP_TIMEOUT_MS     10000
#define NVS_NAMESPACE       "weather"
#define NVS_KEY_LOCATION    "location"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
// All guarded by lock
static weather_t data;
static bool have_data;
static weather_location_t location;
static bool auto_located;           // auto mode: the IP lookup succeeded this boot
static weather_location_t results[WEATHER_MAX_RESULTS];
static int result_count;
static char search_query[64];

static volatile bool busy;          // one network task at a time

/* ---------- descriptions ---------- */

const char *weather_svc_describe(int code)
{
    // WMO codes: https://open-meteo.com/en/docs
    if (code == 0) return "Clear";
    if (code <= 2) return "Partly cloudy";
    if (code == 3) return "Cloudy";
    if (code <= 48) return "Fog";
    if (code <= 57) return "Drizzle";
    if (code <= 67) return "Rain";
    if (code <= 77) return "Snow";
    if (code <= 82) return "Showers";
    if (code <= 86) return "Snow showers";
    return "Thunderstorm";
}

weather_icon_t weather_svc_icon(int code)
{
    if (code == 0) return WEATHER_ICON_CLEAR;
    if (code <= 2) return WEATHER_ICON_PARTLY;
    if (code == 3) return WEATHER_ICON_CLOUDY;
    if (code <= 48) return WEATHER_ICON_FOG;
    if (code <= 67) return WEATHER_ICON_RAIN;
    if (code <= 77) return WEATHER_ICON_SNOW;
    if (code <= 82) return WEATHER_ICON_RAIN;
    if (code <= 86) return WEATHER_ICON_SNOW;
    return WEATHER_ICON_THUNDER;
}

/* ---------- helpers ---------- */

// Download url into buf (NUL-terminated)
static esp_err_t http_get(const char *url, char *buf, size_t size)
{
    const esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,     // trusted root certificates for HTTPS
        .timeout_ms = HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        int total = 0, n;
        while (total < (int)size - 1 && (n = esp_http_client_read(client, buf + total, size - 1 - total)) > 0) {
            total += n;
        }
        buf[total] = '\0';
        if (status != 200 || total == 0) {
            ESP_LOGE(TAG, "HTTP status %d, %d bytes", status, total);
            err = ESP_FAIL;
        }
    }
    esp_http_client_cleanup(client);
    return err;
}

static void url_encode(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (; *in && o + 4 < len; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '-' || c == '.' || c == '_') {
            out[o++] = c;
        } else {
            o += snprintf(out + o, len - o, "%%%02X", c);
        }
    }
    out[o] = '\0';
}

static float num(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : 0;
}

static float num_at(const cJSON *obj, const char *key, int i)
{
    const cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(obj, key), i);
    return cJSON_IsNumber(item) ? (float)item->valuedouble : 0;
}

static const char *str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) ? item->valuestring : "";
}

// A display name may be cut short, that is fine
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static void make_name(char *out, size_t len, const char *city, const char *region)
{
    if (region[0] && strcmp(city, region) != 0) {
        snprintf(out, len, "%s, %s", city, region);
    } else {
        snprintf(out, len, "%s", city);
    }
}
#pragma GCC diagnostic pop

static int wday_of(const char *date)
{
    struct tm t = { 0 };
    if (sscanf(date, "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) {
        return 0;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_hour = 12;
    mktime(&t);
    return t.tm_wday;
}

static void save_location(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, NVS_KEY_LOCATION, &location, sizeof(location));
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static void set_default_location(weather_location_t *loc)
{
    snprintf(loc->name, sizeof(loc->name), "%s", WEATHER_DEFAULT_CITY);
    loc->lat = WEATHER_DEFAULT_LATITUDE;
    loc->lon = WEATHER_DEFAULT_LONGITUDE;
}

/* ---------- network steps (run in the worker task) ---------- */

// Auto mode: find the city from our public IP address
static void geolocate(char *buf)
{
    if (http_get(GEOIP_URL, buf, BUF_SIZE) != ESP_OK) {
        return;
    }
    cJSON *root = cJSON_Parse(buf);
    if (root && strcmp(str(root, "status"), "success") == 0) {
        weather_location_t loc = { .is_auto = true, .lat = num(root, "lat"), .lon = num(root, "lon") };
        make_name(loc.name, sizeof(loc.name), str(root, "city"), str(root, "regionName"));
        portENTER_CRITICAL(&lock);
        if (location.is_auto) {
            location = loc;
            auto_located = true;
        }
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG, "auto location: %s (%.3f, %.3f)", loc.name, loc.lat, loc.lon);
    }
    cJSON_Delete(root);
}

static bool parse_forecast(const char *json, weather_t *out)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *cur = cJSON_GetObjectItem(root, "current");
    const cJSON *daily = cJSON_GetObjectItem(root, "daily");
    bool ok = cur && daily;
    if (ok) {
        out->temp = num(cur, "temperature_2m");
        out->humidity = (int)num(cur, "relative_humidity_2m");
        out->wind = num(cur, "wind_speed_10m");
        out->code = (int)num(cur, "weather_code");
        out->today_max = num_at(daily, "temperature_2m_max", 0);
        out->today_min = num_at(daily, "temperature_2m_min", 0);
        const cJSON *dates = cJSON_GetObjectItem(daily, "time");
        for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
            const cJSON *date = cJSON_GetArrayItem(dates, i + 1);   // index 0 is today
            out->days[i] = (weather_day_t) {
                .code = (int)num_at(daily, "weather_code", i + 1),
                .temp_max = num_at(daily, "temperature_2m_max", i + 1),
                .temp_min = num_at(daily, "temperature_2m_min", i + 1),
                .wday = cJSON_IsString(date) ? wday_of(date->valuestring) : 0,
            };
        }
        out->fetched_us = esp_timer_get_time();
    }
    cJSON_Delete(root);
    return ok;
}

static void forecast_task(void *arg)
{
    char *buf = malloc(BUF_SIZE);
    bool ok = false;
    if (buf) {
        weather_location_t loc;
        portENTER_CRITICAL(&lock);
        bool need_geo = location.is_auto && !auto_located;
        portEXIT_CRITICAL(&lock);
        if (need_geo) {
            geolocate(buf);
        }
        weather_svc_get_location(&loc);

        char url[URL_LEN];
        snprintf(url, sizeof(url), FORECAST_URL_FMT, loc.lat, loc.lon);
        weather_t fresh;
        ok = http_get(url, buf, BUF_SIZE) == ESP_OK && parse_forecast(buf, &fresh);
        if (ok) {
            portENTER_CRITICAL(&lock);
            data = fresh;
            have_data = true;
            portEXIT_CRITICAL(&lock);
            ESP_LOGI(TAG, "%s: %.1f C, %s", loc.name, fresh.temp, weather_svc_describe(fresh.code));
        }
        free(buf);
    }
    if (!ok) {
        ESP_LOGW(TAG, "update failed");
    }
    busy = false;
    app_sm_post(APP_EVT_WEATHER_UPDATED, ok ? 0 : -1);
    vTaskDelete(NULL);
}

static void search_task(void *arg)
{
    char *buf = malloc(BUF_SIZE);
    int count = -1;
    if (buf) {
        char query[3 * sizeof(search_query)], url[URL_LEN];
        url_encode(search_query, query, sizeof(query));
        snprintf(url, sizeof(url), SEARCH_URL_FMT, WEATHER_MAX_RESULTS, query);
        if (http_get(url, buf, BUF_SIZE) == ESP_OK) {
            cJSON *root = cJSON_Parse(buf);
            const cJSON *list = cJSON_GetObjectItem(root, "results");
            weather_location_t found[WEATHER_MAX_RESULTS];
            count = 0;
            const cJSON *item;
            cJSON_ArrayForEach(item, list) {
                if (count >= WEATHER_MAX_RESULTS) {
                    break;
                }
                found[count] = (weather_location_t) { .lat = num(item, "latitude"), .lon = num(item, "longitude") };
                char region[64];
                snprintf(region, sizeof(region), "%s", str(item, "admin1")[0] ? str(item, "admin1") : str(item, "country"));
                make_name(found[count].name, sizeof(found[count].name), str(item, "name"), region);
                count++;
            }
            cJSON_Delete(root);
            portENTER_CRITICAL(&lock);
            memcpy(results, found, sizeof(found[0]) * count);
            result_count = count;
            portEXIT_CRITICAL(&lock);
        }
        free(buf);
    }
    ESP_LOGI(TAG, "search \"%s\": %d result(s)", search_query, count);
    busy = false;
    app_sm_post(APP_EVT_WEATHER_SEARCH_DONE, count);
    vTaskDelete(NULL);
}

static esp_err_t start_task(TaskFunction_t fn, const char *name)
{
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    busy = true;
    if (xTaskCreate(fn, name, TASK_STACK, NULL, 3, NULL) != pdPASS) {
        busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/* ---------- API ---------- */

esp_err_t weather_svc_init(void)
{
    weather_location_t loc = { .is_auto = true };
    set_default_location(&loc);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(loc);
        weather_location_t saved;
        if (nvs_get_blob(nvs, NVS_KEY_LOCATION, &saved, &len) == ESP_OK && len == sizeof(saved)) {
            loc = saved;
        }
        nvs_close(nvs);
    }
    location = loc;
    ESP_LOGI(TAG, "location: %s%s", loc.is_auto ? "auto, last " : "", loc.name);
    return ESP_OK;
}

esp_err_t weather_svc_refresh(void)
{
    esp_err_t err = start_task(forecast_task, "weather");
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;     // already running is fine
}

bool weather_svc_get(weather_t *out)
{
    portENTER_CRITICAL(&lock);
    bool ok = have_data;
    if (ok) {
        *out = data;
    }
    portEXIT_CRITICAL(&lock);
    return ok;
}

void weather_svc_get_location(weather_location_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = location;
    portEXIT_CRITICAL(&lock);
}

static esp_err_t change_location(const weather_location_t *loc)
{
    portENTER_CRITICAL(&lock);
    location = *loc;
    auto_located = false;
    have_data = false;      // the old data is for another place
    portEXIT_CRITICAL(&lock);
    save_location();
    ESP_LOGI(TAG, "location set to %s", loc->is_auto ? "auto" : loc->name);
    return weather_svc_refresh();
}

esp_err_t weather_svc_set_auto(void)
{
    weather_location_t loc;
    weather_svc_get_location(&loc);     // keep the last name/coordinates until the lookup is done
    loc.is_auto = true;
    return change_location(&loc);
}

esp_err_t weather_svc_set_location(const weather_location_t *loc)
{
    weather_location_t manual = *loc;
    manual.is_auto = false;
    return change_location(&manual);
}

esp_err_t weather_svc_search(const char *query)
{
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    snprintf(search_query, sizeof(search_query), "%s", query);
    return start_task(search_task, "weather_search");
}

int weather_svc_search_results(weather_location_t *out, int max)
{
    portENTER_CRITICAL(&lock);
    int n = result_count < max ? result_count : max;
    memcpy(out, results, sizeof(results[0]) * n);
    portEXIT_CRITICAL(&lock);
    return n;
}
