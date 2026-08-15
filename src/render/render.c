/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "git_client.h"
#include "lsp_ui.h"
#include "raylib.h"
#include "syntax.h"
#include "theme.h"
#include "ui.h"

/* ===================================
 * PRIVATE API
 * =================================== */

/**
 * Struct untuk bracket matching
 */
typedef struct {
    size_t x;
    size_t y;
    bool found;
} BracketMatch;

/**
 * Helper untuk menghitung baris yang muat di layar
 */
int visible_lines(void) {
    int win_h = GetRenderHeight();  // Pake ukuran window aktual!
    int editor_h = win_h - TAB_H - STATUS_H - DIAG_PANEL_H;
    // Kurangi PAD_Y * 2 (atas dan bawah) biar gak bablas
    return (editor_h - (PAD_Y * 2)) / LINE_H;
}

/**
 * Fungsi untuk expands Tab
 */
void expand_tabs(const char *src, char *dst, size_t dst_size, int tab_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; i++) {
        if (src[i] == '\t') {
            // Hitung berapa spasi yang dibutuhkan untuk menyamai batas tab stop berikutnya
            int spaces = tab_size - (j % tab_size);
            for (int k = 0; k < spaces && j < dst_size - 1; k++) {
                dst[j++] = ' ';
            }
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

/**
 * Fungsi untuk mendapatkan warna token (Prefix matching untuk Tree-sitter)
 */
static Color Get_token_color(const char *capture_name) {
    if (!capture_name) return g_theme.text_normal;

    if (strncmp(capture_name, "keyword", 7) == 0) return g_theme.keyword;    // Merah/Pink
    if (strncmp(capture_name, "type", 4) == 0) return g_theme.type;          // Biru Muda
    if (strncmp(capture_name, "string", 6) == 0) return g_theme.string;      // Kuning
    if (strncmp(capture_name, "number", 6) == 0) return g_theme.number;      // Ungu
    if (strncmp(capture_name, "float", 5) == 0) return g_theme.number;       // Ungu
    if (strncmp(capture_name, "function", 8) == 0) return g_theme.function;  // Hijau
    if (strncmp(capture_name, "method", 6) == 0) return g_theme.method;      // Hijau
    if (strncmp(capture_name, "comment", 7) == 0) return g_theme.comment;    // Abu-abu
    if (strncmp(capture_name, "constant", 8) == 0) return g_theme.constant;  // Ungu
    if (strncmp(capture_name, "operator", 8) == 0) return g_theme.operator;  // Pink

    return g_theme.text_normal;  // Fallback
}

/**
 * Fungsi utama untuk menggambar baris dengan highlight
 */
void Draw_line_highlighted(Font font, const char *line_text, Vector2 pos, HighlightToken *tokens,
                           int token_count, size_t line_start_byte) {
    size_t len = strlen(line_text);
    float current_x = pos.x;
    float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;
    int col_visual = 0;

    for (size_t i = 0; i < len; i++) {
        size_t current_byte = line_start_byte + i;

        // Cek warna token untuk byte saat ini
        Color color = g_theme.text_normal;
        for (int t = 0; t < token_count; t++) {
            if (current_byte >= tokens[t].start_byte && current_byte < tokens[t].end_byte) {
                color = Get_token_color(tokens[t].capture_name);
                break;
            }
        }

        // Gambar karakter (dengan penanganan Tab)
        if (line_text[i] == '\t') {
            int spaces = 4 - (col_visual % 4);
            current_x += space_w * spaces;
            col_visual += spaces;
        } else {
            char chunk[2] = {line_text[i], '\0'};
            DrawTextEx(font, chunk, (Vector2){current_x, pos.y}, FONT_SIZE, 1.0f, color);
            current_x += MeasureTextEx(font, chunk, FONT_SIZE, 1.0f).x;
            col_visual++;
        }
    }
}

/**
 * Fungsi untuk mencari Bracket
 */
void find_matching_brackets(Buffer *buf, BracketMatch *b1, BracketMatch *b2) {
    b1->found = false;
    b2->found = false;

    char *line = Buffer_get_line_text(buf, buf->cursor.y);
    if (!line) return;

    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') len--;

    // Cek posisi kursor saat ini DAN 1 posisi di sebelah kiri kursor
    size_t check_cols[2] = {buf->cursor.x, (buf->cursor.x > 0) ? buf->cursor.x - 1 : 0};
    size_t target_col = 0;
    char c = '\0';
    bool col_found = false;

    for (int i = 0; i < 2; i++) {
        size_t col = check_cols[i];
        if (col < len) {
            char ch = line[col];
            if (ch == '(' || ch == '{' || ch == '[' || ch == ')' || ch == '}' || ch == ']' ||
                ch == '<' || ch == '>') {
                c = ch;
                target_col = col;
                col_found = true;
                break;
            }
        }
    }
    free(line);

    if (!col_found) return;

    b1->x = target_col;
    b1->y = buf->cursor.y;
    b1->found = true;

    /* ------------------------------------------------------------- *
     * PENCARIAN MAJU (OPENING BRACKET: (, {, [)
     * ------------------------------------------------------------- */
    if (c == '(' || c == '{' || c == '[' || c == '<') {
        char match_c = (c == '(') ? ')' : (c == '{') ? '}' : (c == '[') ? ']' : '>';
        int depth = 1;

        for (size_t y = buf->cursor.y; y < buf->lines.line_count; y++) {
            char *l = Buffer_get_line_text(buf, y);
            if (!l) continue;
            size_t l_len = strlen(l);
            if (l_len > 0 && l[l_len - 1] == '\n') l_len--;

            size_t start_x = (y == buf->cursor.y) ? target_col + 1 : 0;
            for (size_t x = start_x; x < l_len; x++) {
                if (l[x] == c)
                    depth++;
                else if (l[x] == match_c) {
                    depth--;
                    if (depth == 0) {
                        b2->x = x;
                        b2->y = y;
                        b2->found = true;
                        free(l);
                        return;
                    }
                }
            }
            free(l);
        }
    }
    /* ------------------------------------------------------------- *
     * PENCARIAN MUNDUR (CLOSING BRACKET: ), }, ])
     * ------------------------------------------------------------- */
    else if (c == ')' || c == '}' || c == ']' || c == '>') {
        char match_c = (c == ')') ? '(' : (c == '}') ? '{' : (c == ']') ? '[' : '<';
        int depth = 1;

        for (int y = (int)buf->cursor.y; y >= 0; y--) {
            char *l = Buffer_get_line_text(buf, (size_t)y);
            if (!l) continue;
            size_t l_len = strlen(l);
            if (l_len > 0 && l[l_len - 1] == '\n') l_len--;

            int start_x = (y == (int)buf->cursor.y) ? (int)target_col - 1 : (int)l_len - 1;
            for (int x = start_x; x >= 0; x--) {
                if (l[x] == c)
                    depth++;
                else if (l[x] == match_c) {
                    depth--;
                    if (depth == 0) {
                        b2->x = (size_t)x;
                        b2->y = (size_t)y;
                        b2->found = true;
                        free(l);
                        return;
                    }
                }
            }
            free(l);
        }
    }
}

/**
 * Hitung posisi X piksel dari kolom berbasis teks (Sama persis seperti kursor)
 */
static float get_text_column_x(Font font, const char *text, size_t target_col, float start_x) {
    if (!text) return start_x;

    size_t len = strlen(text);
    if (len > 0 && text[len - 1] == '\n') len--;
    if (target_col > len) target_col = len;

    float current_x = start_x;
    float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;
    int col_visual = 0;

    for (size_t i = 0; i < target_col; i++) {
        if (text[i] == '\t') {
            int spaces = 4 - (col_visual % 4);
            current_x += space_w * spaces;
            col_visual += spaces;
        } else {
            char ch[2] = {text[i], '\0'};
            current_x += MeasureTextEx(font, ch, FONT_SIZE, 1.0f).x;
            col_visual++;
        }
    }

    return current_x;
}

/* ===============================
 * PUBLIC API
 * =============================== */

/**
 * Fungsi untuk Draw main Editor
 */
void draw_editor(BufManager *bufmgr, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);  // Active buffer
    if (!buf) return;

    EditorLayout Layout = get_editor_layout(bufmgr);

    int max_vis = visible_lines();

    // Inisiasi Bracket matching
    BracketMatch b1;
    BracketMatch b2;
    find_matching_brackets(buf, &b1, &b2);

    /* Buka Scissor cuma untuk area antara TAB dan STATUS BAR */
    BeginScissorMode(Layout.editor_x, Layout.editor_y, Layout.editor_w, Layout.editor_h);
    /* Fill the Editor*/
    DrawRectangle(Layout.editor_x, Layout.editor_y, Layout.editor_w, Layout.editor_h,
                  g_theme.bg_editor);

    /* Background Gutter */
    DrawRectangle(Layout.gutter_screen_x, Layout.editor_y, GUTTER_W, Layout.editor_h,
                  g_theme.bg_editor);

    /* clamp scroll */
    if (buf->scroll_y < 0) buf->scroll_y = 0;
    if ((size_t)buf->scroll_y >= buf->lines.line_count)
        buf->scroll_y = (int)buf->lines.line_count > 0 ? (int)buf->lines.line_count - 1 : 0;

    size_t first = (size_t)buf->scroll_y;
    size_t last = first + (size_t)max_vis;
    if (last > buf->lines.line_count) last = buf->lines.line_count;

    int text_x = Layout.text_screen_x;

    /* ------------------------- *
     * GET HIGHLIGHT TOKENS
     * ------------------------- */
    HighlightToken tokens[1024];
    int token_count = 0;

    if (buf->state && buf->str->len > 0) {
        uint32_t start_byte = (uint32_t)buf->lines.offset[first];
        uint32_t end_byte = (last < buf->lines.line_count) ? (uint32_t)buf->lines.offset[last]
                                                           : (uint32_t)buf->str->len;

        Bytes full_text = String_get(buf->str, 0, buf->str->len);
        if (full_text.data) {
            token_count = Syntax_get_highlights(buf->state, (const char *)full_text.data,
                                                start_byte, end_byte, tokens, 1024);
            Bytes_free(&full_text);  // Mencegah memory leak
        }
    }

    for (size_t y = first; y < last; y++) {
        int py = Layout.editor_y + PAD_Y + (int)(y - first) * LINE_H;

        if (y == buf->cursor.y) {
            DrawRectangle(Layout.editor_x + GUTTER_W, py - 4, Layout.editor_w - GUTTER_W, LINE_H,
                          g_theme.active_line);
        }

        /* ------------------------------------------------------------- *
         * RENDER GIT GUTTER INDICATOR BAR & GHOST TEXT
         * ------------------------------------------------------------- */
        if (git.is_repo) {  // Hanya render jika Path adalah repo
            if (buf->line_git && y < buf->meta_capacity) {
                LineGitMeta meta = buf->line_git[y];

                // 1. Render Strip Warna di Samping Kiri Line Number
                if (meta.status != GUTTER_NONE) {
                    float gutter_bar_x = (float)(Layout.editor_x + 4);
                    Rectangle gutter_rect = {gutter_bar_x, (float)py - 2, 3.0f,
                                             (float)LINE_H - 2.0f};

                    Color bar_color =
                        (meta.status == GUTTER_ADDED) ? g_theme.function : g_theme.warning;
                    DrawRectangleRounded(gutter_rect, 0.5f, 2, bar_color);
                }
            }
        }

        /* Line Number */
        char num_str[16];
        snprintf(num_str, sizeof(num_str), "%4zu", y + 1);
        Color num_color = (y == buf->cursor.y) ? g_theme.line_num : g_theme.text_muted;
        Vector2 num_pos = {(float)(Layout.editor_x + PAD_X - 8), (float)py};
        DrawTextEx(font, num_str, num_pos, FONT_SIZE, 1.0f, num_color);

        /* Teks Editor */
        char *text = Buffer_get_line_text(buf, y);
        if (text) {
            size_t len = strlen(text);
            if (len > 0 && text[len - 1] == '\n') text[len - 1] = '\0';
            if (len > 0 && text[len - 1] == '\r') text[len - 1] = '\0';

            char expanded_text[2048];
            expand_tabs(text, expanded_text, sizeof(expanded_text), 4);

            Vector2 pos = {(float)text_x, (float)py};

            /* ---------------- *
             * Highlight Selection
             * ---------------- */
            if (buf->selection.is_selected) {
                size_t sel_start, sel_len;
                Get_selected_position(buf, &sel_start, &sel_len);
                size_t sel_end = sel_start + sel_len;

                size_t line_start = buf->lines.offset[y];
                size_t line_end =
                    (y + 1 < buf->lines.line_count) ? buf->lines.offset[y + 1] : buf->str->len;

                // Jika ada bagian baris ini yang masuk dalam seleksi
                if (sel_start < line_end && sel_end > line_start) {
                    // Hitung indeks karakter relatif terhadap awal baris y
                    size_t char_start = (sel_start > line_start) ? (sel_start - line_start) : 0;
                    size_t char_end =
                        (sel_end < line_end) ? (sel_end - line_start) : (line_end - line_start);

                    // Jika char_end mencakup '\n' di akhir baris, potong agar seleksi
                    // tidak kebablasan
                    size_t text_raw_len = strlen(text);
                    if (text_raw_len > 0 && text[text_raw_len - 1] == '\n') {
                        if (char_end > text_raw_len - 1) char_end = text_raw_len - 1;
                    }

                    // helper yang SAMA DENGAN KURSOR untuk menghitung X1 dan X2
                    float x1 = get_text_column_x(font, text, char_start, (float)text_x);
                    float x2 = get_text_column_x(font, text, char_end, (float)text_x);

                    // Tambah ekstra lebar 8px jika seleksi mencakup newline (pindah baris)
                    if (sel_end >= line_end && y + 1 < buf->lines.line_count) {
                        float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;
                        x2 += space_w;
                    }

                    if (x2 > x1) {
                        DrawRectangle((int)x1, py - 4, (int)(x2 - x1), LINE_H, g_theme.selection);
                    }
                }
            }

            /* ----------------------------- *
             * Bracket Coloring
             * ----------------------------- */
            if (b1.found && b2.found) {
                Color match_bg = (Color){255, 255, 255, 35};
                float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;

                // Cek Kurung Pertama (b1)
                if (y == b1.y) {
                    float x1 = get_text_column_x(font, text, b1.x, (float)text_x);
                    Rectangle r1 = {x1, (float)py - 2, space_w, (float)LINE_H};
                    DrawRectangleRounded(r1, 0.2f, 4, match_bg);
                    DrawRectangleRoundedLines(r1, 0.2f, 4, g_theme.keyword);
                }

                // Cek Kurung Kedua (b2) - Dibuat 'if' terpisah agar kurung sebaris ter-render
                // dua-duanya
                if (y == b2.y) {
                    float x2 = get_text_column_x(font, text, b2.x, (float)text_x);
                    Rectangle r2 = {x2, (float)py - 2, space_w, (float)LINE_H};
                    DrawRectangleRounded(r2, 0.2f, 4, match_bg);
                    DrawRectangleRoundedLines(r2, 0.2f, 4, g_theme.keyword);
                }
            }

            // Render teks dengan highlight [RENDER UTAMA]
            Draw_line_highlighted(font, text, pos, tokens, token_count, buf->lines.offset[y]);

            /* ------------------------------------------------------------- *
             * RENDER INLINE GHOST TEXT
             * ------------------------------------------------------------- */
            if (git.is_repo) {  // Hanya render jika Path adalah Repo
                if (y == buf->cursor.y && buf->line_git && y < buf->meta_capacity) {
                    LineGitMeta meta = buf->line_git[y];

                    // tampil kalau ada waktu (blame ATAU edit lokal)
                    if (meta.last_edited_at > 0) {
                        char ghost_str[64];
                        const char *who =
                            meta.author[0] ? meta.author : (git.author[0] ? git.author : "You");

                        format_time_ago(who, meta.last_edited_at, ghost_str, sizeof(ghost_str));

                        float line_w = get_text_column_x(font, expanded_text, strlen(expanded_text),
                                                         (float)text_x);
                        DrawTextEx(font, ghost_str, (Vector2){line_w + 32.0f, (float)py}, FONT_SIZE,
                                   1.0f, g_theme.text_muted);
                    }
                }
            }

            /* ------------------------------------------------------------- *
             * Render Squiggly / Underline Diagnostics
             * ------------------------------------------------------------- */
            DiagnosticList *diagnostics = lsp_get_diagnostics(g_lsp_ui.uri);

            if (diagnostics && diagnostics->count > 0) {
                for (size_t i = 0; i < diagnostics->count; i++) {
                    DiagnosticItem *item = &diagnostics->items[i];

                    // Cek apakah baris ini masuk dalam range diagnostik
                    if ((size_t)item->start_line <= y && y <= (size_t)item->end_line) {
                        size_t col_start = (y == (size_t)item->start_line) ? item->start_char : 0;
                        size_t col_end = (y == (size_t)item->end_line) ? item->end_char : len;

                        // Jika range-nya 0 karakter, beri minimal 1 karakter agar garis kelihatan
                        if (col_start == col_end && col_start < len) {
                            col_end = col_start + 1;
                        }

                        float x1 = get_text_column_x(font, text, col_start, (float)text_x);
                        float x2 = get_text_column_x(font, text, col_end, (float)text_x);

                        // Tentukan warna garis sesuai severity
                        Color diag_color = g_theme.error;  // Fallback / Error (Severity 1)
                        if (item->severity == 2) {
                            diag_color = g_theme.warning;  // Warning
                        } else if (item->severity >= 3) {
                            diag_color =
                                g_theme.info;  // Info / Hint (Pakai warna g_theme yang sesuai)
                        }

                        // Gambar garis bawah tipis tepat di bawah teks
                        int line_y = py + FONT_SIZE + 2;
                        int line_w = (int)(x2 - x1);
                        if (line_w <= 0) line_w = (int)MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;

                        DrawRectangle((int)x1, line_y, line_w, 2, diag_color);
                    }
                }
            }

            free(text);
        }
    }

    /* ---------------- *
     * Cursor
     * ---------------- */
    if (buf->cursor.y >= first && buf->cursor.y < last) {
        char *cur = Buffer_get_line_text(buf, buf->cursor.y);
        float cx_float = (float)text_x;

        if (cur) {
            size_t clen = strlen(cur);
            if (clen > 0 && cur[clen - 1] == '\n') clen--;

            size_t target_x = buf->cursor.x;
            if (target_x > clen) target_x = clen;

            // Hitung koordinat X kursor secara presisi per-byte
            float space_w = MeasureTextEx(font, " ", FONT_SIZE, 1.0f).x;
            int col_visual = 0;

            for (size_t i = 0; i < target_x; i++) {
                if (cur[i] == '\t') {
                    int spaces = 4 - (col_visual % 4);
                    cx_float += space_w * spaces;
                    col_visual += spaces;
                } else {
                    char ch[2] = {cur[i], '\0'};
                    cx_float += MeasureTextEx(font, ch, FONT_SIZE, 1.0f).x;
                    col_visual++;
                }
            }
            free(cur);
        }

        int cx = (int)cx_float;
        int cy = Layout.editor_y + PAD_Y + (int)(buf->cursor.y - first) * LINE_H;

        int cursor_w = 2;
        int cursor_h = FONT_SIZE + 2;

        if (((int)(GetTime() * 1.5f) % 2) == 0) {
            DrawRectangle(cx, cy - 1, cursor_w, cursor_h, g_theme.cursor);
        }
    }

    /* ------------------------------------------------------------- *
     * RENDER VERTICAL SCROLLBAR (Right Edge)
     * ------------------------------------------------------------- */
    size_t total_lines = buf->lines.line_count;
    if (total_lines > (size_t)max_vis) {
        float scrollbar_w = 8.0f;
        float scrollbar_x = (float)(Layout.editor_x + Layout.editor_w) - scrollbar_w - 2.0f;
        float track_h = (float)Layout.editor_h;
        float track_y = (float)Layout.editor_y;

        // Ratio tinggi thumb terhadap total konten
        float visible_ratio = (float)max_vis / (float)total_lines;
        float thumb_h = track_h * visible_ratio;
        if (thumb_h < 20.0f) thumb_h = 20.0f;  // Ukuran minimum thumb agar mudah diklik

        // Posisi Y dari thumb
        size_t max_scroll_y = total_lines - max_vis;
        float scroll_ratio = (float)buf->scroll_y / (float)max_scroll_y;
        float thumb_y = track_y + (scroll_ratio * (track_h - thumb_h));

        Rectangle scrollbar_thumb = {scrollbar_x, thumb_y, scrollbar_w, thumb_h};
        Rectangle scrollbar_track = {scrollbar_x - 2.0f, track_y, scrollbar_w + 4.0f, track_h};

        Vector2 mouse_pos = GetMousePosition();
        bool is_hovered = CheckCollisionPointRec(mouse_pos, scrollbar_track);
        static bool is_dragging_scroll = false;
        static float drag_click_y = 0.0f;

        // Dragging & Interaction
        if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            is_dragging_scroll = true;
            buf->is_dragging = true;
            drag_click_y = mouse_pos.y - thumb_y;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            is_dragging_scroll = false;
            buf->is_dragging = false;
        }

        if (is_dragging_scroll) {
            float new_thumb_y = mouse_pos.y - drag_click_y;
            float new_scroll_ratio = (new_thumb_y - track_y) / (track_h - thumb_h);

            if (new_scroll_ratio < 0.0f) new_scroll_ratio = 0.0f;
            if (new_scroll_ratio > 1.0f) new_scroll_ratio = 1.0f;

            buf->scroll_y = (int)(new_scroll_ratio * max_scroll_y);
        }

        // Color Feedback (Lebih terang jika di-hover atau di-drag)
        Color thumb_color =
            (is_hovered || is_dragging_scroll) ? g_theme.cursor : g_theme.text_muted;
        thumb_color.a = (is_hovered || is_dragging_scroll) ? 180 : 100;  // Opacity

        // Draw Thumb Bar (Pojok membulat)
        DrawRectangleRounded(scrollbar_thumb, 0.4f, 4, thumb_color);
    }

    EndScissorMode();
}
