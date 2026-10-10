/*
 * Weather: current weather and a 3-day forecast from Open-Meteo, with weather icons.
 * The location is automatic (from the internet connection) or a city picked with "Change".
 * Data is fetched when the app opens (if older than WEATHER_MAX_AGE_MIN) or with the refresh button.
 */
#include <stdint.h>
#include "esp_timer.h"
#include "assets/weather_icons.h"
#include "services/weather_svc.h"
#include "services/wifi_svc.h"
#include "ui/ui_common.h"
#include "weather_app.h"

#define WEATHER_MAX_AGE_MIN     15
#define SMALL_ICON_SIZE         36

static const char *const day_names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static lv_obj_t *root;
static lv_obj_t *lbl_location;
static lv_obj_t *img_now;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_desc;
static lv_obj_t *lbl_details;
static lv_obj_t *img_days[WEATHER_FORECAST_DAYS];
static lv_obj_t *lbl_days[WEATHER_FORECAST_DAYS];
static lv_obj_t *lbl_temps[WEATHER_FORECAST_DAYS];
static lv_obj_t *lbl_status;
static bool loading;

// Location panel (NULL when closed)
static lv_obj_t *panel;
static lv_obj_t *ta_search;
static lv_obj_t *kb;
static lv_obj_t *list_results;
static weather_location_t results[WEATHER_MAX_RESULTS];

static const lv_image_dsc_t *icon_src(int code)
{
    switch (weather_svc_icon(code)) {
    case WEATHER_ICON_CLEAR:   return &weather_icon_clear;
    case WEATHER_ICON_PARTLY:  return &weather_icon_partly;
    case WEATHER_ICON_CLOUDY:  return &weather_icon_cloudy;
    case WEATHER_ICON_FOG:     return &weather_icon_fog;
    case WEATHER_ICON_RAIN:    return &weather_icon_rain;
    case WEATHER_ICON_THUNDER: return &weather_icon_thunder;
    default:                   return &weather_icon_snow;
    }
}

static lv_obj_t *icon_create(lv_obj_t *parent, int size)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_size(img, size, size);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);  // scale the 96 px icon to the box
    return img;
}

static lv_obj_t *row_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static lv_obj_t *button_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 40);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

/* ---------- main view ---------- */

static int age_min(const weather_t *w)
{
    return (int)((esp_timer_get_time() - w->fetched_us) / 60000000);
}

static void show_location(void)
{
    weather_location_t loc;
    weather_svc_get_location(&loc);
    lv_label_set_text_fmt(lbl_location, "%s%s", loc.name, loc.is_auto ? "  (auto)" : "");
}

static void show_status(void)
{
    weather_t w;
    wifi_svc_status_t wifi;
    wifi_svc_get_status(&wifi);
    if (loading) {
        lv_label_set_text(lbl_status, "Loading...");
    } else if (weather_svc_get(&w)) {
        int age = age_min(&w);
        lv_label_set_text_fmt(lbl_status, age == 0 ? "Updated just now" : "Updated %d min ago", age);
    } else if (wifi.state != WIFI_SVC_CONNECTED) {
        lv_label_set_text(lbl_status, "Connect WiFi in Settings to get the weather.");
    } else {
        lv_label_set_text(lbl_status, "No data yet, tap refresh.");
    }
}

static void render(void)
{
    show_location();
    weather_t w;
    if (!weather_svc_get(&w)) {
        lv_image_set_src(img_now, &weather_icon_cloudy);
        lv_obj_set_style_image_opa(img_now, LV_OPA_30, 0);
        lv_label_set_text(lbl_temp, "--");
        lv_label_set_text(lbl_desc, "");
        lv_label_set_text(lbl_details, "");
        for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
            lv_image_set_src(img_days[i], NULL);
            lv_label_set_text(lbl_days[i], "-");
            lv_label_set_text(lbl_temps[i], "");
        }
        show_status();
        return;
    }
    lv_image_set_src(img_now, icon_src(w.code));
    lv_obj_set_style_image_opa(img_now, LV_OPA_COVER, 0);
    // LVGL's built-in fonts have no degree sign, so temperatures are shown as "31.2 C"
    lv_label_set_text_fmt(lbl_temp, "%.1f C", w.temp);
    lv_label_set_text(lbl_desc, weather_svc_describe(w.code));
    lv_label_set_text_fmt(lbl_details, "Today %.0f / %.0f C    Humidity %d%%    Wind %.0f km/h",
                          w.today_max, w.today_min, w.humidity, w.wind);
    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        const weather_day_t *d = &w.days[i];
        lv_image_set_src(img_days[i], icon_src(d->code));
        lv_label_set_text_fmt(lbl_days[i], "%s   %s", day_names[d->wday % 7], weather_svc_describe(d->code));
        lv_label_set_text_fmt(lbl_temps[i], "%.0f / %.0f C", d->temp_max, d->temp_min);
    }
    show_status();
}

static void refresh(void)
{
    wifi_svc_status_t wifi;
    wifi_svc_get_status(&wifi);
    if (wifi.state == WIFI_SVC_CONNECTED && weather_svc_refresh() == ESP_OK) {
        loading = true;
    }
    show_status();
}

static void refresh_clicked(lv_event_t *e)
{
    refresh();
}

/* ---------- location panel ---------- */

static void panel_close(void *unused)
{
    if (panel) {
        lv_obj_delete(panel);
        panel = NULL;
    }
}

static void location_chosen(void)
{
    loading = true;
    render();
    lv_async_call(panel_close, NULL);   // don't delete the panel inside its own event
}

static void auto_clicked(lv_event_t *e)
{
    weather_svc_set_auto();
    location_chosen();
}

static void result_clicked(lv_event_t *e)
{
    weather_svc_set_location(&results[(intptr_t)lv_event_get_user_data(e)]);
    location_chosen();
}

static void search_clicked(lv_event_t *e)
{
    const char *query = lv_textarea_get_text(ta_search);
    if (query[0] == '\0') {
        return;
    }
    lv_obj_clean(list_results);
    lv_obj_remove_flag(list_results, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);    // make room for the results
    lv_list_add_text(list_results, weather_svc_search(query) == ESP_OK ? "Searching..." : "Busy, try again.");
}

static void search_focused(lv_event_t *e)
{
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void cancel_clicked(lv_event_t *e)
{
    lv_async_call(panel_close, NULL);
}

static void show_results(int count)
{
    if (!panel) {
        return;
    }
    lv_obj_clean(list_results);
    if (count < 0) {
        lv_list_add_text(list_results, "Search failed. Is WiFi connected?");
        return;
    }
    count = weather_svc_search_results(results, WEATHER_MAX_RESULTS);
    if (count == 0) {
        lv_list_add_text(list_results, "No place found, check the spelling.");
    }
    for (int i = 0; i < count; i++) {
        lv_obj_t *item = lv_list_add_button(list_results, LV_SYMBOL_GPS, results[i].name);
        lv_obj_add_event_cb(item, result_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void location_clicked(lv_event_t *e)
{
    panel = lv_obj_create(lv_obj_get_screen(root));
    lv_obj_set_size(panel, UI_SCREEN_W, UI_SCREEN_H - UI_STATUSBAR_H);
    lv_obj_set_pos(panel, 0, UI_STATUSBAR_H);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *top = row_create(panel);
    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "Weather location");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_flex_grow(title, 1);
    button_create(top, LV_SYMBOL_CLOSE, cancel_clicked);

    lv_obj_t *btn_auto = button_create(panel, LV_SYMBOL_GPS "  Use my location (auto)", auto_clicked);
    lv_obj_set_width(btn_auto, lv_pct(100));

    lv_obj_t *row = row_create(panel);
    ta_search = lv_textarea_create(row);
    lv_obj_set_flex_grow(ta_search, 1);
    lv_textarea_set_one_line(ta_search, true);
    lv_textarea_set_placeholder_text(ta_search, "Search a city, e.g. Batam");
    lv_obj_add_event_cb(ta_search, search_focused, LV_EVENT_FOCUSED, NULL);
    button_create(row, LV_SYMBOL_RIGHT, search_clicked);

    list_results = lv_list_create(panel);
    lv_obj_set_width(list_results, lv_pct(100));
    lv_obj_set_flex_grow(list_results, 1);
    lv_obj_add_flag(list_results, LV_OBJ_FLAG_HIDDEN);

    kb = lv_keyboard_create(panel);
    lv_obj_set_size(kb, lv_pct(100), 190);
    lv_keyboard_set_textarea(kb, ta_search);
    lv_obj_add_event_cb(kb, search_clicked, LV_EVENT_READY, NULL);
}

/* ---------- app callbacks ---------- */

static void weather_create(lv_obj_t *parent)
{
    root = parent;
    panel = NULL;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 8, 0);

    lv_obj_t *top = row_create(parent);
    lbl_location = lv_label_create(top);
    lv_obj_set_flex_grow(lbl_location, 1);
    lv_label_set_long_mode(lbl_location, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_color(lbl_location, lv_color_hex(UI_COLOR_ACCENT), 0);
    button_create(top, LV_SYMBOL_REFRESH, refresh_clicked);
    button_create(top, "Change", location_clicked);

    lv_obj_t *now = row_create(parent);
    lv_obj_set_style_pad_column(now, 16, 0);
    img_now = icon_create(now, WEATHER_ICON_SIZE);
    lv_obj_t *col = lv_obj_create(now);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lbl_temp = lv_label_create(col);
    lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_48, 0);
    lbl_desc = lv_label_create(col);
    lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_24, 0);

    lbl_details = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl_details, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_details, lv_color_hex(UI_COLOR_TEXT_DIM), 0);

    lv_obj_t *card = ui_card_create(parent, "Next days");
    for (int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        lv_obj_t *day = row_create(card);
        img_days[i] = icon_create(day, SMALL_ICON_SIZE);
        lbl_days[i] = lv_label_create(day);
        lv_obj_set_flex_grow(lbl_days[i], 1);
        lbl_temps[i] = lv_label_create(day);
    }

    lbl_status = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(UI_COLOR_TEXT_DIM), 0);

    loading = false;
    render();
    weather_t w;
    if (!weather_svc_get(&w) || age_min(&w) >= WEATHER_MAX_AGE_MIN) {
        refresh();
    }
}

static void weather_destroy(void)
{
    lv_async_call_cancel(panel_close, NULL);
    panel = NULL;       // deleted together with the screen
}

static void weather_tick(void)
{
    show_status();
}

static void weather_event(app_event_t event, int arg)
{
    weather_t w;
    switch (event) {
    case APP_EVT_WEATHER_UPDATED:
        loading = false;
        render();
        if (arg < 0) {
            lv_label_set_text(lbl_status, "Update failed, tap refresh to try again.");
        }
        break;
    case APP_EVT_WEATHER_SEARCH_DONE:
        show_results(arg);
        break;
    case APP_EVT_WIFI_UP:
        if (!weather_svc_get(&w)) {
            refresh();
        }
        break;
    default:
        break;
    }
}

const app_desc_t weather_app = {
    .name = "Weather",
    .icon = LV_SYMBOL_IMAGE,
    .icon_img = &weather_icon_partly,
    .color = 0x0288D1,
    .create = weather_create,
    .destroy = weather_destroy,
    .on_tick = weather_tick,
    .on_event = weather_event,
};
