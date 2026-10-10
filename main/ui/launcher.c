/*
 * Home screen: a grid of app icons built from the app registry.
 */
#include <stdint.h>
#include "core/app_registry.h"
#include "ui_common.h"
#include "launcher.h"

#define LAUNCHER_COLS       3
#define LAUNCHER_ICON_SIZE  96
#define LAUNCHER_IMG_SIZE   72      // picture icons are scaled to this
#define LAUNCHER_CELL_W     140
#define LAUNCHER_CELL_H     140

static lv_obj_t *screen;
static launcher_open_cb_t open_cb;

static void icon_clicked(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (open_cb) {
        open_cb(index);
    }
}

static void icon_create(lv_obj_t *grid, int index, const app_desc_t *app)
{
    lv_obj_t *cell = lv_obj_create(grid);
    lv_obj_set_size(cell, LAUNCHER_CELL_W, LAUNCHER_CELL_H);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_button_create(cell);
    lv_obj_set_size(btn, LAUNCHER_ICON_SIZE, LAUNCHER_ICON_SIZE);
    lv_obj_set_style_radius(btn, 24, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(app->color), 0);
    lv_obj_add_event_cb(btn, icon_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    if (app->icon_img) {
        lv_obj_t *img = lv_image_create(btn);
        lv_image_set_src(img, app->icon_img);
        lv_obj_set_size(img, LAUNCHER_IMG_SIZE, LAUNCHER_IMG_SIZE);
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);   // scale the picture to the box
        lv_obj_center(img);
    } else {
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, app->icon);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
        lv_obj_center(icon);
    }

    lv_obj_t *name = lv_label_create(cell);
    lv_label_set_text(name, app->name);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
}

lv_obj_t *launcher_create(launcher_open_cb_t on_open)
{
    open_cb = on_open;

    screen = ui_screen_create();
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x3A1E5C), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);

    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_set_size(grid, UI_SCREEN_W, UI_SCREEN_H - UI_STATUSBAR_H);
    lv_obj_set_pos(grid, 0, UI_STATUSBAR_H);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_top(grid, 24, 0);
    lv_obj_set_style_pad_hor(grid, (UI_SCREEN_W - LAUNCHER_COLS * LAUNCHER_CELL_W) / 2, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);

    for (size_t i = 0; i < app_registry_count(); i++) {
        icon_create(grid, (int)i, app_registry_get(i));
    }
    return screen;
}

lv_obj_t *launcher_screen(void)
{
    return screen;
}
