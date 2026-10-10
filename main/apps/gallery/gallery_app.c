/*
 * Gallery: browse the photos (.jpg) and animations (.gif) in /gallery.
 *
 * JPGs are decoded by LVGL straight from the file ("S:/gallery/x.jpg").
 * GIFs are first copied into PSRAM: decoding every frame from flash would be too slow.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "services/storage.h"
#include "ui/ui_common.h"
#include "gallery_app.h"

static const char *TAG = "gallery";

#define GALLERY_MAX_FILES   32
#define GALLERY_NAME_LEN    40
#define GALLERY_PATH_LEN    (sizeof(STORAGE_LVGL_PREFIX STORAGE_GALLERY_PATH "/") + GALLERY_NAME_LEN)

static struct {
    char names[GALLERY_MAX_FILES][GALLERY_NAME_LEN];
    int count;
    int index;
    lv_obj_t *view;         // area that holds the picture
    lv_obj_t *picture;      // current lv_image or lv_gif
    lv_obj_t *caption;
    uint8_t *gif_data;      // PSRAM copy of the current GIF
    lv_image_dsc_t gif_dsc;
} g;

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(a, b);
}

static void scan_files(void)
{
    g.count = 0;
    DIR *dir = opendir(STORAGE_GALLERY_PATH);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", STORAGE_GALLERY_PATH);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && g.count < GALLERY_MAX_FILES) {
        if ((has_ext(ent->d_name, ".jpg") || has_ext(ent->d_name, ".jpeg") || has_ext(ent->d_name, ".gif"))
                && strlen(ent->d_name) < GALLERY_NAME_LEN) {
            strcpy(g.names[g.count++], ent->d_name);
        }
    }
    closedir(dir);
    qsort(g.names, g.count, GALLERY_NAME_LEN, name_cmp);
    ESP_LOGI(TAG, "%d file(s) in %s", g.count, STORAGE_GALLERY_PATH);
}

static void free_picture(void)
{
    if (g.picture) {
        lv_obj_delete(g.picture);   // delete the GIF widget before freeing the data it reads
        g.picture = NULL;
    }
    free(g.gif_data);
    g.gif_data = NULL;
}

// Read a whole file into PSRAM
static uint8_t *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = len > 0 ? heap_caps_malloc(len, MALLOC_CAP_SPIRAM) : NULL;
    if (buf && fread(buf, 1, len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = buf ? (size_t)len : 0;
    return buf;
}

static lv_obj_t *gif_create(const char *name)
{
    char path[GALLERY_PATH_LEN];
    snprintf(path, sizeof(path), STORAGE_GALLERY_PATH "/%.*s", GALLERY_NAME_LEN - 1, name);

    size_t size;
    g.gif_data = load_file(path, &size);
    if (!g.gif_data) {
        ESP_LOGE(TAG, "cannot load %s", path);
        return NULL;
    }
    g.gif_dsc = (lv_image_dsc_t) {
        .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RAW },
        .data = g.gif_data,
        .data_size = size,
    };
    lv_obj_t *gif = lv_gif_create(g.view);
    lv_gif_set_color_format(gif, LV_COLOR_FORMAT_RGB565);
    lv_gif_set_src(gif, &g.gif_dsc);
    return gif;
}

static lv_obj_t *jpg_create(const char *name)
{
    char path[GALLERY_PATH_LEN];
    snprintf(path, sizeof(path), STORAGE_LVGL_PREFIX STORAGE_GALLERY_PATH "/%.*s", GALLERY_NAME_LEN - 1, name);

    lv_obj_t *img = lv_image_create(g.view);
    lv_image_set_src(img, path);
    return img;
}

static void show(int index)
{
    if (g.count == 0) {
        return;
    }
    g.index = (index + g.count) % g.count;     // wrap around both ways
    const char *name = g.names[g.index];

    free_picture();
    g.picture = has_ext(name, ".gif") ? gif_create(name) : jpg_create(name);
    if (g.picture) {
        lv_obj_remove_flag(g.picture, LV_OBJ_FLAG_CLICKABLE);  // let swipes reach the view
        lv_obj_center(g.picture);
    }
    lv_label_set_text_fmt(g.caption, "%d / %d   %s", g.index + 1, g.count, name);
}

static void nav_clicked(lv_event_t *e)
{
    show(g.index + (int)(intptr_t)lv_event_get_user_data(e));
}

static void view_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) {
        show(g.index + 1);
    } else if (dir == LV_DIR_RIGHT) {
        show(g.index - 1);
    }
}

static lv_obj_t *nav_button_create(lv_obj_t *parent, const char *symbol, int step, lv_align_t align)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, 56);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_50, 0);
    lv_obj_align(btn, align, 0, 0);
    lv_obj_add_event_cb(btn, nav_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)step);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_center(lbl);
    return btn;
}

static void gallery_create(lv_obj_t *parent)
{
    memset(&g, 0, sizeof(g));
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    scan_files();
    if (g.count == 0) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, "Gallery is empty.\nPut photos in data/gallery,\nthen build & flash.");
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
        return;
    }

    g.view = lv_obj_create(parent);
    lv_obj_set_size(g.view, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(g.view, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g.view, 0, 0);
    lv_obj_set_style_pad_all(g.view, 0, 0);
    lv_obj_remove_flag(g.view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g.view, view_gesture, LV_EVENT_GESTURE, NULL);

    if (g.count > 1) {
        nav_button_create(parent, LV_SYMBOL_LEFT, -1, LV_ALIGN_LEFT_MID);
        nav_button_create(parent, LV_SYMBOL_RIGHT, +1, LV_ALIGN_RIGHT_MID);
    }

    g.caption = lv_label_create(parent);
    lv_obj_set_style_text_font(g.caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(g.caption, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g.caption, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g.caption, 10, 0);
    lv_obj_set_style_pad_ver(g.caption, 4, 0);
    lv_obj_set_style_radius(g.caption, 8, 0);
    lv_obj_align(g.caption, LV_ALIGN_BOTTOM_MID, 0, -8);

    show(0);
}

static void gallery_destroy(void)
{
    free_picture();
}

const app_desc_t gallery_app = {
    .name = "Gallery",
    .icon = LV_SYMBOL_IMAGE,
    .color = 0xE91E63,
    .create = gallery_create,
    .destroy = gallery_destroy,
};
