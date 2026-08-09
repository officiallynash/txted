#ifndef BUFFER_H
#define BUFFER_H

#include <stdbool.h>

#include "clipboard.h"
#include "lsp_server.h"
#include "rope.h"
#include "syntax.h"
#include "undo.h"

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
 * Struct untuk membungkus Selection,
 * Menyimpan is_selected dan start.
 */
typedef struct {
    bool is_selected;
    union {
        size_t start;
    };
} Selection;

/**
 * Struct pembungkus untuk Buffer Editor, Struct ini tier ke 2 setelah String (Rope)
 */
typedef struct {
    String *str;
    SyntaxState *state;
    char *language_id;
    int lsp_version;
    UndoStack undo;

    LineIndex lines;
    Position cursor;
    Selection selection;

    int scroll_y;  // Ui State
    bool is_dirty;
    char *path;
    char *filename;
} Buffer;

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

// Buffer, Clipboard dan Undo Redo
void Buffer_copy(Buffer *buf, Clipboard *clp);
void Buffer_cut(Buffer *buf, Clipboard *clp);
void Buffer_paste(Buffer *buf, Clipboard *clp);
void Buffer_undo(Buffer *buf);
void Buffer_redo(Buffer *buf);

#endif  // !BUFFER_H
