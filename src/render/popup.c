/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#define RAYGUI_IMPLEMENTATION
#include <raygui.h>
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "fs.h"
#include "result.h"
#include "rope.h"
#include "theme.h"
#include "types.h"
#include "ui.h"

// Macro untuk hapus local list
#define CLEANUP_LOCAL_LIST()               \
    do {                                   \
        if (open_file_list) {              \
            FileList_free(open_file_list); \
            open_file_list = nullptr;      \
        }                                  \
        last_q[0] = '\0';                  \
        search_items_count = 0;            \
    } while (0)

// Pakai constexpr agar lebih ramah size pada saat compile time
// Ini jauh lebih typesafe daripada sekedar define
constexpr size_t MAX_SEARCH_HIT = 100;

// Calculate score for fuzzy matching (LSP_UI)
extern int calculate_score(const char *query, const char *label);
// Nanti di eksekusi di navigation.c
extern void FloatPrompt_execute(BufManager *bufmgr, char *text);
void Buffer_goto_search_hit(BufManager *bufmgr, const SearchHitBuffer *hit);

/**
 * PromptBuffer init [PUBLIC API]
 */
PromptBuffer *PromptBuffer_init(void) {
    // Pakai calloc agar lebih aman HEHEHEHE
    PromptBuffer *prb = calloc(1, sizeof(PromptBuffer));
    if (!prb) return nullptr;

    prb->str = String_new();
    prb->cursor_pos = 0;
    prb->len = 0;

    return prb;
}

/**
 * Fungsi untuk menambahkan Teks [PUBLIC API]
 */
void PromptBuffer_insert(PromptBuffer *prb, size_t pos_idx, const char *ch) {
    size_t len = strlen(ch);
    if (!prb || len == 0) return;
    String_insert(&prb->str, pos_idx, ch, len);
    prb->cursor_pos++;
    prb->len++;
}

/**
 * Fungsi untuk menghapus Teks [PUBLIC API]
 */
void PromptBuffer_delete(PromptBuffer *prb, size_t pos_idx) {
    if (!prb) return;
    String_delete(&prb->str, pos_idx, 1);
    prb->cursor_pos--;
    prb->len--;
}

/**
 * Fungsi untuk GET DATA [PUBLIC API]
 */
size_t PromptBuffer_get(PromptBuffer *prb, char *outbuf, size_t out_buf_size) {
    if (!prb || !outbuf || out_buf_size == 0) return 0;

    Bytes data = String_get(prb->str, 0, prb->len);
    if (data.data) {
        size_t copy_len = (data.len < out_buf_size - 1) ? data.len : out_buf_size - 1;
        memcpy(outbuf, data.data, copy_len);  // Cukup copy ke out buffer, anti malloc
        outbuf[copy_len] = '\0';

        Bytes_free(&data);
        return copy_len;  // Kembalikan copy len
    }
    outbuf[0] = '\0';
    return 0;
}

/**
 * Fungsi untuk set ke default si Prompt Buffer kalau untuk prb nanti di free manual [PUBLIC API]
 */
void PromptBuffer_destroy(BufManager *bufmgr) {
    if (!bufmgr || !bufmgr->prompt || !bufmgr->prompt->prb) return;

    PromptBuffer *prb = bufmgr->prompt->prb;
    if (prb->str != nullptr) {
        String_release(prb->str);
        prb->str = nullptr;
    }

    free(prb);
    bufmgr->prompt->prb = nullptr;
}

/**
 * Fungsi untuk memotong teks [PRIVATE API]
 */
static const char *TruncateText(Font font, const char *text, float max_width, float font_size) {
    static char buf[256] = {0};
    float width = MeasureTextEx(font, text, font_size, 1.0f).x;
    if (width <= max_width) return text;  // Aman, gak usah dipotong

    size_t len = strlen(text);
    if (len < 7) return text;

    // Ambil beberapa karakter di depan dan di belakang
    snprintf(buf, sizeof(buf), "...%s", text + (len - 35));  // Tampilkan 35 char terakhir
    return buf;
}

/**
 * Render dan update FloatPrompt [PRIVATE API]
 * Layout modal, tinggi item suggestion, dan clipping text disinkronkan penuh dengan ukuran font
 * dinamis.
 */
void draw_prompt_ui(BufManager *bufmgr, Font font) {
    if (!bufmgr->prompt->is_active) return;

    float current_font_size = (float)font.baseSize;
    GuiSetFont(font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)current_font_size);

    PromptBuffer *prb = bufmgr->prompt->prb;
    if (!prb) return;

    const int MAX_MATCHES = 100;
    int matches[MAX_MATCHES];
    size_t match_count = 0;
    int max_visible = 5;

    PromptItem *items = nullptr;
    size_t items_count = 0;

    // SAFE GET TEXT no malloc club HAHA
    char current_text[512] = {0};
    size_t current_len = PromptBuffer_get(prb, current_text, sizeof(current_text));
    (void)current_len;

    // =========================================================================
    // Item untuk Search dan Open dan Hitung Matches
    // =========================================================================
    static FileList *open_file_list = NULL;
    static PromptItem search_items[MAX_SEARCH_HIT];
    static SearchHitBuffer hits[MAX_SEARCH_HIT];
    static size_t search_items_count = 0;  // STABLE COUNT ANTAR FRAME
    static char last_q[256] = "\0";

    if (bufmgr->prompt->type == PROMPT_TYPE_OPEN_FILE) {
        if (!open_file_list) {
            open_file_list = FileList_init(128);
            Scan_project_files(".", open_file_list);
        } else {
            items = open_file_list->items;
            items_count = open_file_list->item_count;
        }
    } else if (bufmgr->prompt->type == PROMPT_TYPE_SEARCH) {
        Buffer *buf = BufManager_getactive(bufmgr);

        // Karena di search akan lebih bijak kita kasih flag likely, karena mostly ada hasilnya
        if (strcmp(last_q, current_text) != 0) [[clang::likely]] {
            snprintf(last_q, sizeof(last_q), "%s", current_text);

            int n = 0;
            if (prb->len > 0) {
                n = Buffer_search(buf, current_text, hits, MAX_SEARCH_HIT);
            }

            for (int i = 0; i < n; i++) {
                snprintf(search_items[i].label, sizeof(search_items[i].label), "%s", hits[i].label);
                search_items[i].icon_id = ICON_FILETYPE_TEXT;
            }

            search_items_count = (size_t)n;
            bufmgr->prompt->selected_idx = 0;
            bufmgr->prompt->scroll_offset = 0;
        }

        items = search_items;
        items_count = search_items_count;
    }

    // Filter matching
    if (items != nullptr && items_count > 0) {
        for (size_t i = 0; i < items_count; i++) {
            if (current_text[0] == '\0' || calculate_score(current_text, items[i].label) > 0)
                [[clang::likely]] {
                if (match_count < MAX_MATCHES) {
                    matches[match_count++] = (int)i;
                } else {
                    break;
                }
            }
        }
    }

    int max_idx = (match_count > 0) ? (int)match_count - 1 : 0;

    // =========================================================================
    // Layout Modal
    // =========================================================================
    EditorLayout layout = get_editor_layout(bufmgr);

    float input_h = current_font_size + 16.0f;
    float item_h = current_font_size + 12.0f;
    float base_h = input_h + current_font_size + 36.0f;

    float box_w = 460.0f;
    int visible_items = (match_count > (size_t)max_visible) ? max_visible : (int)match_count;
    float suggestions_h = visible_items * item_h;

    float box_h = base_h + (match_count > 0 ? suggestions_h + 10.0f : 0.0f);
    Rectangle modal_rect = {(layout.win_w - box_w) / 2.0f, (layout.win_h - box_h) / 3.0f, box_w,
                            box_h};

    // Input Handling Keyboard
    bool enter_pressed = false;

    if (bufmgr->prompt->edit_mode) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            enter_pressed = true;
        }

        if (IsKeyPressed(KEY_BACKSPACE) && prb->cursor_pos > 0) {
            PromptBuffer_delete(prb, prb->cursor_pos - 1);
        }

        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 126) {
                char ch[2] = {(char)key, '\0'};
                PromptBuffer_insert(prb, prb->cursor_pos, ch);
            }
            key = GetCharPressed();
        }
    }

    // Overlay & Modal BG
    DrawRectangle(0, 0, layout.win_w, layout.win_h, (Color){0, 0, 0, 100});
    DrawRectangleRounded(modal_rect, 0.1f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(modal_rect, 0.1f, 4, g_theme.border);

    Vector2 label_pos = {modal_rect.x + 12.0f, modal_rect.y + 10.0f};
    DrawTextEx(font, bufmgr->prompt->label, label_pos, current_font_size, 1.0f,
               g_theme.text_normal);

    Rectangle input_rect = {modal_rect.x + 12.0f, modal_rect.y + 14.0f + current_font_size,
                            modal_rect.width - 24.0f, input_h};

    float iconSize = 20.0f;
    float padding = 6.0f;
    float clearBtnW = 28.0f;
    Rectangle bounds = input_rect;
    Rectangle textBounds = {bounds.x + iconSize + padding * 2, bounds.y,
                            bounds.width - iconSize - clearBtnW - padding * 3, bounds.height};

    DrawRectangleRounded(bounds, 0.15f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(bounds, 0.15f, 4, g_theme.border);

    if (bufmgr->prompt->icon_id >= 0) {
        GuiDrawIcon(bufmgr->prompt->icon_id, (int)(bounds.x + padding),
                    (int)(bounds.y + (bounds.height - iconSize) / 2.0f), 1, g_theme.text_normal);
    }

    // Render Text Input
    float text_y = bounds.y + (bounds.height - current_font_size) / 2.0f;
    DrawTextEx(font, current_text, (Vector2){textBounds.x + 2.0f, text_y}, current_font_size, 1.0f,
               g_theme.text_normal);

    // =========================================================================
    // Rendering Suggestion Jika ada yang Match
    // =========================================================================
    if (match_count > 0) {
        float start_y = input_rect.y + input_rect.height + 8.0f;

        for (int i = 0; i < visible_items; i++) {
            int item_idx = bufmgr->prompt->scroll_offset + i;
            if (item_idx >= (int)match_count) break;

            Rectangle item_rect = {modal_rect.x + 12.0f, start_y + (i * item_h),
                                   modal_rect.width - 24.0f, item_h - 2.0f};
            PromptItem *item = &items[matches[item_idx]];

            if (CheckCollisionPointRec(GetMousePosition(), item_rect)) [[clang::unlikely]] {
                bufmgr->prompt->selected_idx = item_idx;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) enter_pressed = true;
            }

            bool is_selected = (item_idx == bufmgr->prompt->selected_idx);
            Color bg_item = is_selected ? g_theme.selection : g_theme.bg_card;

            DrawRectangleRounded(item_rect, 0.15f, 4, bg_item);

            int item_icon = (item->icon_id >= 0) ? item->icon_id : ICON_FILETYPE_TEXT;
            GuiDrawIcon(item_icon, (int)(item_rect.x + 6.0f),
                        (int)(item_rect.y + (item_rect.height - 20.0f) / 2.0f), 1,
                        g_theme.text_normal);

            float text_x = item_rect.x + 30.0f;
            float text_y_item = item_rect.y + (item_rect.height - current_font_size) / 2.0f;
            float max_text_width = item_rect.width - 36.0f;

            BeginScissorMode((int)text_x, (int)item_rect.y, (int)max_text_width,
                             (int)item_rect.height);
            const char *display_text =
                TruncateText(font, item->label, max_text_width, current_font_size);
            DrawTextEx(font, display_text, (Vector2){text_x, text_y_item}, current_font_size, 1.0f,
                       g_theme.text_normal);
            EndScissorMode();
        }
    }

    // =========================================================================
    // Handling untuk keyboard dan Mouse
    // =========================================================================
    if (enter_pressed) {
        bool condition = match_count > 0 && bufmgr->prompt->selected_idx < (int)match_count;
        if (bufmgr->prompt->type == PROMPT_TYPE_OPEN_FILE && condition) {
            int original_idx = matches[bufmgr->prompt->selected_idx];
            char *result = strdup(items[original_idx].label);

            CLEANUP_LOCAL_LIST();
            PromptBuffer_destroy(bufmgr);
            FloatPrompt_execute(bufmgr, result);

        } else if (bufmgr->prompt->type == PROMPT_TYPE_SEARCH && condition) {
            int original_hit_idx = matches[bufmgr->prompt->selected_idx];

            // Ambil data hit sebelum prompt dibersihkan
            SearchHitBuffer target_hit = hits[original_hit_idx];

            CLEANUP_LOCAL_LIST();
            PromptBuffer_destroy(bufmgr);

            // Panggil lompat ke baris pencarian
            Buffer_goto_search_hit(bufmgr, &target_hit);

        } else {
            char *exec_text = strdup(current_text);

            CLEANUP_LOCAL_LIST();
            PromptBuffer_destroy(bufmgr);
            FloatPrompt_execute(bufmgr, exec_text);
        }

        return;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        CLEANUP_LOCAL_LIST();
        PromptBuffer_destroy(bufmgr);
        bufmgr->prompt->is_active = false;
        bufmgr->prompt->edit_mode = false;
        bufmgr->mode = WRITE;

        return;
    }

    // Navigasi Keyboard
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        if (bufmgr->prompt->selected_idx < max_idx) [[clang::likely]] {
            bufmgr->prompt->selected_idx++;
            if (bufmgr->prompt->selected_idx >= bufmgr->prompt->scroll_offset + max_visible) {
                bufmgr->prompt->scroll_offset++;
            }
        }
    }

    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        if (prb->len > 0 && prb->cursor_pos > 0) prb->cursor_pos--;
    }

    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        if (prb->cursor_pos < prb->len) prb->cursor_pos++;
    }

    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        if (bufmgr->prompt->selected_idx > 0) [[clang::likely]] {
            bufmgr->prompt->selected_idx--;
            if (bufmgr->prompt->selected_idx < bufmgr->prompt->scroll_offset) {
                bufmgr->prompt->scroll_offset--;
            }
        }
    }

    float mouse_wheel = GetMouseWheelMove();
    if (mouse_wheel != 0) {
        if (mouse_wheel < 0 && bufmgr->prompt->scroll_offset + max_visible < (int)match_count) {
            bufmgr->prompt->scroll_offset++;
        } else if (mouse_wheel > 0 && bufmgr->prompt->scroll_offset > 0) {
            bufmgr->prompt->scroll_offset--;
        }
    }

    if (bufmgr->prompt->selected_idx > max_idx) bufmgr->prompt->selected_idx = max_idx;

    // Render Caret/Cursor
    if (((int)(GetTime() * 1.5f) % 2) == 0) {
        char temp_buf[512] = {0};
        int cp = prb->cursor_pos;

        if (cp > (int)strlen(current_text)) {
            cp = (int)strlen(current_text);
        }

        strncpy(temp_buf, current_text, cp);
        temp_buf[cp] = '\0';

        float text_width_to_cursor = MeasureTextEx(font, temp_buf, current_font_size, 1.0f).x;
        float caret_x = textBounds.x + 2.0f + text_width_to_cursor;
        float caret_h = current_font_size;
        float caret_y = bounds.y + (bounds.height - caret_h) / 2.0f;

        DrawRectangleRec((Rectangle){caret_x, caret_y, 2.0f, caret_h}, g_theme.cursor);
    }
}

/**
 * Fungsi untuk melompat ke Hasil pencarian [PUBLIC API]
 */
void Buffer_goto_search_hit(BufManager *bufmgr, const SearchHitBuffer *hit) {
    // Kita taruh goto ke lines disini ya
    // Karena untuk mengakomodasi Layout visible lines.
    // Jika masih di buffer.c maka akan bentrok dengan Buffer.h
    // Saling cross include si Header bahaya selain itu
    // untuk menjaga kesucian buffer.c dari BufManager

    Buffer *buf = BufManager_getactive(bufmgr);
    EditorLayout layout = get_editor_layout(bufmgr);  // ambil layout

    if (!buf || !hit) return;

    buf->cursor.y = hit->line;
    buf->cursor.x = hit->col;
    SET_FLAG(buf->buf_flags, BUF_IS_SEARCH);  // Set ke BUF IS SEARCH

    if (hit->line < buf->lines.line_count) {
        buf->cursor.cursor_pos = buf->lines.offset[hit->line] + hit->col;
    } else {
        buf->cursor.cursor_pos = String_len(buf->str);
    }

    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);  // Memastikan bahwa is_selected mati!
    bufmgr->mode = WRITE;

    // scroll biar kelihatan
    int vis = layout.visible_lines;

    // Clamp scroll_y
    if ((int)buf->cursor.y < buf->scroll_y) buf->scroll_y = (int)buf->cursor.y;
    if ((int)buf->cursor.y >= buf->scroll_y + vis) buf->scroll_y = (int)buf->cursor.y - vis + 1;
}
