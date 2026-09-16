/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "lsp_server.h"
#include "lsp_ui.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

extern int calculate_score(const char *query, const char *label);  // Menghitung score (lsp_ui.c)
// Menghitung compare_scores (completion.c)
extern int compare_scores(const void *a, const void *b);
extern void lsp_clear_all_diagnostics(void);  // Clear Diagnostic [lsp_client.c]

// Helper internal
static float get_lsp_cursor_x(Font font, Buffer *buf, float text_x) {
    if (!buf) return text_x;
    char *line_text = Buffer_get_line_text(buf, buf->cursor.y);
    if (!line_text) return text_x;

    float current_x = text_x;
    float current_font_x = (float)font.baseSize;  // Dinamis mengikuti font
    float space_w = MeasureTextEx(font, " ", current_font_x, 1.0f).x;
    size_t target_col = buf->cursor.x;
    size_t len = strlen(line_text);
    if (target_col > len) target_col = len;

    int col_visual = 0;
    for (size_t i = 0; i < target_col; i++) {
        if (line_text[i] == '\t') {
            int spaces = 4 - (col_visual % 4);
            current_x += space_w * spaces;
            col_visual += spaces;
        } else {
            char ch[2] = {line_text[i], '\0'};
            current_x += MeasureTextEx(font, ch, current_font_x, 1.0f).x;
            col_visual++;
        }
    }

    free(line_text);
    return current_x;
}

/**
 * Fungsi untuk draw atau render Diagnostic [PUBLIC API]
 */
void draw_diagnostic_bar(BufManager *bufmgr, Font font) {
    EditorLayout Layout = get_editor_layout(bufmgr);

    int editor_x = Layout.editor_x;
    int editor_w = Layout.editor_w;

    int panel_y = Layout.win_h - STATUS_H - DIAG_PANEL_H;
    float current_font_x = (float)font.baseSize;

    // Geser X ke Layout.editor_x dan gunakan lebar Layout.editor_w
    DrawRectangle(editor_x, panel_y, editor_w, DIAG_PANEL_H, g_theme.bg_sidebar);
    DrawLine(editor_x, panel_y, editor_x + editor_w, panel_y, g_theme.line_num);

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) {
        Vector2 pos = {(float)(editor_x + PAD_X), (float)(panel_y + 4)};
        DrawTextEx(font, "No diagnostics", pos, current_font_x, 1.0f, g_theme.comment);
        return;
    }

    if (buf->path) {  // Jika ada buf dan buf->path, langsung assign ke Diagnostic
        char *uri = Path_to_uri(buf->path);
        buf->diagnostic = lsp_get_diagnostics(uri);
        free(uri);  // Jangan sampai lupa cokk
    } else {        // Kalau kosong ya kasih NULL aja HAHAHAHA
        buf->diagnostic = nullptr;
    }

    if (buf->diagnostic == nullptr || buf->diagnostic->count == 0) {
        if (buf->diagnostic) {  // Safety check biar ga ketimpa
            lsp_free_diagnostics(buf->diagnostic);
            buf->diagnostic = nullptr;
        }

        Vector2 pos = {(float)(editor_x + PAD_X), (float)(panel_y + 4)};
        DrawTextEx(font, "No diagnostics", pos, current_font_x, 1.0f, g_theme.comment);
        return;
    }

    DiagnosticItem *active_item = nullptr;
    for (size_t i = 0; i < buf->diagnostic->count; i++) {
        if ((size_t)buf->diagnostic->items[i].start_line == buf->cursor.y) {
            active_item = &buf->diagnostic->items[i];
            break;
        }
    }

    if (!active_item) active_item = &buf->diagnostic->items[0];

    Color color = (active_item->severity == 1) ? g_theme.keyword : g_theme.warning;

    char msg[512] = {0};
    snprintf(msg, sizeof(msg), "[Ln %d, Col %d] %s", active_item->start_line + 1,
             active_item->start_char + 1, active_item->message);

    Vector2 pos = {(float)(editor_x + PAD_X), (float)(panel_y + 4)};

    DrawTextEx(font, msg, pos, current_font_x, 1.0f, color);
}

/**
 * Fungsi untuk render completion [PUBLIC API]
 */
void render_lsp_completion_ui(BufManager *bufmgr, Font font) {
    // Kita buat flag untuk pengecekan jika lsp disable dan tidak visible
    bool flag =
        !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE) || !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);

    if (flag || !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) return;

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    EditorLayout Layout = get_editor_layout(bufmgr);

    char current_word[256] = {0};
    Buffer_get_current_word(buf, current_word, sizeof(current_word));

    FilteredItem filtered[256] = {0};
    int total_items = 0;

    // FILTER DAN HITUNG SKOR
    for (size_t i = 0; i < g_lsp_ui.completion.count && total_items < 256; i++) {
        const char *label = g_lsp_ui.completion.items[i].label;
        if (!label) continue;

        int score = calculate_score(current_word, label);
        if (score >= 0) {
            filtered[total_items].item = &g_lsp_ui.completion.items[i];
            filtered[total_items].score = score;
            total_items++;
        }
    }

    if (total_items == 0) return;

    // SORTING HANYA PADA ITEMS YANG TERFILTER
    if (current_word[0] != '\0') {
        qsort(filtered, total_items, sizeof(FilteredItem), compare_scores);
    }

    // HITUNG LEBAR DINAMIS (BACA DARI FILTERED)
    float current_font_x = (float)font.baseSize;
    float max_label_width = 150.0f;

    for (int i = 0; i < total_items; i++) {
        const char *label = filtered[i].item->label;
        if (label) {
            float text_w = MeasureTextEx(font, label, current_font_x, 1.0f).x;
            if (text_w > max_label_width) {
                max_label_width = text_w;
            }
        }
    }

    // Ekstrak ke Variable local
    int win_w = Layout.win_w;
    int win_h = Layout.win_h;
    int editor_y = Layout.editor_y;
    int text_screen_x = Layout.text_screen_x;

    float box_w = max_label_width + 24.0f;
    float max_box_w = win_w * 0.6f;
    if (box_w > max_box_w) box_w = max_box_w;

    // HITUNG POSISI KURSOR & KOTAK
    float cursor_screen_x = get_lsp_cursor_x(font, buf, (float)text_screen_x);
    float cursor_screen_y = editor_y + PAD_Y + ((int)buf->cursor.y - buf->scroll_y) * LINE_H;

    float item_h = current_font_x + 8.0f;
    int max_visible = 7;
    int display_count = total_items > max_visible ? max_visible : total_items;
    float box_h = (display_count * item_h) + 12.0f;

    int start_index = 0;
    if (g_lsp_ui.selected_index >= max_visible) {
        start_index = g_lsp_ui.selected_index - max_visible + 1;
    }

    float box_x = cursor_screen_x;
    float box_y;

    float min_x = (float)text_screen_x;
    if (box_x < min_x) {
        box_x = min_x;
    }

    // Penanda di atas atau bawah, agar Completion dan Signature tidak tumpang tindih
    bool place_below = true;
    if (cursor_screen_y + LINE_H + 2.0f + box_h > win_h - STATUS_H) {
        place_below = false;
    }

    // Jika di bawah maka set completion side di bawah
    if (place_below) {
        box_y = cursor_screen_y + LINE_H + 2.0f;
        g_lsp_ui.completion_side = POPUP_BELOW;
    } else {
        box_y = cursor_screen_y - box_h - 2.0f;
        g_lsp_ui.completion_side = POPUP_ABOVE;
    }

    if (box_x + box_w > win_w - 10.0f) {
        box_x = win_w - box_w - 10.0f;
    }

    Rectangle box = {box_x, box_y, box_w, box_h};

    // Render si Completion lsp
    DrawRectangleRounded(box, 0.05f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.05f, 4, g_theme.border);

    BeginScissorMode((int)box.x, (int)box.y, (int)box.width, (int)box.height);

    for (int i = 0; i < display_count; ++i) {
        int item_idx = start_index + i;
        if (item_idx >= total_items) break;

        const CompletionItem *it = filtered[item_idx].item;
        const char *label = it->label ? it->label : "(null)";

        // Hitung Y dasar tiap item slot
        float item_y = box.y + 6.0f + (i * item_h);

        // Render Background Active Item (Full height item_h biar inline & rapi)
        if (item_idx == g_lsp_ui.selected_index) {
            Rectangle item_bg = {box.x + 4, item_y, box.width - 8, item_h};
            DrawRectangleRounded(item_bg, 0.15f, 4, g_theme.border);
        }

        // Hitung posisi Y teks agar terpusat secara vertikal tepat di tengah background item_bg
        float text_y = item_y + (item_h - current_font_x) / 2.0f;

        DrawTextEx(font, label, (Vector2){box.x + 10, text_y}, current_font_x, 1.0f, WHITE);
    }

    EndScissorMode();
}

/**
 * Fungsi Render Signature Help [PUBLIC API]
 */
void render_signature_help(BufManager *bufmgr, Font font) {
    if (!HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG) || g_lsp_ui.signature_help.count == 0) return;

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    EditorLayout Layout = get_editor_layout(bufmgr);

    SignatureItem *sig = &g_lsp_ui.signature_help.items[g_lsp_ui.signature_help.active_signature];
    if (!sig || !sig->label) return;

    // PECAH STRING MENJADI 3 BAGIAN (PREFIX, ACTIVE PARAM, SUFFIX)
    char prefix_str[256] = {0};
    char param_str[256] = {0};
    char suffix_str[256] = {0};

    int target_param = sig->active_parameter;
    bool has_active_param = false;

    if (target_param >= 0 && (size_t)target_param < sig->parameter_count) {
        ParameterInfo *p = &sig->parameters[target_param];
        int label_len = (int)strlen(sig->label);

        if (p->start >= 0 && p->start < label_len) {
            int start_idx = p->start;
            int end_idx = p->end;

            if (end_idx <= start_idx || end_idx > label_len) {
                const char *comma = strchr(sig->label + start_idx, ',');
                const char *close_paren = strchr(sig->label + start_idx, ')');

                if (comma && close_paren) {
                    end_idx = (int)((comma < close_paren ? comma : close_paren) - sig->label);
                } else if (comma) {
                    end_idx = (int)(comma - sig->label);
                } else if (close_paren) {
                    end_idx = (int)(close_paren - sig->label);
                } else {
                    end_idx = label_len;
                }
            }

            // Potong Prefix
            strncpy(prefix_str, sig->label, start_idx);
            prefix_str[start_idx] = '\0';

            // Potong Active Parameter
            int param_len = end_idx - start_idx;
            if (param_len > 0) {
                strncpy(param_str, sig->label + start_idx, param_len);
                param_str[param_len] = '\0';
            }

            // Potong Suffix
            if (end_idx < label_len) {
                strncpy(suffix_str, sig->label + end_idx, sizeof(suffix_str) - 1);
            }

            has_active_param = true;
        }
    }

    if (!has_active_param) {
        snprintf(prefix_str, sizeof(prefix_str), "%s", sig->label);
    }

    float current_font_x = (float)font.baseSize;

    // HITUNG LEBAR PRESISI DARI 3 POTONGAN TEKS
    float prefix_w = MeasureTextEx(font, prefix_str, current_font_x, 1.0f).x;
    float param_w =
        has_active_param ? MeasureTextEx(font, param_str, current_font_x, 1.0f).x : 0.0f;
    float suffix_w =
        has_active_param ? MeasureTextEx(font, suffix_str, current_font_x, 1.0f).x : 0.0f;

    // TOTAL LEBAR & TINGGI DINAMIS KOTAK
    float total_text_w = prefix_w + param_w + suffix_w;
    float box_w = total_text_w + 20.0f;    // 10px padding kanan-kiri
    float box_h = current_font_x + 12.0f;  // Skala tinggi dinamis mengikuti font

    // KOREKSI POSISI POPUP (X & Y)
    float cursor_x = get_lsp_cursor_x(font, buf, (float)Layout.text_screen_x);
    float cursor_y = Layout.editor_y + PAD_Y + ((int)buf->cursor.y - buf->scroll_y) * LINE_H;

    float box_y;

    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
        if (g_lsp_ui.completion_side == POPUP_BELOW) {
            box_y = cursor_y - box_h - 4.0f;
            g_lsp_ui.signature_side = POPUP_ABOVE;

            if (box_y < TAB_H + 4.0f) {
                box_y = cursor_y + LINE_H + 4.0f;
                g_lsp_ui.signature_side = POPUP_BELOW;
            }
        } else {
            box_y = cursor_y + LINE_H + 4.0f;
            g_lsp_ui.signature_side = POPUP_BELOW;

            if (box_y + box_h > Layout.win_h - STATUS_H) {
                box_y = cursor_y - box_h - 4.0f;
                g_lsp_ui.signature_side = POPUP_ABOVE;
            }
        }
    } else {
        box_y = cursor_y - box_h - 4.0f;
        g_lsp_ui.signature_side = POPUP_ABOVE;

        if (box_y < TAB_H + 4.0f) {
            box_y = cursor_y + LINE_H + 4.0f;
            g_lsp_ui.signature_side = POPUP_BELOW;
        }
    }

    float box_x = cursor_x;

    if (box_x + box_w > Layout.win_w - 10.0f) {
        box_x = Layout.win_w - box_w - 10.0f;
    }

    float min_x = (float)Layout.text_screen_x;
    if (box_x < min_x) {
        box_x = min_x;
    }

    Rectangle box = {box_x, box_y, box_w, box_h};

    // RENDER BACKGROUND & BORDER
    DrawRectangleRounded(box, 0.15f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.15f, 4, g_theme.border);

    // KALKULASI VERTICAL CENTER UNTUK TEKS
    float start_text_x = box_x + 10.0f;
    float text_y = box_y + (box_h - current_font_x) / 2.0f;  // Center vertikal presisi

    // Render Prefix
    DrawTextEx(font, prefix_str, (Vector2){start_text_x, text_y}, current_font_x, 1.0f,
               g_theme.text_normal);

    if (has_active_param) {
        float active_x = start_text_x + prefix_w;

        // Pill background active parameter di-center terhadap text_y
        float pill_h = current_font_x + 2.0f;
        float pill_y = text_y - 1.0f;
        Rectangle param_bg = {active_x - 2.0f, pill_y, param_w + 4.0f, pill_h};
        DrawRectangleRounded(param_bg, 0.2f, 4, g_theme.active_line);

        // Render Active Parameter Text
        DrawTextEx(font, param_str, (Vector2){active_x, text_y}, current_font_x, 1.0f,
                   g_theme.cursor);

        // Render Suffix
        float suffix_x = active_x + param_w;
        DrawTextEx(font, suffix_str, (Vector2){suffix_x, text_y}, current_font_x, 1.0f,
                   g_theme.text_normal);
    }
}

/**
 * Fungsi untuk render Hovering [PUBLIC API]
 */
void render_hover_ui(BufManager *bufmgr, Font font) {
    if (!HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE) || !g_lsp_ui.hover.contents ||
        strlen(g_lsp_ui.hover.contents) == 0)
        return;

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    EditorLayout Layout = get_editor_layout(bufmgr);

    const char *text = g_lsp_ui.hover.contents;
    float current_font_x = (float)font.baseSize;

    // Posisi kursor di layar
    float cursor_x = get_lsp_cursor_x(font, buf, (float)Layout.text_screen_x);
    float cursor_y = Layout.editor_y + PAD_Y + ((int)buf->cursor.y - buf->scroll_y) * LINE_H;

    float max_box_w = Layout.win_w * 0.5f;
    if (max_box_w < 200.0f) max_box_w = 200.0f;

    float padding_x = 12.0f;
    float usable_w = max_box_w - (padding_x * 2.0f) - 10.0f;

    // Hitung baris visual untuk word wrap
    int total_visual_lines = 0;
    float max_measured_w = 0.0f;

    const char *p = text;
    while (*p) {
        const char *next = strchr(p, '\n');
        size_t len = next ? (size_t)(next - p) : strlen(p);

        char raw_line[1024] = {0};
        if (len >= sizeof(raw_line)) len = sizeof(raw_line) - 1;
        strncpy(raw_line, p, len);
        raw_line[len] = '\0';

        if (strncmp(raw_line, "```", 3) != 0) {
            float line_w = MeasureTextEx(font, raw_line, current_font_x, 1.0f).x;
            if (line_w <= usable_w) {
                if (line_w > max_measured_w) max_measured_w = line_w;
                total_visual_lines++;
            } else {
                // Word-wrap simulation
                char *temp_line = strdup(raw_line);  // Clone string dahulu
                char *word = strtok(temp_line, " ");
                char current_wrap[512] = {0};

                while (word) {
                    char test_buf[512] = {0};
                    if (strlen(current_wrap) > 0) {
                        snprintf(test_buf, sizeof(test_buf), "%s %s", current_wrap, word);
                    } else {
                        snprintf(test_buf, sizeof(test_buf), "%s", word);
                    }

                    if (MeasureTextEx(font, test_buf, current_font_x, 1.0f).x > usable_w) {
                        if (strlen(current_wrap) > 0) {
                            float w = MeasureTextEx(font, current_wrap, current_font_x, 1.0f).x;
                            if (w > max_measured_w) max_measured_w = w;
                            total_visual_lines++;
                            snprintf(current_wrap, sizeof(current_wrap), "%s", word);
                        } else {
                            total_visual_lines++;
                            current_wrap[0] = '\0';
                        }
                    } else {
                        snprintf(current_wrap, sizeof(current_wrap), "%s", test_buf);
                    }
                    word = strtok(nullptr, " ");
                }

                free(temp_line);  // Free clone string
                if (strlen(current_wrap) > 0) {
                    float w = MeasureTextEx(font, current_wrap, current_font_x, 1.0f).x;
                    if (w > max_measured_w) max_measured_w = w;
                    total_visual_lines++;
                }
            }
        }

        if (!next) break;
        p = next + 1;
    }

    if (total_visual_lines == 0) return;

    // DIMENSI POPUP
    float box_w = max_measured_w + (padding_x * 2.0f) + 10.0f;
    if (box_w > max_box_w) box_w = max_box_w;
    if (box_w < 160.0f) box_w = 160.0f;

    float line_h = current_font_x + 4.0f;
    float content_h = (total_visual_lines * line_h) + 16.0f;
    float max_box_h = Layout.win_h * 0.35f;
    float box_h = content_h > max_box_h ? max_box_h : content_h;

    // POSISI POPUP
    float x = cursor_x;
    float y = cursor_y + LINE_H + 4.0f;

    if (x + box_w > Layout.win_w - 10.0f) x = Layout.win_w - box_w - 10.0f;
    if (x < Layout.text_screen_x) x = Layout.text_screen_x;
    if (y + box_h > Layout.win_h - STATUS_H) y = cursor_y - box_h - 4.0f;

    Rectangle box = {x, y, box_w, box_h};

    // HANDLE SCROLL MOUSE
    Vector2 mouse_pos = GetMousePosition();
    if (CheckCollisionPointRec(mouse_pos, box)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            g_lsp_ui.hover_scroll -= wheel * 24.0f;
        }
    }

    // Clamp Scroll
    float max_scroll = content_h - box_h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (g_lsp_ui.hover_scroll < 0.0f) g_lsp_ui.hover_scroll = 0.0f;
    if (g_lsp_ui.hover_scroll > max_scroll) g_lsp_ui.hover_scroll = max_scroll;

    // Backgroud dan Border
    DrawRectangleRounded(box, 0.06f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.06f, 4, g_theme.border);

    // Render dengan clipping
    BeginScissorMode((int)box.x + 2, (int)box.y + 2, (int)box.width - 4, (int)box.height - 4);

    float ty = y + 8.0f - g_lsp_ui.hover_scroll;
    p = text;

    while (*p) {
        const char *next = strchr(p, '\n');
        size_t len = next ? (size_t)(next - p) : strlen(p);

        char raw_line[1024] = {0};
        if (len >= sizeof(raw_line)) len = sizeof(raw_line) - 1;
        strncpy(raw_line, p, len);
        raw_line[len] = '\0';

        if (strncmp(raw_line, "```", 3) != 0) {
            float line_w = MeasureTextEx(font, raw_line, current_font_x, 1.0f).x;
            float draw_usable = box_w - (padding_x * 2.0f) - 6.0f;

            if (line_w <= draw_usable) {
                if (ty + line_h >= y && ty <= y + box_h) {
                    DrawTextEx(font, raw_line, (Vector2){x + padding_x, ty}, current_font_x, 1.0f,
                               g_theme.text_normal);
                }
                ty += line_h;
            } else {
                // Render baris terbungkus
                char *temp_line = strdup(raw_line);
                char *word = strtok(temp_line, " ");
                char current_wrap[512] = {0};

                while (word) {
                    char test_buf[512] = {0};
                    if (strlen(current_wrap) > 0) {
                        snprintf(test_buf, sizeof(test_buf), "%s %s", current_wrap, word);
                    } else {
                        snprintf(test_buf, sizeof(test_buf), "%s", word);
                    }

                    if (MeasureTextEx(font, test_buf, current_font_x, 1.0f).x > draw_usable) {
                        if (strlen(current_wrap) > 0) {
                            if (ty + line_h >= y && ty <= y + box_h) {
                                DrawTextEx(font, current_wrap, (Vector2){x + padding_x, ty},
                                           current_font_x, 1.0f, g_theme.text_normal);
                            }
                            ty += line_h;
                            snprintf(current_wrap, sizeof(current_wrap), "%s", word);
                        } else {
                            if (ty + line_h >= y && ty <= y + box_h) {
                                DrawTextEx(font, word, (Vector2){x + padding_x, ty}, current_font_x,
                                           1.0f, g_theme.text_normal);
                            }
                            ty += line_h;
                            current_wrap[0] = '\0';
                        }
                    } else {
                        snprintf(current_wrap, sizeof(current_wrap), "%s", test_buf);
                    }
                    word = strtok(nullptr, " ");
                }

                // Free temporary clone string
                free(temp_line);

                if (strlen(current_wrap) > 0) {
                    if (ty + line_h >= y && ty <= y + box_h) {
                        DrawTextEx(font, current_wrap, (Vector2){x + padding_x, ty}, current_font_x,
                                   1.0f, g_theme.text_normal);
                    }
                    ty += line_h;
                }
            }
        }

        if (!next) break;
        p = next + 1;
    }

    EndScissorMode();

    // Mini Scroll bar
    if (max_scroll > 0.0f) {
        float scrollbar_h = (box_h / content_h) * (box_h - 8.0f);
        if (scrollbar_h < 12.0f) scrollbar_h = 12.0f;

        float scrollbar_y =
            box.y + 4.0f + (g_lsp_ui.hover_scroll / max_scroll) * (box_h - 8.0f - scrollbar_h);
        Rectangle scrollbar = {box.x + box.width - 6.0f, scrollbar_y, 4.0f, scrollbar_h};

        DrawRectangleRounded(scrollbar, 0.5f, 4, g_theme.border);
    }
}
