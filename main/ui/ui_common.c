/*
 * Shared look and building blocks for all screens.
 */
#include "ui_common.h"

#define UI_TOAST_MS     4000

static lv_obj_t *toast;
static lv_timer_t *toast_timer;
static bool perf_shown;

static void back_clicked(lv_event_t *e)
{
    ui_back_cb_t cb = (ui_back_cb_t)lv_event_get_user_data(e);
    if (cb) {
        cb();
    }
}

void ui_common_init(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, lv_color_hex(UI_COLOR_ACCENT), lv_palette_main(LV_PALETTE_PINK),
                                              true, &lv_font_montserrat_20);
    lv_display_set_theme(disp, theme);
    ui_perf_monitor_set(false);     // LVGL shows it by default when enabled in menuconfig
}

lv_obj_t *ui_digits_create(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_pad_column(box, 0, 0);
    lv_obj_set_style_text_font(box, font, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    return box;
}

static int32_t digit_width(const lv_font_t *font)
{
    int32_t w = 0;
    for (char c = '0'; c <= '9'; c++) {
        int32_t cw = lv_font_get_glyph_width(font, c, 0);
        w = cw > w ? cw : w;
    }
    return w;
}

void ui_digits_set_text(lv_obj_t *digits, const char *text)
{
    const lv_font_t *font = lv_obj_get_style_text_font(digits, 0);
    uint32_t len = lv_strlen(text);
    int32_t dw = digit_width(font);

    // Different number of characters (e.g. "--:--" -> "1:05:00"): rebuild the cells
    if (lv_obj_get_child_count(digits) != len) {
        lv_obj_clean(digits);
        for (uint32_t i = 0; i < len; i++) {
            lv_obj_t *cell = lv_label_create(digits);
            lv_obj_set_style_text_align(cell, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(cell, "");
        }
    }
    char ch[2] = { 0 };
    for (uint32_t i = 0; i < len; i++) {
        lv_obj_t *cell = lv_obj_get_child(digits, i);
        ch[0] = text[i];
        if (lv_strcmp(lv_label_get_text(cell), ch) == 0) {
            continue;   // unchanged: nothing to redraw
        }
        bool digit = text[i] >= '0' && text[i] <= '9';
        int32_t w = digit ? dw : lv_font_get_glyph_width(font, text[i], 0);
        if (lv_obj_get_style_width(cell, 0) != w) {
            lv_obj_set_width(cell, w);
        }
        lv_label_set_text(cell, ch);
    }
}

void ui_perf_monitor_set(bool show)
{
#if LV_USE_PERF_MONITOR
    if (show) {
        lv_sysmon_show_performance(NULL);
    } else {
        lv_sysmon_hide_performance(NULL);
    }
    perf_shown = show;
#endif
}

bool ui_perf_monitor_get(void)
{
    return perf_shown;
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_app_screen_create(const char *title, ui_back_cb_t on_back, lv_obj_t **content)
{
    lv_obj_t *scr = ui_screen_create();

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, UI_SCREEN_W, UI_TITLEBAR_H);
    lv_obj_set_pos(bar, 0, UI_STATUSBAR_H);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(bar);
    lv_obj_set_size(back, 64, UI_TITLEBAR_H);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, (void *)on_back);
    lv_obj_t *arrow = lv_label_create(back);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_center(arrow);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);

    lv_obj_t *area = lv_obj_create(scr);
    lv_obj_set_size(area, UI_SCREEN_W, UI_CONTENT_H);
    lv_obj_set_pos(area, 0, UI_STATUSBAR_H + UI_TITLEBAR_H);
    lv_obj_set_style_radius(area, 0, 0);
    lv_obj_set_style_border_width(area, 0, 0);
    lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(area, 12, 0);

    *content = area;
    return scr;
}

lv_obj_t *ui_error_screen_create(const char *msg)
{
    lv_obj_t *scr = ui_screen_create();
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, msg);
    lv_obj_set_width(lbl, lv_pct(90));
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(lbl, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_center(lbl);
    return scr;
}

lv_obj_t *ui_card_create(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t *lbl = lv_label_create(card);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_ACCENT), 0);
    }
    return card;
}

static void toast_expired(lv_timer_t *t)
{
    toast_timer = NULL;     // a one-shot timer deletes itself after this call
    if (toast) {
        lv_obj_delete(toast);
        toast = NULL;
    }
}

void ui_toast_show(const char *text)
{
    // Replace the previous toast and its timer
    if (toast_timer) {
        lv_timer_delete(toast_timer);
    }
    toast_expired(NULL);
    toast = lv_label_create(lv_layer_top());
    lv_label_set_text(toast, text);
    lv_label_set_long_mode(toast, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(toast, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(toast, UI_SCREEN_W - 40, 0);
    lv_obj_set_style_bg_color(toast, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_border_color(toast, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(toast, 2, 0);
    lv_obj_set_style_radius(toast, 16, 0);
    lv_obj_set_style_pad_all(toast, 14, 0);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -24);

    toast_timer = lv_timer_create(toast_expired, UI_TOAST_MS, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}
