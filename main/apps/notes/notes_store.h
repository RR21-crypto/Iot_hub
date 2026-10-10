/*
 * Notes storage: one text file per note in /user (kept across flashes).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NOTES_MAX_COUNT     50
#define NOTES_MAX_LEN       1024    // characters per note
#define NOTES_TITLE_LEN     40

typedef struct {
    uint32_t id;
    char title[NOTES_TITLE_LEN];    // first line of the note
} note_info_t;

/**
 * @brief List the notes, newest first
 *
 * @return number of notes written to `out`
 */
int notes_store_list(note_info_t *out, int max);

esp_err_t notes_store_load(uint32_t id, char *buf, size_t len);

/**
 * @brief Save a note
 *
 * @param[in,out] id 0 for a new note (receives the new id), otherwise the note to overwrite
 */
esp_err_t notes_store_save(uint32_t *id, const char *text);

esp_err_t notes_store_delete(uint32_t id);
