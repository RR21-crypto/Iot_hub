/*
 * Status bar on the top layer: clock, WiFi and Bluetooth icons.
 */
#include "main.h"
#include "quick_panel.h"
#include "ui_common.h"
#include "statusbar.h"

static lv_obj_t *lbl_time;
static lv_obj_t *lbl_wifi;
static lv_obj_t *lbl_name;
static lv_obj_t *lbl_bt;

static void bar_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED
            || lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) {
        quick_panel_open();
    }
}

void statusbar_create(void)
{
    lv_obj_t *bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bar, UI_SCREEN_W, UI_STATUSBAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bar, bar_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(bar, bar_event, LV_EVENT_GESTURE, NULL);

    lbl_name = lv_label_create(bar);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_time = lv_label_create(bar);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, 0);

    lbl_wifi = lv_label_create(bar);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, 0, 0);

    lbl_bt = lv_label_create(bar);
    lv_label_set_text(lbl_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_bt, LV_ALIGN_RIGHT_MID, -32, 0);

    statusbar_set_time(NULL);
    statusbar_set_wifi(false);
    statusbar_set_bt(false, false);
    statusbar_set_message(NULL);
}

void statusbar_set_time(const struct tm *now)
{
    if (now) {
        lv_label_set_text_fmt(lbl_time, "%02d:%02d", now->tm_hour, now->tm_min);
    } else {
        lv_label_set_text(lbl_time, "--:--");
    }
}

void statusbar_set_wifi(bool connected)
{
    lv_obj_set_style_text_color(lbl_wifi, connected ? lv_color_white() : lv_color_hex(0x4A4656), 0);
}

void statusbar_set_message(const char *text)
{
    lv_label_set_text(lbl_name, text ? text : APP_DISPLAY_NAME);
    lv_obj_set_style_text_color(lbl_name, text ? lv_color_hex(UI_COLOR_ACCENT) : lv_color_hex(UI_COLOR_TEXT_DIM), 0);
}

void statusbar_set_bt(bool enabled, bool connected)
{
    lv_color_t color = connected ? lv_palette_main(LV_PALETTE_BLUE)
                       : enabled ? lv_color_white() : lv_color_hex(0x4A4656);
    lv_obj_set_style_text_color(lbl_bt, color, 0);
}
