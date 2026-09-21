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
#include "types.h"

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
