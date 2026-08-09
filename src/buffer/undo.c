#include "undo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================
 * PRIVATE API
 * ============================ */

/**
 * Fungsi untuk mendapatkan waktu dalam milidetik [PRIVATE API]
 */
static long now_ms() { return (long)(clock() * 1000 / CLOCKS_PER_SEC); }

/**
 * Fungsi untuk membersihkan stack undo [PRIVATE API]
 */
void Undo_free(UndoStack *us) {
    if (!us || !us->actions) return;

    for (size_t i = 0; i < us->count; i++) {
        if (us->actions[i].text) {
            free(us->actions[i].text);
            us->actions[i].text = NULL;  // Biar anti double free!
        }
    }

    free(us->actions);
    us->actions = NULL;  // Biar kalau dipanggil lagi aman
    us->count = 0;
    us->capacity = 0;
    us->current = 0;
}

/**
 * Fungsi untuk membersihkan stack undo [PRIVATE API]
 */
static void Undo_discard_redo(UndoStack *us) {
    for (size_t i = us->current; i < us->count; i++) {
        free(us->actions[i].text);
    }

    us->count = us->current;
}

/* ==========================
 * PUBLIC API
 * ========================== */

/**
 * Fungsi untuk menginisialisasi stack undo [PUBLIC API]
 */
void Undo_init(UndoStack *us) {
    us->capacity = 128;
    us->actions = malloc(sizeof(UndoAction) * us->capacity);
    us->count = 0;
    us->current = 0;
    us->is_undoing = false;
}

/**
 * Fungsi untuk membersihkan stack undo [PUBLIC API]
 */
void Undo_clear(UndoStack *us) {
    Undo_free(us);
    Undo_init(us);
}

/**
 * Fungsi untuk menambahkan action ke stack undo [PUBLIC API]
 */
void Undo_push(UndoStack *us, UndoType type, size_t offset, const char *text, size_t len) {
    if (len == 0 || !us || us->is_undoing) return;
    Undo_discard_redo(us);

    long ts = now_ms();

    if (us->count > 0 && type == UNDO_INSERT) {
        UndoAction *last = &us->actions[us->count - 1];

        if (last->type == UNDO_INSERT && last->offset + last->len == offset &&
            ts - last->timestamp_ms < UNDO_TIMEOUT) {
            last->text = realloc(last->text, last->len + len + 1);
            memcpy(last->text + last->len, text, len);

            last->len += len;
            last->text[last->len] = '\0';
            last->timestamp_ms = ts;
            us->current = us->count;
            return;
        }
    }

    if (us->count > 0 && type == UNDO_DELETE) {
        UndoAction *last = &us->actions[us->count - 1];
        if (last->type == UNDO_DELETE && offset + len == last->offset &&
            ts - last->timestamp_ms < UNDO_TIMEOUT) {
            char *new_text = malloc(last->len + len + 1);
            memcpy(new_text, text, len);                    // Teks baru di depan
            memcpy(new_text + len, last->text, last->len);  // Teks lama di belakang

            free(last->text);
            last->text = new_text;
            last->offset = offset;
            last->len += len;
            last->timestamp_ms = ts;
            new_text[last->len] = '\0';
            return;
        }
    }

    if (us->count >= us->capacity) {
        us->capacity *= 2;
        us->actions = realloc(us->actions, sizeof(UndoAction) * us->capacity);
    }

    if (us->count >= UNDO_MAX_ACTIONS) {
        free(us->actions[0].text);
        memmove(us->actions, us->actions + 1, sizeof(UndoAction) * (us->count - 1));
        us->count--;
        us->current--;
    }

    UndoAction *a = &us->actions[us->count++];
    a->type = type;
    a->offset = offset;
    a->len = len;
    a->text = malloc(len + 1);
    memcpy(a->text, text, len);
    a->text[len] = '\0';
    a->timestamp_ms = ts;
    us->current++;
}

/**
 * Fungsi untuk mem-pop action dari stack undo [PUBLIC API]
 */
bool Undo_pop(UndoStack *us, UndoAction *out) {
    if (us->current == 0) return false;
    us->current--;
    *out = us->actions[us->current];
    return true;
}

/**
 * Fungsi untuk mem-pop action dari stack undo [PUBLIC API]
 */
bool Redo_pop(UndoStack *us, UndoAction *out) {
    if (us->current >= us->count) return false;
    *out = us->actions[us->current];
    us->current++;
    return true;
}
