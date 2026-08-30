/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef BUFFER_H
#define BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "clipboard.h"
#include "lsp_server.h"
#include "rope.h"
#include "syntax.h"
#include "undo.h"

// Flag untuk penanda Buf Flag
constexpr uint8_t BUF_IS_DIRTY = (1 << 0);
constexpr uint8_t BUF_IS_DRAGGING = (1 << 1);
constexpr uint8_t BUF_IS_SELECT = (1 << 2);

/**
 * Enum untuk Gutter Git
 */
typedef enum {
    GUTTER_NONE = 0,
    GUTTER_ADDED,     // Hijau (+)
    GUTTER_MODIFIED,  // Kuning (~)
    GUTTER_DELETED    // Merah (-)
} GutterStatus;

/**
 * Struct untuk menyimpan Line
 */
typedef struct {
    GutterStatus status;    // GUTTER_ADDED, GUTTER_MODIFIED
    double last_edited_at;  // Timestamp dari GetTime() Raylib saat baris di-edit
    char author[64];
} LineGitMeta;

/**
 * Struct untuk membungkus Position
 */
typedef struct {
    size_t x;
    size_t y;
    size_t cursor_pos;
} Position;

/**
 * Struct pembungkus untuk membungkus LineIndex mulai dari line count dan offset dari cursor pos
 * Digunakan untuk indexing cepat.
 */
typedef struct {
    size_t *offset;
    size_t line_count;
    size_t capacity;
} LineIndex;

/**
 * Struct pembungkus untuk Buffer Editor, Struct ini tier ke 2 setelah String (Rope)
 */
typedef struct {
    char *path;
    char *filename;
    char *language_id;
    String *str;
    SyntaxState *state;  // Tree-sitter
    int lsp_version;
    UndoStack undo;

    LineIndex lines;
    Position cursor;

    // Pengganti struct Selection
    union {
        size_t start;
    };
    int scroll_y;       // Ui State
    uint8_t buf_flags;  // Buffer flag penanda

    DiagnosticList *diagnostic;  // Diagnostic

    // Git
    LineGitMeta *line_git;
    size_t meta_capacity;
} Buffer;

/**
 * Struct untuk menampung Search
 */
typedef struct {
    char label[256];
    size_t line;
    size_t col;
} SearchHitBuffer;

Buffer *Buffer_new();
Buffer *Buffer_open(const char *filename);
void Buffer_insert(Buffer *buf, size_t pos_idx, const char *ch);
void Buffer_delete(Buffer *buf, size_t pos_idx);
void Buffer_save(Buffer *buf, const char *filename);
void Get_selected_position(Buffer *buf, size_t *start, size_t *len);
char *Buffer_get_line_text(Buffer *buf, size_t y);
void Buffer_free(Buffer *buf);
char *Path_to_uri(const char *path);
void Buffer_get_current_word(Buffer *buf, char *out_str, size_t max_len);
char Buffer_get_char_at(Buffer *buf, size_t line, size_t col);
void lsp_apply_completion(Buffer *buf, const CompletionItem *item);  // LSP

// Search
int Buffer_search(Buffer *buf, const char *query, SearchHitBuffer *out, int max_hits);

// Buffer, Clipboard dan Undo Redo
void Buffer_copy(Buffer *buf, Clipboard *clp);
void Buffer_cut(Buffer *buf, Clipboard *clp);
void Buffer_paste(Buffer *buf, Clipboard *clp);
void Buffer_undo(Buffer *buf);
void Buffer_redo(Buffer *buf);

#endif  // !BUFFER_H
