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
#include <string.h>
#include <sys/stat.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

// Pakai constexpr agar lebih ramah size pada saat compile time
// Ini jauh lebih typesafe daripada sekedar define
constexpr size_t MAX_SEARCH_HIT = 100;

FloatPrompt g_prompt = {};  // Deklarasi awal g_prompt nantinya buat di extern
extern void render_all_ui(BufManager *bufmgr, Font font);  // Didefinisikan di main.c
extern int calculate_score(const char *query,
                           const char *label);  // Calculate score for fuzzy matching (LSP_UI)

/* ================================
 * PRIVATE API
 * ================================ */

/**
 * Fungsi untuk menambahkan karakter [PRIVATE API]
 */
static void append_char(char *text, size_t textSize, int ch) {
    size_t len = strlen(text);
    if (len + 1 >= textSize) return;
    text[len] = (char)ch;
    text[len + 1] = '\0';
}

/**
 * Fungsi untuk menghapus karakter terakhir [PRIVATE API]
 */
static void remove_last_char(char *text) {
    size_t len = strlen(text);
    if (len > 0) text[len - 1] = '\0';
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
 * Helper functions untuk open FloatPrompt [PRIVATE API]
 */
static void FloatPrompt_open(FloatPrompt *fp, const char *msg, const char *default_val,
                             int icon_id) {
    GetKeyPressed();

    fp->is_active = true;
    fp->edit_mode = true;
    fp->icon_id = icon_id;

    snprintf(fp->label, sizeof(fp->label), "%s", msg);

    if (default_val && strlen(default_val) > 0) {
        snprintf(fp->input_buf, sizeof(fp->input_buf), "%s", default_val);
    } else {
        fp->input_buf[0] = '\0';
    }
}

/**
 * Custom input box dengan icon dan clear button [PRIVATE API]
 * Disesuaikan agar caret & kalkulasi vertikal fleksibel terhadap ukuran font.
 */
bool GuiCustomInputBox(Rectangle bounds, char *text, int textSize, bool *editMode, int iconId,
                       Font font) {
    float iconSize = 20.0f;
    float padding = 6.0f;
    float clearBtnW = 28.0f;
    float current_font_size = (float)font.baseSize;

    Rectangle textBounds = {bounds.x + iconSize + padding * 2, bounds.y,
                            bounds.width - iconSize - clearBtnW - padding * 3, bounds.height};

    Rectangle clearBounds = {bounds.x + bounds.width - clearBtnW - 4, bounds.y + 4, clearBtnW - 4,
                             bounds.height - 8};

    DrawRectangleRounded(bounds, 0.15f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(bounds, 0.15f, 4, g_theme.border);

    if (iconId >= 0) {
        GuiDrawIcon(iconId, (int)(bounds.x + padding),
                    (int)(bounds.y + (bounds.height - iconSize) / 2.0f), 1, g_theme.text_normal);
    }

    bool enterPressed = false;

    if (*editMode) {
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            enterPressed = true;
        }

        if (IsKeyPressed(KEY_BACKSPACE)) {
            remove_last_char(text);
        }

        int key = GetCharPressed();
        while (key > 0) {
            if (key >= 32 && key <= 126) {
                append_char(text, (size_t)textSize, key);
            }
            key = GetCharPressed();
        }
    }

    // Y offset dan Caret disesuaikan menggunakan font.baseSize dinamis
    float text_y = bounds.y + (bounds.height - current_font_size) / 2.0f;

    DrawTextEx(font, text, (Vector2){textBounds.x + 2.0f, text_y}, current_font_size, 1.0f,
               g_theme.text_normal);

    if (*editMode && ((int)(GetTime() * 1.5f) % 2) == 0) {
        float text_width = MeasureTextEx(font, text, current_font_size, 1.0f).x;
        float caret_x = textBounds.x + 2.0f + text_width;
        float caret_h = current_font_size;
        float caret_y = bounds.y + (bounds.height - caret_h) / 2.0f;

        DrawRectangleRec((Rectangle){caret_x, caret_y, 2.0f, caret_h}, g_theme.cursor);
    }

    if (text[0] != '\0') {
        if (GuiButton(clearBounds, GuiIconText(ICON_CROSS, ""))) {
            text[0] = '\0';
            *editMode = true;
        }
    }

    return enterPressed;
}

/**
 * Render dan update FloatPrompt [PRIVATE API]
 * Layout modal, tinggi item suggestion, dan clipping text disinkronkan penuh dengan ukuran font
 * dinamis.
 */
char *FloatPrompt_update_and_render(BufManager *bufmgr, FloatPrompt *fp, Font font) {
    if (!fp->is_active) return nullptr;

    float current_font_size = (float)font.baseSize;

    GuiSetFont(font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)current_font_size);

    if (IsKeyPressed(KEY_ESCAPE)) {
        fp->is_active = false;
        fp->edit_mode = false;
        return nullptr;
    }

    // Hitung fuzzy matching (Jika ada items)
    const int MAX_MATCHES = 100;
    int matches[MAX_MATCHES];
    size_t match_count = 0;

    if (fp->items != nullptr && fp->item_count > 0) {
        for (size_t i = 0; i < fp->item_count; i++) {
            if (calculate_score(fp->input_buf, fp->items[i].label) > 0) {
                matches[match_count++] = (int)i;
                if (match_count >= MAX_MATCHES) break;
            }
        }
    }

    int max_visible = 5;
    int max_idx = (match_count > 0) ? (int)match_count - 1 : 0;

    static char list_buf[256] = "";
    if (strcmp(list_buf, fp->input_buf) != 0) {
        fp->selected_idx = 0;
        fp->scroll_offset = 0;
        snprintf(list_buf, sizeof(list_buf), "%s", fp->input_buf);
    }

    // Navigasi Keyboard (Atas/Bawah)
    if (IsKeyPressed(KEY_DOWN)) {
        if (fp->selected_idx < max_idx) {
            fp->selected_idx++;
            if (fp->selected_idx >= fp->scroll_offset + max_visible) {
                fp->scroll_offset++;
            }
        }
    }

    if (IsKeyPressed(KEY_UP)) {
        if (fp->selected_idx > 0) {
            fp->selected_idx--;
            if (fp->selected_idx < fp->scroll_offset) {
                fp->scroll_offset--;
            }
        }
    }

    float mouse_wheel = GetMouseWheelMove();
    if (mouse_wheel != 0) {
        if (mouse_wheel < 0 && fp->scroll_offset + max_visible < (int)match_count) {
            fp->scroll_offset++;
        } else if (mouse_wheel > 0 && fp->scroll_offset > 0) {
            fp->scroll_offset--;
        }
    }

    if (fp->selected_idx > max_idx) fp->selected_idx = max_idx;

    EditorLayout layout = get_editor_layout(bufmgr);

    // Dimensi dinamis: Input box & suggestion item menyesuaikan ukuran font
    float input_h = current_font_size + 16.0f;  // Dynamic height untuk input box
    float item_h = current_font_size + 12.0f;   // Dynamic height untuk item list
    float base_h = input_h + current_font_size + 36.0f;

    float box_w = 460.0f;
    int visible_items = (match_count > (size_t)max_visible) ? max_visible : (int)match_count;
    float suggestions_h = visible_items * item_h;

    float box_h = base_h + (match_count > 0 ? suggestions_h + 10.0f : 0.0f);
    Rectangle modal_rect = {(layout.win_w - box_w) / 2.0f, (layout.win_h - box_h) / 3.0f, box_w,
                            box_h};

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse_pos = GetMousePosition();
        if (!CheckCollisionPointRec(mouse_pos, modal_rect)) {
            fp->edit_mode = true;
        }
    }

    DrawRectangle(0, 0, layout.win_w, layout.win_h, (Color){0, 0, 0, 100});

    DrawRectangleRounded(modal_rect, 0.1f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(modal_rect, 0.1f, 4, g_theme.border);

    Vector2 label_pos = {modal_rect.x + 12.0f, modal_rect.y + 10.0f};
    DrawTextEx(font, fp->label, label_pos, current_font_size, 1.0f, g_theme.text_normal);

    Rectangle input_rect = {modal_rect.x + 12.0f, modal_rect.y + 14.0f + current_font_size,
                            modal_rect.width - 24.0f, input_h};

    bool enter_key_pressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    bool enter_pressed = GuiCustomInputBox(input_rect, fp->input_buf, sizeof(fp->input_buf),
                                           &fp->edit_mode, fp->icon_id, font);

    // Render suggestion jika ada yang match
    if (match_count > 0) {
        float start_y = input_rect.y + input_rect.height + 8.0f;

        for (int i = 0; i < visible_items; i++) {
            int item_idx = fp->scroll_offset + i;
            if (item_idx >= (int)match_count) break;

            Rectangle item_rect = {modal_rect.x + 12.0f, start_y + (i * item_h),
                                   modal_rect.width - 24.0f, item_h - 2.0f};
            PromptItem *item = &fp->items[matches[item_idx]];

            // Check Hover Mouse
            if (CheckCollisionPointRec(GetMousePosition(), item_rect)) {
                fp->selected_idx = item_idx;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) enter_key_pressed = true;
            }

            bool is_selected = (item_idx == fp->selected_idx);
            Color bg_item = is_selected ? g_theme.selection : g_theme.bg_card;

            DrawRectangleRounded(item_rect, 0.15f, 4, bg_item);

            // Icon Item
            int item_icon = (item->icon_id >= 0) ? item->icon_id : ICON_FILETYPE_TEXT;
            GuiDrawIcon(item_icon, (int)(item_rect.x + 6.0f),
                        (int)(item_rect.y + (item_rect.height - 20.0f) / 2.0f), 1,
                        g_theme.text_normal);

            // Draw Suggestion text dengan posisi Y tersinkronisasi
            float text_x = item_rect.x + 30.0f;
            float text_y = item_rect.y + (item_rect.height - current_font_size) / 2.0f;
            float max_text_width = item_rect.width - 36.0f;

            BeginScissorMode((int)text_x, (int)item_rect.y, (int)max_text_width,
                             (int)item_rect.height);
            const char *display_text =
                TruncateText(font, item->label, max_text_width, current_font_size);

            DrawTextEx(font, display_text, (Vector2){text_x, text_y}, current_font_size, 1.0f,
                       g_theme.text_normal);
            EndScissorMode();
        }
    }

    // Logika return dan execute
    if ((enter_pressed && enter_key_pressed) || enter_key_pressed) {
        fp->is_active = false;
        fp->edit_mode = false;

        if (match_count > 0 && fp->selected_idx < (int)match_count) {
            int original_idx = matches[fp->selected_idx];
            fp->selected_idx = original_idx;

            return strdup(fp->items[original_idx].label);
        }

        if (strlen(fp->input_buf) > 0) {
            return strdup(fp->input_buf);
        }
    }

    return nullptr;
}

/**
 * Fungsi untuk meminta input dari pengguna [PUBLIC API]
 */
char *FloatPrompt_ask(FloatPrompt *fp, const char *msg, const char *default_val, int icon_id,
                      Font font, BufManager *bufmgr) {
    fp->items = nullptr;
    fp->item_count = 0;
    fp->selected_idx = 0;

    FloatPrompt_open(fp, msg, default_val, icon_id);
    char *result = nullptr;

    while (fp->is_active && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(g_theme.bg_editor);

        render_all_ui(bufmgr, font);

        // Render overlay popup
        result = FloatPrompt_update_and_render(bufmgr, fp, font);
        EndDrawing();

        if (result != nullptr) break;
    }

    return result;
}

/**
 * Fungsi Baru (Prompt Universal dengan Box Suggestion!) [PUBLIC API]
 */
char *FloatPrompt_ask_with_items(FloatPrompt *fp, const char *msg, const char *default_val,
                                 int icon_id, Font font, BufManager *bufmgr, PromptItem *items,
                                 size_t item_count) {
    fp->items = items;
    fp->item_count = item_count;
    fp->selected_idx = 0;

    FloatPrompt_open(fp, msg, default_val, icon_id);
    char *result = nullptr;

    while (fp->is_active && !WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(g_theme.bg_editor);
        render_all_ui(bufmgr, font);
        result = FloatPrompt_update_and_render(bufmgr, fp, font);
        EndDrawing();
        if (result != nullptr) break;
    }
    return result;
}

/**
 * Fungsi untuk melompat ke Hasil pencarian [PUBLIC API]
 */
void Buffer_goto_search_hit(BufManager *bufmgr, const SearchHitBuffer *hit) {
    // Kita taruh goto ke lines disini ya
    // Karena untuk mengakomodasi Layout visible lines.
    // Jika masih di search.c maka akan bentrok dengan Buffer.h
    // Saling cross include si Header bahaya
    Buffer *buf = BufManager_getactive(bufmgr);
    EditorLayout layout = get_editor_layout(bufmgr);  // ambil layout

    if (!buf || !hit) return;

    buf->cursor.y = hit->line;
    buf->cursor.x = hit->col;

    if (hit->line < buf->lines.line_count) {
        buf->cursor.cursor_pos = buf->lines.offset[hit->line] + hit->col;
    } else {
        buf->cursor.cursor_pos = String_len(buf->str);
    }

    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);  // Memastikan bahwa is_selected mati!

    // scroll biar kelihatan
    int vis = layout.visible_lines;

    if ((int)buf->cursor.y < buf->scroll_y) buf->scroll_y = (int)buf->cursor.y;
    if ((int)buf->cursor.y >= buf->scroll_y + vis) buf->scroll_y = (int)buf->cursor.y - vis + 1;
}

/**
 * Fungsi untuk Fuzzy Search di Buffer [PUBLIC API]
 */
char *SearchPrompt_ask(BufManager *bufmgr, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return nullptr;

    FloatPrompt *fp = &g_prompt;
    FloatPrompt_open(fp, "Search", "", ICON_LENS);

    // Alokasi statis untuk hit pencarian
    static SearchHitBuffer hits[MAX_SEARCH_HIT];
    static PromptItem items[MAX_SEARCH_HIT];

    char *result = nullptr;
    char last_q[256] = "\0";

    while (fp->is_active && !WindowShouldClose()) {
        // Rebuild list kalau query berubah
        if (strcmp(last_q, fp->input_buf) != 0) {
            snprintf(last_q, sizeof(last_q), "%s", fp->input_buf);

            int n = 0;
            if (strlen(fp->input_buf) > 0) {
                n = Buffer_search(buf, fp->input_buf, hits, MAX_SEARCH_HIT);
            }

            for (int i = 0; i < n; i++) {
                snprintf(items[i].label, sizeof(items[i].label), "%s", hits[i].label);
                items[i].icon_id = ICON_FILETYPE_TEXT;
            }

            fp->items = items;
            fp->item_count = (size_t)n;
            fp->selected_idx = 0;
            fp->scroll_offset = 0;
        }

        BeginDrawing();
        ClearBackground(g_theme.bg_editor);
        render_all_ui(bufmgr, font);

        result = FloatPrompt_update_and_render(bufmgr, fp, font);
        EndDrawing();

        if (result != nullptr) {
            // Karena fp->selected_idx sudah dikoreksi ke original index di
            // FloatPrompt_update_and_render, akses ke array hits ini sekarang 100% aman dan akurat!
            if (fp->item_count > 0 && fp->selected_idx >= 0 &&
                fp->selected_idx < (int)fp->item_count) {
                Buffer_goto_search_hit(bufmgr, &hits[fp->selected_idx]);
            }
            free(result);
            break;
        }
    }
    return nullptr;
}
