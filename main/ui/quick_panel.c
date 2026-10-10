/*
 * Quick panel: slides down from the top with WiFi and Bluetooth tiles.
 */
#include "main.h"
#include "services/bt_svc.h"
#include "services/wifi_svc.h"
#include "ui_common.h"
#include "quick_panel.h"

#define PANEL_H         300
#define PANEL_ANIM_MS   200
#define TILE_W          212
#define TILE_H          110

#define COLOR_TILE_OFF  0x3A3646

static lv_obj_t *backdrop;      // dims the screen and closes the panel when tapped
static lv_obj_t *panel;
static lv_obj_t *tile_wifi;
static lv_obj_t *tile_bt;
static lv_obj_t *lbl_info;
static bool is_open;

static void anim_y(void *obj, int32_t y)
{
    lv_obj_set_y(obj, y);
}

static void slide(int32_t from, int32_t to)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_exec_cb(&a, anim_y);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, PANEL_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void tile_set(lv_obj_t *tile, bool on, const char *state)
{
    lv_obj_set_style_bg_color(tile, on ? lv_color_hex(UI_COLOR_ACCENT) : lv_color_hex(COLOR_TILE_OFF), 0);
    lv_color_t text = on ? lv_color_hex(UI_COLOR_BG) : lv_color_white();
    lv_obj_set_style_text_color(tile, text, 0);
    lv_label_set_text(lv_obj_get_child(tile, 2), state);
}

static void wifi_clicked(lv_event_t *e)
{
    wifi_svc_set_enabled(!wifi_svc_is_enabled());
    quick_panel_refresh();
}

static void bt_clicked(lv_event_t *e)
{
    bt_svc_status_t st;
    bt_svc_get_status(&st);
    bt_svc_set_enabled(!st.enabled);
    quick_panel_refresh();
}

static lv_obj_t *tile_create(lv_obj_t *parent, const char *icon, const char *name, lv_event_cb_t cb)
{
    lv_obj_t *tile = lv_button_create(parent);
    lv_obj_set_size(tile, TILE_W, TILE_H);
    lv_obj_set_style_radius(tile, 20, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_icon = lv_label_create(tile);         // child 0
    lv_label_set_text(lbl_icon, icon);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_icon, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_name = lv_label_create(tile);         // child 1
    lv_label_set_text(lbl_name, name);
    lv_obj_align(lbl_name, LV_ALIGN_BOTTOM_LEFT, 0, -22);

    lv_obj_t *lbl_state = lv_label_create(tile);        // child 2
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_state, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return tile;
}

static void backdrop_clicked(lv_event_t *e)
{
    quick_panel_close();
}

static void panel_gesture(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) {
        quick_panel_close();
    }
}

static void close_clicked(lv_event_t *e)
{
    quick_panel_close();
}

void quick_panel_create(void)
{
    backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_size(backdrop, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, 0);
    lv_obj_add_event_cb(backdrop, backdrop_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);

    panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(panel, UI_SCREEN_W, PANEL_H);
    lv_obj_set_pos(panel, 0, -PANEL_H);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_top(panel, 14, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, panel_gesture, LV_EVENT_GESTURE, NULL);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Quick settings");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *close = lv_button_create(panel);
    lv_obj_set_size(close, 56, 36);
    lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 8, -4);
    lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *arrow = lv_label_create(close);
    lv_label_set_text(arrow, LV_SYMBOL_UP);
    lv_obj_center(arrow);

    tile_wifi = tile_create(panel, LV_SYMBOL_WIFI, "WiFi", wifi_clicked);
    lv_obj_align(tile_wifi, LV_ALIGN_TOP_LEFT, 0, 44);
    tile_bt = tile_create(panel, LV_SYMBOL_BLUETOOTH, "Bluetooth", bt_clicked);
    lv_obj_align(tile_bt, LV_ALIGN_TOP_RIGHT, 0, 44);

    lbl_info = lv_label_create(panel);
    lv_obj_set_width(lbl_info, lv_pct(100));
    lv_label_set_long_mode(lbl_info, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_info, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 0, 44 + TILE_H + 16);

    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

void quick_panel_open(void)
{
    if (is_open) {
        return;
    }
    is_open = true;
    quick_panel_refresh();
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(backdrop);
    lv_obj_move_foreground(panel);     // above the status bar
    slide(-PANEL_H, 0);
}

static void hide_ready(lv_anim_t *a)
{
    lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
}

void quick_panel_close(void)
{
    if (!is_open) {
        return;
    }
    is_open = false;
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_exec_cb(&a, anim_y);
    lv_anim_set_values(&a, lv_obj_get_y(panel), -PANEL_H);
    lv_anim_set_duration(&a, PANEL_ANIM_MS);
    lv_anim_set_completed_cb(&a, hide_ready);
    lv_anim_start(&a);
}

void quick_panel_refresh(void)
{
    if (!panel || !is_open) {
        return;
    }
    wifi_svc_status_t wifi;
    wifi_svc_get_status(&wifi);
    bt_svc_status_t bt;
    bt_svc_get_status(&bt);

    const char *wifi_state = wifi.state == WIFI_SVC_OFF ? "Off"
                             : wifi.state == WIFI_SVC_CONNECTED ? wifi.ssid
                             : wifi.state == WIFI_SVC_NOT_SET ? "Not set up"
                             : "Connecting...";
    tile_set(tile_wifi, wifi.state != WIFI_SVC_OFF, wifi_state);
    tile_set(tile_bt, bt.enabled, bt.connected ? "Connected" : bt.enabled ? "Visible" : "Off");

    char info[192];
    int n = 0;
    if (wifi.state == WIFI_SVC_CONNECTED) {
        n += lv_snprintf(info + n, sizeof(info) - n, "WiFi: %s  (IP %s)\n", wifi.ssid, wifi.ip);
    }
    if (bt.connected) {
        lv_snprintf(info + n, sizeof(info) - n, "Bluetooth: connected to %s", bt.peer);
    } else if (bt.enabled) {
        lv_snprintf(info + n, sizeof(info) - n,
                    "Bluetooth: visible as \"" APP_DISPLAY_NAME "\". Connect with the nRF Connect app "
                    "and write text to \"Message\" to show it here.");
    } else {
        lv_snprintf(info + n, sizeof(info) - n, "Bluetooth: off");
    }
    lv_label_set_text(lbl_info, info);
}
