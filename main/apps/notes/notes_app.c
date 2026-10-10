/*
 * Notes: list, write (on-screen keyboard), edit and delete text notes.
 *
 * Two views inside the app area: the list and the editor. Switching views deletes the
 * widgets of the current one, so it is done with lv_async_call() (never delete the
 * button whose click is being handled).
 */
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "ui/ui_common.h"
#include "notes_store.h"
#include "notes_app.h"

static const char *TAG = "notes_app";

static struct {
    lv_obj_t *root;         // the app area
    lv_obj_t *textarea;     // editor only
    bool editing;
    uint32_t id;            // note being edited, 0 = new
    uint32_t open_id;       // note to open on the next show_editor() call
} n;

static note_info_t notes[NOTES_MAX_COUNT];
static char text_buf[NOTES_MAX_LEN];

static void show_editor(void *unused);
static void open_clicked(lv_event_t *e);

static void show_list(void *unused)
{
    n.editing = false;
    n.textarea = NULL;
    lv_obj_clean(n.root);

    // Every button carries the id of the note it opens (0 = new note)
    lv_obj_t *add = lv_button_create(n.root);
    lv_obj_set_width(add, lv_pct(100));
    lv_obj_add_event_cb(add, open_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)0);
    lv_obj_t *lbl = lv_label_create(add);
    lv_label_set_text(lbl, LV_SYMBOL_PLUS "  New note");
    lv_obj_center(lbl);

    int count = notes_store_list(notes, NOTES_MAX_COUNT);
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(n.root);
        lv_label_set_text(empty, "No notes yet.");
        lv_obj_set_style_text_color(empty, lv_color_hex(UI_COLOR_TEXT_DIM), 0);
    }

    lv_obj_t *list = lv_list_create(n.root);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    for (int i = 0; i < count; i++) {
        lv_obj_t *item = lv_list_add_button(list, LV_SYMBOL_FILE, notes[i].title);
        lv_obj_add_event_cb(item, open_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)notes[i].id);
    }
    if (count == 0) {
        lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
    }
}

static void open_clicked(lv_event_t *e)
{
    n.open_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    lv_async_call(show_editor, NULL);
}

static void save_current(void)
{
    const char *text = lv_textarea_get_text(n.textarea);
    if (text[0] == '\0') {
        return;     // nothing typed: don't create empty notes
    }
    if (notes_store_save(&n.id, text) != ESP_OK) {
        ESP_LOGE(TAG, "save failed");
    }
}

static void save_clicked(lv_event_t *e)
{
    save_current();
    lv_async_call(show_list, NULL);
}

static void delete_clicked(lv_event_t *e)
{
    if (n.id) {
        notes_store_delete(n.id);
    }
    lv_async_call(show_list, NULL);
}

static lv_obj_t *small_button_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
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

static void show_editor(void *unused)
{
    n.editing = true;
    n.id = n.open_id;
    lv_obj_clean(n.root);

    lv_obj_t *row = lv_obj_create(n.root);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    small_button_create(row, LV_SYMBOL_SAVE " Save", save_clicked);
    lv_obj_t *del = small_button_create(row, LV_SYMBOL_TRASH " Delete", delete_clicked);
    lv_obj_set_style_bg_color(del, lv_palette_main(LV_PALETTE_RED), 0);

    n.textarea = lv_textarea_create(n.root);
    lv_obj_set_width(n.textarea, lv_pct(100));
    lv_obj_set_flex_grow(n.textarea, 1);
    lv_textarea_set_max_length(n.textarea, NOTES_MAX_LEN - 1);
    lv_textarea_set_placeholder_text(n.textarea, "The first line becomes the title...");
    if (n.id && notes_store_load(n.id, text_buf, sizeof(text_buf)) == ESP_OK) {
        lv_textarea_set_text(n.textarea, text_buf);
    }

    lv_obj_t *kb = lv_keyboard_create(n.root);
    lv_obj_set_size(kb, lv_pct(100), 190);
    lv_keyboard_set_textarea(kb, n.textarea);
    lv_obj_add_event_cb(kb, save_clicked, LV_EVENT_READY, NULL);   // the keyboard's check key saves too
}

static void notes_create(lv_obj_t *parent)
{
    memset(&n, 0, sizeof(n));
    n.root = parent;
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 10, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    show_list(NULL);
}

static void notes_destroy(void)
{
    lv_async_call_cancel(show_list, NULL);
    lv_async_call_cancel(show_editor, NULL);
    if (n.editing) {
        save_current();     // leaving with the back button keeps what was typed
    }
}

const app_desc_t notes_app = {
    .name = "Notes",
    .icon = LV_SYMBOL_EDIT,
    .color = 0x4CAF50,
    .create = notes_create,
    .destroy = notes_destroy,
};
