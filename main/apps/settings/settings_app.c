/*
 * Settings: WiFi (scan, connect, forget), firmware update over WiFi (OTA) and board info.
 */
#include <stdio.h>
#include <stdint.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "main.h"
#include "i2c/i2c_scan.h"
#include "services/ota_svc.h"
#include "services/storage.h"
#include "services/wifi_svc.h"
#include "ui/ui_common.h"
#include "settings_app.h"

static lv_obj_t *root;
static lv_obj_t *lbl_wifi;
static lv_obj_t *btn_scan;
static lv_obj_t *list_aps;
static lv_obj_t *lbl_update;
static lv_obj_t *lbl_memory;
static lv_obj_t *lbl_uptime;
static lv_obj_t *dialog;            // password dialog, NULL when closed
static lv_obj_t *sw_fps;
static lv_obj_t *ta_password;

static wifi_svc_ap_t aps[WIFI_SVC_MAX_APS];
static int selected_ap;

static lv_obj_t *info_label_create(lv_obj_t *card)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
    return lbl;
}

static lv_obj_t *button_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 44);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

static lv_obj_t *row_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* ---------- WiFi ---------- */

static void update_wifi(void)
{
    wifi_svc_status_t st;
    wifi_svc_get_status(&st);
    switch (st.state) {
    case WIFI_SVC_NOT_SET:
        lv_label_set_text(lbl_wifi, "Not connected. Tap Scan.");
        break;
    case WIFI_SVC_CONNECTING:
        lv_label_set_text_fmt(lbl_wifi, "Connecting to %s...", st.ssid);
        break;
    case WIFI_SVC_CONNECTED:
        lv_label_set_text_fmt(lbl_wifi, "Connected: %s\nIP: %s   Signal: %d dBm\nAccess point: %s",
                              st.ssid, st.ip, st.rssi, st.bssid);
        break;
    case WIFI_SVC_OFF:
        lv_label_set_text(lbl_wifi, "WiFi is off. Turn it on in the quick panel (tap the status bar).");
        break;
    case WIFI_SVC_FAILED:
        lv_label_set_text_fmt(lbl_wifi, "Failed to join %s: %s\nRetrying...", st.ssid, st.fail_reason);
        break;
    }
}

static void update_ota(void)
{
    wifi_svc_status_t st;
    wifi_svc_get_status(&st);
    const char *version = esp_app_get_description()->version;
    const char *code = ota_svc_code();
    if (st.state == WIFI_SVC_CONNECTED && code) {
        lv_label_set_text_fmt(lbl_update, "Version: %s\nOpen in a browser:\nhttp://%s/\nCode: %s",
                              version, st.ip, code);
    } else {
        lv_label_set_text_fmt(lbl_update, "Version: %s\nConnect WiFi to update without a cable.", version);
    }
}

static void dialog_close(void)
{
    if (dialog) {
        lv_obj_delete(dialog);
        dialog = NULL;
        ta_password = NULL;
    }
}

static void dialog_close_async(void *unused)
{
    dialog_close();
}

static void connect_clicked(lv_event_t *e)
{
    wifi_svc_connect(aps[selected_ap].ssid, lv_textarea_get_text(ta_password));
    lv_obj_add_flag(list_aps, LV_OBJ_FLAG_HIDDEN);
    update_wifi();
    lv_async_call(dialog_close_async, NULL);    // don't delete the dialog inside its own event
}

static void cancel_clicked(lv_event_t *e)
{
    lv_async_call(dialog_close_async, NULL);
}

static void password_dialog_open(void)
{
    // Full-screen panel over the app (under the status bar)
    dialog = lv_obj_create(lv_obj_get_screen(root));
    lv_obj_set_size(dialog, UI_SCREEN_W, UI_SCREEN_H - UI_STATUSBAR_H);
    lv_obj_set_pos(dialog, 0, UI_STATUSBAR_H);
    lv_obj_set_style_radius(dialog, 0, 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(dialog, 10, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text_fmt(title, LV_SYMBOL_WIFI "  %s", aps[selected_ap].ssid);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT), 0);

    ta_password = lv_textarea_create(dialog);
    lv_obj_set_width(ta_password, lv_pct(100));
    lv_textarea_set_one_line(ta_password, true);
    lv_textarea_set_password_mode(ta_password, false);     // easier to type correctly on a small keyboard
    lv_textarea_set_placeholder_text(ta_password, "Password");

    lv_obj_t *row = row_create(dialog);
    button_create(row, LV_SYMBOL_CLOSE " Cancel", cancel_clicked);
    button_create(row, LV_SYMBOL_OK " Connect", connect_clicked);

    lv_obj_t *kb = lv_keyboard_create(dialog);
    lv_obj_set_width(kb, lv_pct(100));
    lv_obj_set_flex_grow(kb, 1);
    lv_keyboard_set_textarea(kb, ta_password);
    lv_obj_add_event_cb(kb, connect_clicked, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, cancel_clicked, LV_EVENT_CANCEL, NULL);
}

static void ap_clicked(lv_event_t *e)
{
    selected_ap = (int)(intptr_t)lv_event_get_user_data(e);
    if (aps[selected_ap].secure) {
        password_dialog_open();
    } else {
        wifi_svc_connect(aps[selected_ap].ssid, "");
        lv_obj_add_flag(list_aps, LV_OBJ_FLAG_HIDDEN);
        update_wifi();
    }
}

static void show_scan_results(void)
{
    lv_label_set_text(lv_obj_get_child(btn_scan, 0), LV_SYMBOL_REFRESH " Scan");
    lv_obj_clean(list_aps);
    int n = wifi_svc_scan_results(aps, WIFI_SVC_MAX_APS);
    if (n == 0) {
        lv_list_add_text(list_aps, "No networks found");
    }
    for (int i = 0; i < n; i++) {
        char text[WIFI_SVC_SSID_LEN + 32];
        snprintf(text, sizeof(text), "%.32s  (%d dBm)%s", aps[i].ssid, aps[i].rssi, aps[i].secure ? "" : " open");
        lv_obj_t *item = lv_list_add_button(list_aps, LV_SYMBOL_WIFI, text);
        lv_obj_add_event_cb(item, ap_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_remove_flag(list_aps, LV_OBJ_FLAG_HIDDEN);
}

static void scan_clicked(lv_event_t *e)
{
    if (wifi_svc_scan_start() == ESP_OK) {
        lv_label_set_text(lv_obj_get_child(btn_scan, 0), "Scanning...");
    } else {
        lv_label_set_text(lbl_wifi, "Scan failed, try again in a moment.");
    }
}

static void forget_clicked(lv_event_t *e)
{
    wifi_svc_forget();
    update_wifi();
    update_ota();
}

/* ---------- display ---------- */

static void fps_changed(lv_event_t *e)
{
    ui_perf_monitor_set(lv_obj_has_state(sw_fps, LV_STATE_CHECKED));
}

static void display_card_create(lv_obj_t *parent)
{
    lv_obj_t *card = ui_card_create(parent, LV_SYMBOL_EYE_OPEN "  Display");
    lv_obj_t *row = row_create(card);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "Show FPS / CPU");
    sw_fps = lv_switch_create(row);
    if (ui_perf_monitor_get()) {
        lv_obj_add_state(sw_fps, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw_fps, fps_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *hint = info_label_create(card);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    lv_label_set_text(hint, "Bottom left: frames per second and CPU load of the UI. "
                            "It only counts frames where something changed on screen.");
}

/* ---------- system info ---------- */

static void update_memory(void)
{
    lv_label_set_text_fmt(lbl_memory, "Internal RAM: %u KB free\nPSRAM: %u KB free",
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void update_uptime(void)
{
    uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000);
    lv_label_set_text_fmt(lbl_uptime, "Uptime: %02u:%02u:%02u",
                          (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

static void storage_label_create(lv_obj_t *card, storage_area_t area, const char *name)
{
    size_t total, used;
    if (storage_get_usage(area, &total, &used) == ESP_OK) {
        lv_label_set_text_fmt(info_label_create(card), "%s: %u / %u KB", name,
                              (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
}

/* ---------- app callbacks ---------- */

static void settings_create(lv_obj_t *parent)
{
    root = parent;
    dialog = NULL;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 12, 0);

    lv_obj_t *wifi = ui_card_create(parent, LV_SYMBOL_WIFI "  WiFi");
    lbl_wifi = info_label_create(wifi);
    lv_obj_t *row = row_create(wifi);
    btn_scan = button_create(row, LV_SYMBOL_REFRESH " Scan", scan_clicked);
    button_create(row, LV_SYMBOL_TRASH " Forget", forget_clicked);
    list_aps = lv_list_create(wifi);
    lv_obj_set_size(list_aps, lv_pct(100), 220);
    lv_obj_add_flag(list_aps, LV_OBJ_FLAG_HIDDEN);

    display_card_create(parent);

    lv_obj_t *update = ui_card_create(parent, LV_SYMBOL_DOWNLOAD "  Update firmware");
    lbl_update = info_label_create(update);

    // Live I2C scan: shows what answers on the bus right now
    lv_obj_t *i2c = ui_card_create(parent, LV_SYMBOL_SHUFFLE "  I2C devices");
    uint8_t devices[I2C_MAX_DEVICES];
    size_t n = i2c_scan(bsp_i2c_get_handle(), devices, I2C_MAX_DEVICES);
    char text[I2C_TEXT_LEN];
    i2c_scan_format(devices, n > I2C_MAX_DEVICES ? I2C_MAX_DEVICES : n, text, sizeof(text));
    lv_label_set_text(info_label_create(i2c), text);

    lv_obj_t *sys = ui_card_create(parent, LV_SYMBOL_SETTINGS "  System");
    lv_label_set_text_fmt(info_label_create(sys), "ESP-IDF %s", esp_get_idf_version());
    lbl_memory = info_label_create(sys);
    lbl_uptime = info_label_create(sys);
    storage_label_create(sys, STORAGE_GALLERY, "Gallery");
    storage_label_create(sys, STORAGE_USER, "User data");

    update_wifi();
    update_ota();
    update_memory();
    update_uptime();
}

static void settings_destroy(void)
{
    lv_async_call_cancel(dialog_close_async, NULL);
    dialog = NULL;      // deleted together with the screen
}

static void settings_tick(void)
{
    update_wifi();
    update_ota();
    update_memory();
    update_uptime();
}

static void settings_event(app_event_t event, int arg)
{
    switch (event) {
    case APP_EVT_WIFI_SCAN_DONE:
        show_scan_results();
        break;
    case APP_EVT_WIFI_UP:
    case APP_EVT_WIFI_DOWN:
        update_wifi();
        update_ota();
        break;
    default:
        break;
    }
}

const app_desc_t settings_app = {
    .name = "Settings",
    .icon = LV_SYMBOL_SETTINGS,
    .color = 0x607D8B,
    .create = settings_create,
    .destroy = settings_destroy,
    .on_tick = settings_tick,
    .on_event = settings_event,
};
