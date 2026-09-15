/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_H
#define UI_H

#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "settings_txted.h"

// Karena float harus di deklarasikan dari awal
// Jadi ga bisa pakai constexpr,
// Ya kita tahu bahwa font size itu dinamis berdasarkan settings
#define FONT_SIZE (float)default_settings.font_size
constexpr size_t TAB_H = 36;
constexpr size_t STATUS_H = 26;
constexpr size_t DIAG_PANEL_H = 26;
constexpr size_t PAD_X = 16;
constexpr size_t PAD_Y = 10;
constexpr size_t LINE_H = 24;
constexpr size_t GUTTER_W = 50;

/**
 * Struct untuk Layouting
 */
typedef struct {
    int win_h, win_w;
    int fm_x, fm_y, fm_w, fm_h;  // area file manager
    int editor_x, editor_y;      // origin editor
    int editor_w, editor_h;      // ukuran editor
    int gutter_screen_x;         // X gutter di layar
    int text_screen_x;           // X awal teks di layar
    int visible_lines;           // Visible Lines
} EditorLayout;

/**
 * Struct untuk membawa data dari Prompt
 */
typedef struct {
    char label[128];
    char subtext[128];
    int icon_id;
} PromptItem;

/**
 * Struct untuk konfigurasi FloatPrompt
 */
typedef struct {
    char label[64];
    char input_buf[256];
    int icon_id;

    PromptItem *items;
    size_t item_count;
    int selected_idx;
    int scroll_offset;
    bool is_active;
    bool edit_mode;
} FloatPrompt;

extern FloatPrompt g_prompt;

EditorLayout get_editor_layout(BufManager *bufmgr);  // Layout manager

void draw_tabs(BufManager *bufmgr, Font font);
void draw_status(BufManager *bufmgr, Font font);
void draw_editor(BufManager *bufmgr, Font font);
void draw_diagnostic_bar(BufManager *bufmgr, Font font);
void draw_dialog_modal(BufManager *bufmgr, Font font);
void handle_input(BufManager *bufmgr, Font font);
void handle_mouse_input(BufManager *bufmgr, Font font);
void Draw_confirm_exit(BufManager *bufmgr, Font font);
void render_top_right_state(BufManager *bufmgr, Font font);

// Pop up
char *FloatPrompt_ask(FloatPrompt *fp, const char *msg, const char *default_val, int icon_id,
                      Font font, BufManager *bufmgr);
char *SearchPrompt_ask(BufManager *bufmgr, Font font);
void Buffer_goto_search_hit(BufManager *bufmgr, const SearchHitBuffer *hit);

// Pop up dengan box suggestion
char *FloatPrompt_ask_with_items(FloatPrompt *fp, const char *msg, const char *default_val,
                                 int icon_id, Font font, BufManager *bufmgr, PromptItem *items,
                                 size_t item_count);

#endif
