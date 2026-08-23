/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "buffer.h"

extern int visible_lines(void);  // Didefinisikan di render.c

// Search bagian dari Buffer.c, tetapi dikarenakan File Buffer.c sudah membengkak sampai 1000 lines
// ++ akan lebih bijak jika search buffer di pisah dari Buffer.c tetapi tetap masih menggunakan
// header buffer.h untuk pintu masuknya, dan untuk UI masih sama di file popup.c dengan header ui.h
// Fitur ini sudah integrasi dengan tree-sitter untuk filter bahwa hasil pencarian CUMA untuk
// Function dan lain-lain, untuk Comment akan di skip otomatis.

/**
 * Helper internal untuk cek apakah Comment [PRIVATE API]
 */
bool is_node_comment(TSNode node) {
    while (!ts_node_is_null(node)) {
        const char *type = ts_node_type(node);

        if (strstr(type, "comment")) {
            return true;
        }
        node = ts_node_parent(node);
    }
    return false;
}

/**
 * Helper internal untuk mencari Posisi di Tree-sitter [PRIVATE API]
 */
bool is_position_in_comment(TSTree *tree, uint32_t start_byte, uint32_t end_byte) {
    if (!tree) return false;

    TSNode root = ts_tree_root_node(tree);
    TSNode node = ts_node_named_descendant_for_byte_range(root, start_byte, end_byte);
    return is_node_comment(node);
}

/**
 * Fungsi untuk akomodasi Search [PUBLIC API]
 */
int Buffer_search(Buffer *buf, const char *query, SearchHitBuffer *out, int max_hits) {
    if (!buf || !query || !query[0] || !out || max_hits <= 0) return 0;

    int count = 0;
    size_t q_len = strlen(query);

    for (size_t y = 0; y < buf->lines.line_count && count < max_hits; y++) {
        char *line = Buffer_get_line_text(buf, y);
        if (!line) continue;

        const char *p = line;
        size_t line_start_byte = buf->lines.offset[y];
        while ((p = strcasestr(p, query)) != NULL) {
            size_t col = (size_t)(p - line);
            uint32_t match_start_byte = (uint32_t)(line_start_byte + col);
            uint32_t match_end_byte = match_start_byte + (uint32_t)q_len;

            if (buf->state &&
                is_position_in_comment(buf->state->tree, match_start_byte, match_end_byte)) {
                p += (q_len > 0 ? q_len : 1);
                continue;  // Lanjut cari kata kunci berikutnya tanpa dimasukkan ke results
            }

            SearchHitBuffer *h = &out[count++];
            h->line = y;
            h->col = col;

            snprintf(h->label, sizeof(h->label), "[%zu:%zu] %.20s...", y + 1, col + 1, p);

            p += (q_len > 0 ? q_len : 1);
            if (count >= max_hits) break;
        }
        free(line);
    }
    return count;
}

/**
 * Fungsi untuk melompat ke Hasil pencarian [PUBLIC API]
 */
void Buffer_goto_search_hit(Buffer *buf, const SearchHitBuffer *hit) {
    if (!buf || !hit) return;

    buf->cursor.y = hit->line;
    buf->cursor.x = hit->col;

    if (hit->line < buf->lines.line_count) {
        buf->cursor.cursor_pos = buf->lines.offset[hit->line] + hit->col;
    } else {
        buf->cursor.cursor_pos = String_len(buf->str);
    }

    buf->selection.is_selected = false;

    // scroll biar kelihatan
    int vis = visible_lines();
    if ((int)buf->cursor.y < buf->scroll_y) buf->scroll_y = (int)buf->cursor.y;
    if ((int)buf->cursor.y >= buf->scroll_y + vis) buf->scroll_y = (int)buf->cursor.y - vis + 1;
}
