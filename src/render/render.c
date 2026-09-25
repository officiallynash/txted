/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "git_client.h"
#include "lsp.h"
#include "raylib.h"
#include "result.h"
#include "rope.h"
#include "syntax.h"
#include "theme.h"
#include "types.h"
#include "ui.h"

// Macro untuk mempercepat conditional if pakai XOR
// Untuk memudahkan walaupun sama tapi bitwise lebih efisien
#define XOR_CHECK(src, val) ((src ^ val) == 0)

extern void Buffer_clamp_scroll(Buffer *buf, int visible_lines);  // nav_utils.c

/**
 * Struct untuk bracket matching
 */
typedef struct {
    size_t x;
    size_t y;
    bool found;
} BracketMatch;

/**
 * Helper untuk hitung Column_x agar tidak boilerplate juga di export ke render lsp ui
 */
float advance_column_x(char c, int *col_visual, float glyph_w, float space_w) {
    if (XOR_CHECK(c, '\t')) {
        int spaces = 4 - (*col_visual & 3);
        *col_visual += spaces;
        return space_w * spaces;
    }

    (*col_visual)++;
    return glyph_w;
}

/**
 * Fungsi untuk expands Tab
 */
static size_t expand_tabs(const char *src, char *dst, size_t dst_size, int tab_size) {
    if (!src || !dst || dst_size == 0) return 0;
    if (tab_size <= 0) tab_size = 4;  // Fallback ke default jika tab_size invalid

    size_t j = 0;
    // Cadangkan 1 byte terakhir untuk null-terminator '\0'
    size_t max_j = dst_size - 1;

    // Agak boiler plate tapi ga papa, lagian sudah di kasih optimasi level compiler
    if (tab_size == 4) [[clang::likely]] {
        for (size_t i = 0; !XOR_CHECK(src[i], '\0') && j < max_j; i++) {
            char c = src[i];
            if (XOR_CHECK(c, '\t')) [[clang::unlikely]] {
                // Bitwise pengganti (4 - (j % 4)):
                // (j & 3) sama dengan (j % 4), jadi 4 - (j & 3)
                size_t spaces = 4 - (j & 3);

                // Clamp agar tidak overrun buffer
                if (j + spaces > max_j) spaces = max_j - j;

                // Memset jauh lebih kencang daripada while loop
                memset(dst + j, ' ', spaces);
                j += spaces;
            } else {
                dst[j++] = c;
            }
        }
    } else {
        // Fallback untuk tab_size kustom (misal tab_size = 2 atau 8)
        size_t ts = (tab_size <= 0) ? 4 : (size_t)tab_size;

        for (size_t i = 0; !XOR_CHECK(src[i], '\0') && j < max_j; i++) {
            char c = src[i];
            if (XOR_CHECK(c, '\t')) {
                size_t spaces = ts - (j & (ts - 1));
                if (j + spaces > max_j) spaces = max_j - j;
                memset(dst + j, ' ', spaces);
                j += spaces;
            } else {
                dst[j++] = c;
            }
        }
    }

    dst[j] = '\0';
    return j;
}

/**
 * Fungsi untuk mendapatkan warna token (Prefix matching untuk Tree-sitter)
 */
static inline Color Get_token_color(const char *capture_name) {
    if (!capture_name) return g_theme.text_normal;

    // Cek huruf pertama untuk mempercepat matching sebelum strncmp
    switch (capture_name[0]) {
        case 'k':
            if (strncmp(capture_name, "keyword", 7) == 0) return g_theme.keyword;
            break;
        case 't':
            if (strncmp(capture_name, "type", 4) == 0) return g_theme.type;
            break;
        case 's':
            if (strncmp(capture_name, "string", 6) == 0) return g_theme.string;
            break;
        case 'n':
            if (strncmp(capture_name, "number", 6) == 0) return g_theme.number;
            break;
        case 'f':
            if (strncmp(capture_name, "function", 8) == 0) return g_theme.function;
            if (strncmp(capture_name, "float", 5) == 0) return g_theme.number;
            break;
        case 'm':
            if (strncmp(capture_name, "method", 6) == 0) return g_theme.method;
            break;
        case 'c':
            if (strncmp(capture_name, "comment", 7) == 0) return g_theme.comment;
            if (strncmp(capture_name, "constant", 8) == 0) return g_theme.constant;
            break;
        case 'o':
            if (strncmp(capture_name, "operator", 8) == 0) return g_theme.operator;
            break;
    }
    return g_theme.text_normal;
}

/**
 * Fungsi utama untuk menggambar baris dengan highlight
 */
static void Draw_line_highlighted(Font font, const char *line_text, size_t len, Vector2 pos,
                                  HighlightToken *tokens, int token_count, size_t line_start_byte) {
    float current_x = pos.x;
    float current_font_size = (float)font.baseSize;

    // Alokasi awal agar ga terus2an panggil MeasureText
    float glyph_w = MeasureTextEx(font, "A", current_font_size, 1.0f).x;
    float space_w = MeasureTextEx(font, " ", current_font_size, 1.0f).x;
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

        // Tangani tab & spasi dengan advance column
        if (!XOR_CHECK(line_text[i], '\t')) {
            char chunk[2] = {line_text[i], '\0'};
            DrawTextEx(font, chunk, (Vector2){current_x, pos.y}, current_font_size, 1.0f, color);
        }

        current_x += advance_column_x(line_text[i], &col_visual, glyph_w, space_w);
    }
}

/**
 * Hitung posisi X piksel dari kolom berbasis teks (Sama persis seperti kursor)
 */
static float get_text_column_x(Font font, const char *text, size_t len, size_t target_col,
                               float start_x) {
    if (!text) return start_x;

    if (len > 0 && XOR_CHECK(text[len - 1], '\n')) len--;
    if (target_col > len) target_col = len;

    float current_x = start_x;
    float current_font_x = (float)font.baseSize;  // Pakai float dari fontsize Base

    // Alokasi awal si space_w dan glyph_w
    float space_w = MeasureTextEx(font, " ", current_font_x, 1.0f).x;
    float glyph_w = MeasureTextEx(font, "A", current_font_x, 1.0f).x;
    int col_visual = 0;

    for (size_t i = 0; i < target_col; i++) {
        current_x += advance_column_x(text[i], &col_visual, glyph_w, space_w);
    }

    return current_x;
}

/**
 * Fungsi untuk mencari Bracket
 */
static void find_matching_brackets(Buffer *buf, BracketMatch *b1, BracketMatch *b2) {
    b1->found = false;
    b2->found = false;

    // Ambil buffer yang lines
    char lines[1024] = {0};
    size_t line_len = Buffer_get_line_text(buf, buf->cursor.y, lines, sizeof(lines));
    if (line_len == 0) return;

    if (line_len > 0 && XOR_CHECK(lines[line_len - 1], '\n')) line_len--;

    // Cek posisi kursor saat ini DAN 1 posisi di sebelah kiri kursor
    size_t check_cols[2] = {buf->cursor.x, (buf->cursor.x > 0) ? buf->cursor.x - 1 : 0};
    size_t target_col = 0;
    char c = '\0';
    bool col_found = false;

    for (int i = 0; i < 2; i++) {
        size_t col = check_cols[i];
        if (col < line_len) {
            char ch = lines[col];
            if (XOR_CHECK(ch, '(') || XOR_CHECK(ch, '{') || XOR_CHECK(ch, '[') ||
                XOR_CHECK(ch, ')') || XOR_CHECK(ch, '}') || XOR_CHECK(ch, ']') ||
                XOR_CHECK(ch, '<') || XOR_CHECK(ch, '>')) {
                c = ch;
                target_col = col;
                col_found = true;
                break;
            }
        }
    }

    if (!col_found) return;

    b1->x = target_col;
    b1->y = buf->cursor.y;
    b1->found = true;

    /* ------------------------------------------------------------- *
     * PENCARIAN MAJU (OPENING BRACKET: (, {, [)
     * ------------------------------------------------------------- */
    if (XOR_CHECK(c, '(') || XOR_CHECK(c, '{') || XOR_CHECK(c, '[') || XOR_CHECK(c, '<')) {
        char match_c = XOR_CHECK(c, '(')   ? ')'
                       : XOR_CHECK(c, '{') ? '}'
                       : XOR_CHECK(c, '[') ? ']'
                                           : '>';

        int depth = 1;
        char l[1024] = {0};

        for (size_t y = buf->cursor.y; y < buf->lines.line_count; y++) {
            size_t l_len = Buffer_get_line_text(buf, y, l, sizeof(l));
            if (l_len == 0) continue;

            if (l_len > 0 && XOR_CHECK(l[l_len - 1], '\n')) l_len--;

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
                        return;
                    }
                }
            }
        }
    }
    /* ------------------------------------------------------------- *
     * PENCARIAN MUNDUR (CLOSING BRACKET: ), }, ])
     * ------------------------------------------------------------- */
    else if (XOR_CHECK(c, ')') || XOR_CHECK(c, '}') || XOR_CHECK(c, ']') || XOR_CHECK(c, '>')) {
        char match_c = XOR_CHECK(c, ')')   ? '('
                       : XOR_CHECK(c, '}') ? '{'
                       : XOR_CHECK(c, ']') ? '['
                                           : '<';
        int depth = 1;
        char l[1024] = {0};

        for (int y = (int)buf->cursor.y; y >= 0; y--) {
            size_t l_len = Buffer_get_line_text(buf, (size_t)y, l, sizeof(l));
            if (l_len == 0) continue;

            if (l_len > 0 && XOR_CHECK(l[l_len - 1], '\n')) l_len--;

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
                        return;
                    }
                }
            }
        }
    }
}

/**
 * Fungsi untuk Draw main Editor
 */
void draw_editor(BufManager *bufmgr, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);  // Active buffer
    if (!buf) return;

    float current_font_x = (float)font.baseSize;
    size_t rope_len = String_len(buf->str);
    EditorLayout Layout = get_editor_layout(bufmgr);  // Ambil layout

    int max_vis = Layout.visible_lines;
    Buffer_clamp_scroll(buf, max_vis);  // Clamp scroll_y dulu

    // Inisiasi Bracket matching
    BracketMatch b1;
    BracketMatch b2;
    find_matching_brackets(buf, &b1, &b2);

    int editor_x = Layout.editor_x;
    int editor_y = Layout.editor_y;
    int editor_h = Layout.editor_h;
    int editor_w = Layout.editor_w;
    int gutter_screen_x = Layout.gutter_screen_x;

    // Memamaksimalkan pemanggilan URI dan diagnostic sekali aja kali ya
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE) && buf->path) {
        // Diagnostic aktif ketika lsp aktif dan ada path-nya
        char *path_uri = Path_to_uri(buf->path);  // Path to Uri

        if (path_uri) buf->diagnostic = lsp_get_diagnostics(path_uri);
        free(path_uri);  // Langsung free
    }

    /* Buka Scissor cuma untuk area antara TAB dan STATUS BAR */
    BeginScissorMode(editor_x, editor_y, editor_w, editor_h);

    /* Fill the Editor*/
    DrawRectangle(editor_x, editor_y, editor_w, editor_h, g_theme.bg_editor);
    /* Background Gutter */
    DrawRectangle(gutter_screen_x, editor_y, GUTTER_W, editor_h, g_theme.bg_editor);

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

    if (buf->state && rope_len > 0) {
        uint32_t start_byte = (uint32_t)buf->lines.offset[first];
        uint32_t end_byte =
            (last < buf->lines.line_count) ? (uint32_t)buf->lines.offset[last] : (uint32_t)rope_len;

        Bytes full_text = String_get(buf->str, 0, rope_len);
        if (full_text.data) {
            token_count = Syntax_get_highlights(buf->state, (const char *)full_text.data,
                                                start_byte, end_byte, tokens, 1024);
            Bytes_free(&full_text);  // Mencegah memory leak
        }
    }

    /* ------------------------- *
     * Render Teks dan Sebagainya
     * ------------------------- */
    // State stack teks
    char text[1024] = {0};

    for (size_t y = first; y < last; y++) {
        int py = editor_y + PAD_Y + (int)(y - first) * LINE_H;

        bool is_search = HAS_FLAG(buf->buf_flags, BUF_IS_SEARCH);

        // Active line, selama flag is search ga aktif
        if (y == buf->cursor.y) [[clang::likely]] {
            // Background untuk hasil pencarian
            Color bg_active = is_search ? g_theme.cursor : g_theme.active_line;

            DrawRectangle(editor_x + GUTTER_W + 4, py, editor_w - GUTTER_W - 4, current_font_x + 2,
                          bg_active);
        }

        /* Line Number */
        char num_str[16] = {0};
        snprintf(num_str, sizeof(num_str), "%4zu", y + 1);
        Color num_color = (y == buf->cursor.y) ? g_theme.line_num : g_theme.text_muted;
        Vector2 num_pos = {(float)(editor_x + PAD_X - 8), (float)py};
        DrawTextEx(font, num_str, num_pos, current_font_x, 1.0f, num_color);

        /* Teks Editor */
        size_t len = Buffer_get_line_text(buf, y, text, sizeof(text));
        if (len == 0) continue;
        if (XOR_CHECK(text[len - 1], '\n')) text[len - 1] = '\0';
        if (XOR_CHECK(text[len - 1], '\r')) text[len - 1] = '\0';

        char expanded_text[2048] = {0};
        size_t expanded_len = expand_tabs(text, expanded_text, sizeof(expanded_text), 4);

        Vector2 pos = {(float)text_x, (float)py};

        /* ---------------- *
         * Highlight Selection
         * ---------------- */
        if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
            size_t sel_start, sel_len;

            // Ambil Get selected start dan actual pos
            Get_selected_position(buf, &sel_start, &sel_len);
            size_t sel_end = sel_start + sel_len;

            size_t line_start = buf->lines.offset[y];
            size_t line_end = (y + 1 < buf->lines.line_count) ? buf->lines.offset[y + 1] : rope_len;

            // Jika ada bagian baris ini yang masuk dalam seleksi
            if (sel_start < line_end && sel_end > line_start) {
                // Hitung indeks karakter relatif terhadap awal baris y
                size_t char_start = (sel_start > line_start) ? (sel_start - line_start) : 0;
                size_t char_end =
                    (sel_end < line_end) ? (sel_end - line_start) : (line_end - line_start);

                // Jika char_end mencakup '\n' di akhir baris, potong agar seleksi
                // tidak kebablasan
                if (len > 0 && XOR_CHECK(text[len - 1], '\n')) {
                    if (char_end > len - 1) char_end = len - 1;
                }

                // helper yang SAMA DENGAN KURSOR untuk menghitung X1 dan X2
                float x1 = get_text_column_x(font, text, len, char_start, (float)text_x);
                float x2 = get_text_column_x(font, text, len, char_end, (float)text_x);

                // Tambah ekstra lebar 8px jika seleksi mencakup newline (pindah baris)
                if (sel_end >= line_end && y + 1 < buf->lines.line_count) {
                    float space_w = MeasureTextEx(font, " ", current_font_x, 1.0f).x;
                    x2 += space_w;
                }

                if (x2 > x1) {
                    DrawRectangle((int)x1, py, (int)(x2 - x1), (int)current_font_x + 2,
                                  g_theme.selection);
                }
            }
        }

        /* ------------------------------------------------------------- *
         * RENDER GIT GUTTER INDICATOR BAR & GHOST TEXT
         * ------------------------------------------------------------- */
        if (git.is_repo) {  // Hanya render jika Path adalah repo
            if (buf->line_git && y < buf->meta_capacity) {
                LineGitMeta meta = buf->line_git[y];

                // Render Strip Warna di Samping Kiri Line Number
                if (meta.status != GUTTER_NONE) {
                    float gutter_bar_x = (float)(editor_x + 4);
                    Rectangle gutter_rect = {gutter_bar_x, (float)py, 3.0f, (float)LINE_H - 2.0f};

                    Color bar_color =
                        (meta.status == GUTTER_ADDED) ? g_theme.function : g_theme.warning;
                    DrawRectangleRounded(gutter_rect, 0.5f, 2, bar_color);
                }

                // Jika last_edited_at belum terisi oleh blame, gunakan waktu sekarang jika
                // baris dimodifikasi
                double time_to_show = meta.last_edited_at;
                if (time_to_show == 0 && meta.status != GUTTER_NONE) {
                    time_to_show = (double)time(nullptr);
                }

                if (time_to_show > 0) {
                    char ghost_str[64] = {0};
                    const char *who =
                        meta.author[0] ? meta.author : (git.author[0] ? git.author : "You");

                    format_time_ago(who, time_to_show, ghost_str, sizeof(ghost_str));

                    // Hitung X awal teks ghost (ujung kode + margin 32px)
                    float line_w = get_text_column_x(font, expanded_text, expanded_len,
                                                     expanded_len, (float)text_x);
                    float ghost_x = line_w + 32.0f;

                    // Hitung lebar teks ghost itu sendiri
                    float ghost_w = MeasureTextEx(font, ghost_str, current_font_x, 1.0f).x;

                    // Cek apakah X akhir ghost text masih muat di dalam batas kanan editor
                    // Beri sisa padding (misal 16px) biar gak terlalu mepet scrollbar/ujung
                    // layar dan hanya render ketika y == cursor.y
                    if ((ghost_x + ghost_w) < (float)(editor_x + editor_w - 16) &&
                        y == buf->cursor.y) {
                        DrawTextEx(font, ghost_str, (Vector2){ghost_x, (float)py}, current_font_x,
                                   1.0f, g_theme.text_muted);
                    }
                }
            }
        }

        /* ----------------------------- *
         * Bracket Coloring
         * ----------------------------- */
        if (b1.found && b2.found) {
            Color match_bg = (Color){255, 255, 255, 35};
            float space_w = MeasureTextEx(font, " ", current_font_x, 1.0f).x;

            // Cek Kurung Pertama (b1)
            if (y == b1.y) {
                float x1 = get_text_column_x(font, text, len, b1.x, (float)text_x);
                Rectangle r1 = {x1, (float)py, space_w, (float)LINE_H};
                DrawRectangleRounded(r1, 0.2f, 4, match_bg);
                DrawRectangleRoundedLines(r1, 0.2f, 4, g_theme.keyword);
            }

            // Cek Kurung Kedua (b2) - Dibuat 'if' terpisah agar kurung sebaris ter-render
            // dua-duanya
            if (y == b2.y) {
                float x2 = get_text_column_x(font, text, len, b2.x, (float)text_x);
                Rectangle r2 = {x2, (float)py, space_w, (float)LINE_H};
                DrawRectangleRounded(r2, 0.2f, 4, match_bg);
                DrawRectangleRoundedLines(r2, 0.2f, 4, g_theme.keyword);
            }
        }

        // Render teks dengan highlight [RENDER UTAMA]
        Draw_line_highlighted(font, text, len, pos, tokens, token_count, buf->lines.offset[y]);

        /* ------------------------------------------------------------- *
         * Render Squiggly / Underline Diagnostics
         * ------------------------------------------------------------- */
        if (buf->diagnostic && buf->diagnostic->count > 0) {
            for (size_t i = 0; i < buf->diagnostic->count; i++) {
                DiagnosticItem *item = &buf->diagnostic->items[i];

                // Cek apakah baris ini masuk dalam range diagnostik
                if ((size_t)item->start_line <= y && y <= (size_t)item->end_line) {
                    size_t col_start = (y == (size_t)item->start_line) ? item->start_char : 0;
                    size_t col_end = (y == (size_t)item->end_line) ? item->end_char : len;

                    // Jika range-nya 0 karakter, beri minimal 1 karakter agar garis
                    // kelihatan
                    if (col_start == col_end && col_start < len) {
                        col_end = col_start + 1;
                    }

                    float x1 = get_text_column_x(font, text, len, col_start, (float)text_x);
                    float x2 = get_text_column_x(font, text, len, col_end, (float)text_x);

                    // Tentukan warna garis sesuai severity
                    Color diag_color = g_theme.error;  // Fallback / Error (Severity 1)
                    if (item->severity == 2) {
                        diag_color = g_theme.warning;  // Warning
                    } else if (item->severity >= 3) {
                        // Info / Hint (Pakai warna g_theme yang sesuai)
                        diag_color = g_theme.info;
                    }

                    // Gambar garis bawah tipis tepat di bawah teks
                    int line_y = py + (int)current_font_x;  // + 1 aja kali ya biar ga ada jarak
                    int line_w = (int)(x2 - x1);
                    if (line_w <= 0) line_w = (int)MeasureTextEx(font, " ", current_font_x, 1.0f).x;

                    DrawRectangle((int)x1, line_y, line_w, 1, diag_color);
                }
            }
        }
    }

    /* ---------------- *
     * Cursor
     * ---------------- */
    if (bufmgr->mode == WRITE && buf->cursor.y >= first && buf->cursor.y < last) {
        char cur[1024] = {0};
        size_t clen = Buffer_get_line_text(buf, buf->cursor.y, cur, sizeof(cur));
        while (clen > 0 && (cur[clen - 1] == '\n' || cur[clen - 1] == '\r')) {
            cur[--clen] = '\0';
        }

        float cx_float = get_text_column_x(font, cur, clen, buf->cursor.x, (float)text_x);

        int cx = (int)cx_float;
        int cy = editor_y + PAD_Y + (int)(buf->cursor.y - first) * LINE_H;

        int cursor_w = 2;
        int cursor_h = (int)current_font_x + 2;

        if (((int)(GetTime() * 1.5f) & 1) == 0) {  // Setara dengan % 2
            DrawRectangle(cx, cy, cursor_w, cursor_h, g_theme.cursor);
        }
    }

    /* ------------------------------------------------------------- *
     * RENDER VERTICAL SCROLLBAR (Right Edge)
     * ------------------------------------------------------------- */
    size_t total_lines = buf->lines.line_count;
    if (total_lines > (size_t)max_vis) {
        float scrollbar_w = 8.0f;
        float scrollbar_x = (float)(editor_x + editor_w) - scrollbar_w - 2.0f;
        float track_h = (float)editor_h;
        float track_y = (float)editor_y;

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
            SET_FLAG(buf->buf_flags, BUF_IS_DRAGGING);
            drag_click_y = mouse_pos.y - thumb_y;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            is_dragging_scroll = false;
            CLR_FLAG(buf->buf_flags, BUF_IS_DRAGGING);
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
    // Render Border Indikator Focus Mode (Write Focus)
    if (bufmgr->mode == WRITE) {
        Rectangle editor_rect = {(float)editor_x, (float)editor_y, (float)editor_w,
                                 (float)editor_h};
        DrawRectangleLinesEx(editor_rect, 1.5f, g_theme.cursor);  // Border warna cursor (terang)
    }
}
