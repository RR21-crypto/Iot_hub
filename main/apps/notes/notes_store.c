/*
 * Notes storage: one text file per note, /user/note_XXXXXXXX.txt (id in hex).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "services/storage.h"
#include "notes_store.h"

static const char *TAG = "notes";

#define NOTE_PREFIX     "note_"
#define NOTE_PATH_LEN   48

static void note_path(char *buf, size_t len, uint32_t id)
{
    snprintf(buf, len, STORAGE_USER_PATH "/" NOTE_PREFIX "%08lx.txt", (unsigned long)id);
}

static bool parse_id(const char *name, uint32_t *id)
{
    unsigned long v;
    if (sscanf(name, NOTE_PREFIX "%8lx.txt", &v) != 1 || v == 0) {
        return false;
    }
    *id = (uint32_t)v;
    return true;
}

static void read_title(uint32_t id, char *title, size_t len)
{
    char path[NOTE_PATH_LEN];
    note_path(path, sizeof(path), id);
    title[0] = '\0';
    FILE *f = fopen(path, "r");
    if (f) {
        if (fgets(title, len, f)) {
            title[strcspn(title, "\r\n")] = '\0';
        }
        fclose(f);
    }
    if (title[0] == '\0') {
        snprintf(title, len, "(empty)");
    }
}

static int newest_first(const void *a, const void *b)
{
    uint32_t ia = ((const note_info_t *)a)->id, ib = ((const note_info_t *)b)->id;
    return ia < ib ? 1 : ia > ib ? -1 : 0;
}

int notes_store_list(note_info_t *out, int max)
{
    int count = 0;
    DIR *dir = opendir(STORAGE_USER_PATH);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", STORAGE_USER_PATH);
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && count < max) {
        if (parse_id(ent->d_name, &out[count].id)) {
            count++;
        }
    }
    closedir(dir);

    qsort(out, count, sizeof(out[0]), newest_first);
    for (int i = 0; i < count; i++) {
        read_title(out[i].id, out[i].title, sizeof(out[i].title));
    }
    return count;
}

esp_err_t notes_store_load(uint32_t id, char *buf, size_t len)
{
    char path[NOTE_PATH_LEN];
    note_path(path, sizeof(path), id);
    FILE *f = fopen(path, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return ESP_OK;
}

esp_err_t notes_store_save(uint32_t *id, const char *text)
{
    if (*id == 0) {
        // New id = highest existing id + 1 (static: too big for the caller's stack)
        static note_info_t all[NOTES_MAX_COUNT];
        int n = notes_store_list(all, NOTES_MAX_COUNT);
        if (n >= NOTES_MAX_COUNT) {
            return ESP_ERR_NO_MEM;
        }
        *id = n ? all[0].id + 1 : 1;
    }

    char path[NOTE_PATH_LEN];
    note_path(path, sizeof(path), *id);
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot write %s", path);
        return ESP_FAIL;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    ESP_LOGI(TAG, "saved note %lu (%u bytes)", (unsigned long)*id, (unsigned)len);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t notes_store_delete(uint32_t id)
{
    char path[NOTE_PATH_LEN];
    note_path(path, sizeof(path), id);
    return remove(path) == 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
