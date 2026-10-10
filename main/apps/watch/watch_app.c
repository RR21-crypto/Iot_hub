/*
 * Watch: clock, stopwatch (with laps) and countdown timer, one tab each.
 *
 * Times are measured with esp_timer_get_time() (microseconds since boot), so the
 * stopwatch/timer state survives closing the app; only the widgets are rebuilt.
 */
#include <stdio.h>
#include "esp_timer.h"
#include "services/time_svc.h"
#include "ui/ui_common.h"
#include "watch_app.h"

#define WATCH_REFRESH_MS    50      // stopwatch/timer display refresh while open
#define WATCH_MAX_LAPS      20

static const char *const day_names[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};

/* ---------- state (kept while the app is closed) ---------- */

static struct {
    bool running;
    int64_t start_us;       // when the current run started
    int64_t elapsed_us;     // time accumulated before the current run
    int laps;
} sw;

static struct {
    bool running;
    int64_t end_us;        // when it reaches zero (while running)
    int64_t left_us;        // remaining time (while paused)
} tm_cd;

/* ---------- widgets (only valid while the app is open) ---------- */

static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_sw;
static lv_obj_t *btn_sw_start;
static lv_obj_t *btn_sw_lap;
static lv_obj_t *list_laps;
static lv_obj_t *lbl_cd;
static lv_obj_t *cd_picker;     // row with both rollers
static lv_obj_t *roller_min;
static lv_obj_t *roller_sec;
static lv_obj_t *btn_cd_start;
static lv_timer_t *refresh_timer;

static lv_obj_t *button_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 140, 56);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

static void button_set_text(lv_obj_t *btn, const char *text)
{
    lv_label_set_text(lv_obj_get_child(btn, 0), text);
}

static lv_obj_t *row_create(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void tab_setup(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tab, 12, 0);
}

/* ---------- clock tab ---------- */

static void clock_update(void)
{
    struct tm now;
    if (time_svc_now(&now)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.tm_hour, now.tm_min, now.tm_sec);
        ui_digits_set_text(lbl_clock, buf);
        lv_label_set_text_fmt(lbl_date, "%s, %02d-%02d-%04d", day_names[now.tm_wday % 7],
                              now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);
    } else {
        ui_digits_set_text(lbl_clock, "--:--:--");
        lv_label_set_text(lbl_date, "RTC not available");
    }
}

static void clock_tab_create(lv_obj_t *tab)
{
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lbl_clock = ui_digits_create(tab, &lv_font_montserrat_48);
    lbl_date = lv_label_create(tab);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    clock_update();
}

/* ---------- stopwatch tab ---------- */

static int64_t sw_elapsed_us(void)
{
    return sw.elapsed_us + (sw.running ? esp_timer_get_time() - sw.start_us : 0);
}

static void format_duration(char *buf, size_t len, int64_t us, bool hundredths)
{
    int64_t cs = us / 10000;        // centiseconds
    int min = (int)(cs / 6000), sec = (int)(cs / 100 % 60);
    if (hundredths) {
        snprintf(buf, len, "%02d:%02d.%02d", min, sec, (int)(cs % 100));
    } else {
        snprintf(buf, len, "%02d:%02d", min, sec);
    }
}

static void sw_update_buttons(void)
{
    button_set_text(btn_sw_start, sw.running ? LV_SYMBOL_PAUSE " Stop" : LV_SYMBOL_PLAY " Start");
    button_set_text(btn_sw_lap, sw.running ? LV_SYMBOL_LIST " Lap" : LV_SYMBOL_REFRESH " Reset");
}

static void sw_start_clicked(lv_event_t *e)
{
    if (sw.running) {
        sw.elapsed_us = sw_elapsed_us();
        sw.running = false;
    } else {
        sw.start_us = esp_timer_get_time();
        sw.running = true;
    }
    sw_update_buttons();
}

static void sw_lap_clicked(lv_event_t *e)
{
    if (sw.running) {
        if (sw.laps >= WATCH_MAX_LAPS) {
            return;
        }
        char text[48], t[16];
        format_duration(t, sizeof(t), sw_elapsed_us(), true);
        snprintf(text, sizeof(text), "Lap %d   %s", ++sw.laps, t);
        lv_obj_t *item = lv_list_add_text(list_laps, text);
        lv_obj_move_to_index(item, 0);      // newest on top
    } else {                                // Reset
        sw.elapsed_us = 0;
        sw.laps = 0;
        lv_obj_clean(list_laps);
    }
    sw_update_buttons();
}

static void stopwatch_tab_create(lv_obj_t *tab)
{
    lbl_sw = ui_digits_create(tab, &lv_font_montserrat_48);

    lv_obj_t *row = row_create(tab);
    btn_sw_start = button_create(row, "", sw_start_clicked);
    btn_sw_lap = button_create(row, "", sw_lap_clicked);
    sw_update_buttons();

    list_laps = lv_list_create(tab);
    lv_obj_set_width(list_laps, lv_pct(90));
    lv_obj_set_flex_grow(list_laps, 1);
    // Laps are not kept while the app is closed, only their count
    if (sw.laps) {
        char text[32];
        snprintf(text, sizeof(text), "%d lap(s) recorded", sw.laps);
        lv_list_add_text(list_laps, text);
    }
}

/* ---------- timer tab ---------- */

static int64_t cd_left_us(void)
{
    if (!tm_cd.running) {
        return tm_cd.left_us;
    }
    int64_t left = tm_cd.end_us - esp_timer_get_time();
    return left > 0 ? left : 0;
}

static void cd_update_controls(void)
{
    bool idle = !tm_cd.running && tm_cd.left_us == 0;
    button_set_text(btn_cd_start, tm_cd.running ? LV_SYMBOL_PAUSE " Pause" : LV_SYMBOL_PLAY " Start");
    // The rollers pick the duration; hide them while a countdown is active
    if (idle) {
        lv_obj_remove_flag(cd_picker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_cd, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cd_picker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_cd, LV_OBJ_FLAG_HIDDEN);
    }
}

static void cd_start_clicked(lv_event_t *e)
{
    if (tm_cd.running) {
        tm_cd.left_us = cd_left_us();
        tm_cd.running = false;
    } else {
        if (tm_cd.left_us == 0) {
            int secs = lv_roller_get_selected(roller_min) * 60 + lv_roller_get_selected(roller_sec);
            if (secs == 0) {
                return;
            }
            tm_cd.left_us = (int64_t)secs * 1000000;
        }
        tm_cd.end_us = esp_timer_get_time() + tm_cd.left_us;
        tm_cd.running = true;
    }
    cd_update_controls();
}

static void cd_reset_clicked(lv_event_t *e)
{
    tm_cd.running = false;
    tm_cd.left_us = 0;
    cd_update_controls();
}

static void cd_alert(void)
{
    tm_cd.running = false;
    tm_cd.left_us = 0;
    cd_update_controls();

    lv_obj_t *mbox = lv_msgbox_create(NULL);    // modal, on top of everything
    lv_msgbox_add_title(mbox, LV_SYMBOL_BELL " Timer");
    lv_msgbox_add_text(mbox, "Time is up!");
    lv_msgbox_add_close_button(mbox);
}

static lv_obj_t *roller_create(lv_obj_t *parent, const char *unit)
{
    static char options[60 * 3];
    if (options[0] == '\0') {
        for (int i = 0; i < 60; i++) {
            snprintf(options + i * 3, 4, "%02d%s", i, i < 59 ? "\n" : "");
        }
    }
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *roller = lv_roller_create(col);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_width(roller, 100);
    lv_obj_t *lbl = lv_label_create(col);
    lv_label_set_text(lbl, unit);
    lv_obj_set_style_text_color(lbl, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    return roller;
}

static void timer_tab_create(lv_obj_t *tab)
{
    cd_picker = row_create(tab);
    roller_min = roller_create(cd_picker, "min");
    roller_sec = roller_create(cd_picker, "sec");
    lv_roller_set_selected(roller_min, 5, LV_ANIM_OFF);

    lbl_cd = ui_digits_create(tab, &lv_font_montserrat_48);
    lv_obj_set_style_pad_ver(lbl_cd, 40, 0);

    lv_obj_t *row = row_create(tab);
    btn_cd_start = button_create(row, "", cd_start_clicked);
    button_create(row, LV_SYMBOL_STOP " Stop", cd_reset_clicked);
    cd_update_controls();
}

/* ---------- app callbacks ---------- */

static void refresh(lv_timer_t *t)
{
    char buf[16];
    format_duration(buf, sizeof(buf), sw_elapsed_us(), true);
    ui_digits_set_text(lbl_sw, buf);

    if (tm_cd.running && cd_left_us() == 0) {
        cd_alert();
    }
    // Round up so the display shows 00:01 until the very end, never 00:00 while running
    format_duration(buf, sizeof(buf), cd_left_us() + 999999, false);
    ui_digits_set_text(lbl_cd, buf);
}

static void watch_create(lv_obj_t *parent)
{
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tv = lv_tabview_create(parent);
    lv_obj_set_size(tv, lv_pct(100), lv_pct(100));
    lv_tabview_set_tab_bar_size(tv, 48);

    lv_obj_t *t_clock = lv_tabview_add_tab(tv, "Clock");
    lv_obj_t *t_sw = lv_tabview_add_tab(tv, "Stopwatch");
    lv_obj_t *t_cd = lv_tabview_add_tab(tv, "Timer");
    tab_setup(t_clock);
    tab_setup(t_sw);
    tab_setup(t_cd);

    clock_tab_create(t_clock);
    stopwatch_tab_create(t_sw);
    timer_tab_create(t_cd);

    refresh_timer = lv_timer_create(refresh, WATCH_REFRESH_MS, NULL);
    refresh(refresh_timer);
}

static void watch_destroy(void)
{
    lv_timer_delete(refresh_timer);
    refresh_timer = NULL;
}

static void watch_tick(void)
{
    clock_update();
}

const app_desc_t watch_app = {
    .name = "Clock",
    .icon = LV_SYMBOL_BELL,
    .color = 0xFF9800,
    .create = watch_create,
    .destroy = watch_destroy,
    .on_tick = watch_tick,
};
