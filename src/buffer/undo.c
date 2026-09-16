/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "undo.h"

#include <bits/time.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * Fungsi untuk mendapatkan waktu dalam milidetik [PRIVATE API]
 */
static long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1'000 + ts.tv_nsec / 1'000'000);
}

/**
 * Fungsi untuk membersihkan stack undo [PRIVATE API]
 */
void Undo_free(UndoStack *us) {
    if (!us || !us->actions) return;

    for (size_t i = 0; i < us->count; i++) {
        if (us->actions[i].text) {
            free(us->actions[i].text);
            us->actions[i].text = nullptr;  // Biar anti double free!
        }
    }

    free(us->actions);
    us->actions = nullptr;  // Biar kalau dipanggil lagi aman
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

/**
 * Fungsi untuk menginisialisasi stack undo [PUBLIC API]
 */
void Undo_init(UndoStack *us) {
    us->capacity = 128;
    us->actions = calloc(us->capacity, sizeof(UndoAction));
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

    if (us->count > 0) {
        UndoAction *last = &us->actions[us->count - 1];

        // Merge INSERT (Ngetik beruntun)
        if (type == UNDO_INSERT && last->type == UNDO_INSERT &&
            last->offset + last->len == offset && (ts - last->timestamp_ms < UNDO_TIMEOUT)) {
            char *tmp = realloc(last->text, last->len + len + 1);
            if (tmp) {
                last->text = tmp;
                memcpy(last->text + last->len, text, len);
                last->len += len;
                last->text[last->len] = '\0';
                last->timestamp_ms = ts;
                return;
            }
        }

        // Merge DELETE BACKSPACE (Delete ke kiri: pos mundur)
        if (type == UNDO_DELETE && last->type == UNDO_DELETE && offset + len == last->offset &&
            (ts - last->timestamp_ms < UNDO_TIMEOUT)) {
            char *tmp = realloc(last->text, last->len + len + 1);
            if (tmp) {
                last->text = tmp;
                // Geser teks lama ke kanan, masukkan teks baru di depan
                memmove(last->text + len, last->text, last->len + 1);
                memcpy(last->text, text, len);
                last->offset = offset;
                last->len += len;
                last->timestamp_ms = ts;
                return;
            }
        }

        // Merge DELETE KEY (Delete ke kanan: pos tetap sama)
        if (type == UNDO_DELETE && last->type == UNDO_DELETE && offset == last->offset &&
            (ts - last->timestamp_ms < UNDO_TIMEOUT)) {
            char *tmp = realloc(last->text, last->len + len + 1);
            if (tmp) {
                last->text = tmp;
                memcpy(last->text + last->len, text, len);
                last->len += len;
                last->text[last->len] = '\0';
                last->timestamp_ms = ts;
                return;
            }
        }
    }

    // Dynamic array expansion biasa jika belum melebihi limit
    if (us->count >= us->capacity) {
        size_t new_cap = us->capacity * 2;
        UndoAction *tmp = realloc(us->actions, sizeof(UndoAction) * new_cap);
        if (!tmp) return;
        us->actions = tmp;
        us->capacity = new_cap;
    }

    // Tambahkan action baru
    UndoAction *a = &us->actions[us->count++];
    a->type = type;
    a->offset = offset;
    a->len = len;
    a->text = malloc(len + 1);
    if (a->text) {
        memcpy(a->text, text, len);
        a->text[len] = '\0';
    }
    a->timestamp_ms = ts;
    us->current = us->count;
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
