/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "raygui.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

static float dialog_scroll_y = 0.0f;  // Menyimpan offset scroll dialog

/* =============================
 * Internal State
 * ============================= */
static int active_menu = -1;

typedef void (*MenuAction)(BufManager *bufmgr, Font font);
typedef struct {
    const char *label;
    const char *shortcut;
    MenuAction action;
} MenuItem;

bool Is_active_menu(void) { return active_menu != -1; }
typedef enum { DIALOG_NONE = 0, DIALOG_HELP, DIALOG_ABOUT } DialogState;
static DialogState current_dialog = DIALOG_NONE;

// Setter helper
static void UI_open_dialog(BufManager *bufmgr, DialogState state) {
    current_dialog = state;
    dialog_scroll_y = 0.0f;
    SET_FLAG(bufmgr->win_flags, TXTED_SHOW_HELP);
}

/* ------------------------------- *
 * Extern Function
 * ------------------------------- */
extern void Nav_create_folder(BufManager *bufmgr, Font font);
extern void Nav_open_file(BufManager *bufmgr, Font font);
extern void Nav_exit(BufManager *bufmgr, Font font);
extern void Nav_save_as(BufManager *bufmgr, Font font);
extern void Nav_save(BufManager *bufmgr, Font font);
extern void Nav_create_new_file(BufManager *bufmgr, Font font);
extern void Nav_close_tab(BufManager *bufmgr, Font font);
extern void Nav_copy(BufManager *bufmgr, Font font);
extern void Nav_cut(BufManager *bufmgr, Font font);
extern void Nav_paste(BufManager *bufmgr, Font font);
extern void Nav_redo(BufManager *bufmgr, Font);
extern void Nav_undo(BufManager *bufmgr, Font font);

void Nav_show_help(BufManager *bufmgr, Font font) {
    (void)bufmgr;
    (void)font;
    UI_open_dialog(bufmgr, DIALOG_HELP);
}

void Nav_show_about(BufManager *bufmgr, Font font) {
    (void)bufmgr;
    (void)font;
    UI_open_dialog(bufmgr, DIALOG_ABOUT);
}

void Nav_show_fm(BufManager *bufmgr, Font font) {
    (void)font;
    if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
        SET_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
    } else {
        CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
    }
}

/**
 * Menu items
 */
static MenuItem file_items[] = {
    {"New File", "Ctrl+N", Nav_create_new_file},
    {"Open File", "Ctrl+O", Nav_open_file},
    {"Save File", "Ctrl+S", Nav_save},
    {"Save As", "Ctrl+Shift+S", Nav_save_as},
    {"Create Folder", "Ctrl+P", Nav_create_folder},
    {"File Manager", "Ctrl + F", Nav_show_fm},
    {"Exit", "Ctrl+Q", Nav_exit},
};

static MenuItem edit_items[] = {{"Undo", "Ctrl+Z", Nav_undo},
                                {"Redo", "Ctrl+R", Nav_redo},
                                {"Cut", "Ctrl+X", Nav_cut},
                                {"Copy", "Ctrl+C", Nav_copy},
                                {"Paste", "Ctrl+V", Nav_paste}};

static MenuItem help_items[] = {{"Help", "", Nav_show_help}, {"About", "", Nav_show_about}};

/**
 * Render Dropdown Items
 * Disesuaikan agar item height dan positioning shortcut responsif terhadap font.baseSize.
 */
static void draw_dropdown_items(int menu_idx, float x, float y, BufManager *bufmgr, Font font) {
    MenuItem *items = nullptr;
    int count = 0;
    float current_font_size = (float)font.baseSize;

    if (menu_idx == 0) {
        items = file_items;
        count = sizeof(file_items) / sizeof(file_items[0]);
    } else if (menu_idx == 1) {
        items = edit_items;
        count = sizeof(edit_items) / sizeof(edit_items[0]);
    } else if (menu_idx == 2) {
        items = help_items;
        count = sizeof(help_items) / sizeof(help_items[0]);
    }

    if (!items || count == 0) return;

    float item_h = current_font_size + 10.0f;
    float dropdown_w = 250.0f;
    float dropdown_h = count * item_h + 8.0f;

    Rectangle dropdown_rect = {x, y, dropdown_w, dropdown_h};
    Vector2 mouse_pos = GetMousePosition();

    DrawRectangleRec(dropdown_rect, g_theme.bg_sidebar);
    DrawRectangleLinesEx(dropdown_rect, 1.0f, g_theme.active_tab);

    bool item_clicked = false;

    for (int i = 0; i < count; i++) {
        Rectangle item_rect = {x + 2.0f, y + 4.0f + (i * item_h), dropdown_w - 4.0f, item_h - 2.0f};
        bool is_hovered = CheckCollisionPointRec(mouse_pos, item_rect);

        if (is_hovered) {
            DrawRectangleRec(item_rect, g_theme.active_tab);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                MenuAction act = items[i].action;
                active_menu = -1;
                item_clicked = true;

                if (act != nullptr) {
                    act(bufmgr, font);
                }
                break;
            }
        }

        Color text_color = is_hovered ? g_theme.cursor : g_theme.text_normal;
        float text_y = item_rect.y + (item_rect.height - current_font_size) / 2.0f;
        Vector2 text_pos = {item_rect.x + 8.0f, text_y};
        DrawTextEx(font, items[i].label, text_pos, current_font_size, 1.0f, text_color);

        if (items[i].shortcut[0] != '\0') {
            Vector2 sc_size =
                MeasureTextEx(font, items[i].shortcut, current_font_size - 2.0f, 1.0f);
            Vector2 sc_pos = {item_rect.x + dropdown_w - sc_size.x - 12.0f,
                              item_rect.y + (item_rect.height - (current_font_size - 2.0f)) / 2.0f};
            DrawTextEx(font, items[i].shortcut, sc_pos, current_font_size - 2.0f, 1.0f,
                       g_theme.text_muted);
        }
    }

    if (!item_clicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (!CheckCollisionPointRec(mouse_pos, dropdown_rect) && mouse_pos.y > TAB_H) {
            active_menu = -1;
        }
    }
}

/**
 * Fungsi untuk Draw Menu
 */
int draw_menu(BufManager *bufmgr, Font font, Vector2 mouse_pos) {
    const char *menus[] = {"File", "Edit", "Help"};
    int num_menus = 3;
    int x = 12;
    float active_menu_x = 0;
    float current_font_size = (float)font.baseSize;

    for (int m = 0; m < num_menus; m++) {
        Vector2 msize = MeasureTextEx(font, menus[m], current_font_size, 1.0f);
        int menu_w = (int)msize.x + 16;
        Rectangle menu_rect = {(float)x, 4.0f, (float)menu_w, (float)(TAB_H - 8)};

        bool is_hovered = CheckCollisionPointRec(mouse_pos, menu_rect);
        bool is_active = (active_menu == m);

        if (is_active) active_menu_x = (float)x;

        if (is_hovered && active_menu != -1 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            active_menu = m;
        }

        Color menu_text_color = (is_hovered || is_active) ? g_theme.cursor : g_theme.text_normal;

        if (is_hovered || is_active) {
            DrawRectangleRec(menu_rect, g_theme.active_tab);

            if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && active_menu == -1) {
                active_menu = m;
            } else if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && is_active) {
                active_menu = -1;
            }
        }

        float text_y = menu_rect.y + (menu_rect.height - current_font_size) / 2.0f;
        Vector2 menu_pos = {(float)(x + 8), text_y};
        DrawTextEx(font, menus[m], menu_pos, current_font_size, 1.0f, menu_text_color);

        x += menu_w + 4;
    }

    if (active_menu != -1) {
        draw_dropdown_items(active_menu, active_menu_x, (float)TAB_H, bufmgr, font);
    }

    return x + 12;
}

/**
 * Fungsi untuk Draw Tabs
 */
void draw_tabs(BufManager *bufmgr, Font font) {
    EditorLayout layout = get_editor_layout(bufmgr);
    float current_font_size = (float)font.baseSize;

    DrawRectangle(0, layout.win_h - TAB_H, layout.win_w, TAB_H, g_theme.bg_sidebar);

    Vector2 mouse_pos = GetMousePosition();
    int x = draw_menu(bufmgr, font, mouse_pos);

    // Separator tipis antara Menu Bar dan Tab List
    DrawLine(x + 2, 8, x + 2, TAB_H - 8, g_theme.text_muted);
    x += 12;

    /* ============================================================
     * RENDER TABS
     * ============================================================ */
    for (size_t i = 0; i < bufmgr->num_tabs; i++) {
        Buffer *buf = bufmgr->buf[i];
        if (!buf) continue;

        char text[128];
        snprintf(text, sizeof(text), "%s%s", buf->filename ? buf->filename : "Untitled",
                 HAS_FLAG(buf->buf_flags, BUF_IS_DIRTY) ? "[*]" : "");

        Vector2 nsize = MeasureTextEx(font, text, current_font_size, 1.0f);
        int tab_w = (int)nsize.x + 48;
        Rectangle tab_rect = {(float)x, 4.0f, (float)tab_w, (float)(TAB_H - 8)};

        Color bg = (i == (size_t)bufmgr->active_idx) ? g_theme.active_tab : g_theme.bg_sidebar;
        DrawRectangleRec(tab_rect, bg);

        // Render Teks Nama File
        float text_y = tab_rect.y + (tab_rect.height - current_font_size) / 2.0f;
        Vector2 text_pos = {(float)(x + 12), text_y};
        DrawTextEx(font, text, text_pos, current_font_size, 1.0f, g_theme.text_normal);

        // Render Tombol "x"
        int x_btn_x = x + 12 + (int)nsize.x + 8;
        Rectangle x_rect = {(float)x_btn_x,
                            tab_rect.y + (tab_rect.height - current_font_size) / 2.0f, 16.0f,
                            current_font_size};

        bool is_x_hovered = CheckCollisionPointRec(mouse_pos, x_rect);
        Color x_color = is_x_hovered ? g_theme.selection : g_theme.text_muted;

        Vector2 x_pos = {(float)x_btn_x + 3, text_y};
        DrawTextEx(font, "x", x_pos, current_font_size, 1.0f, x_color);

        // Handling Klik Tab & 'x'
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mouse_pos, x_rect)) {
                bufmgr->active_idx = (int)i;
                Nav_close_tab(bufmgr, font);
                buf->scroll_y = 0;
                break;
            } else {
                if (CheckCollisionPointRec(mouse_pos, tab_rect)) {
                    bufmgr->active_idx = (int)i;
                    buf->scroll_y = 0;
                }
            }
        }

        x += tab_w + 4;
    }

    if (bufmgr->num_tabs == 0) {
        BufManager_newtab(bufmgr, nullptr);
    }

    /* Tombol '+' New Tab */
    if (bufmgr->num_tabs < MAX_TABS) {
        Rectangle plus = {(float)x + 4, 4, 28, (float)(TAB_H - 8)};
        DrawRectangleRec(plus, g_theme.active_tab);

        Vector2 plus_size = MeasureTextEx(font, "+", current_font_size, 1.0f);
        float plus_y = plus.y + (plus.height - current_font_size) / 2.0f;
        Vector2 plus_pos = {plus.x + (plus.width - plus_size.x) / 2.0f, plus_y};
        DrawTextEx(font, "+", plus_pos, current_font_size, 1.0f, g_theme.text_normal);

        if (CheckCollisionPointRec(mouse_pos, plus) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            BufManager_newtab(bufmgr, nullptr);
        }
    }
}

/**
 * Fungsi untuk render Dialog / Help / About Modal
 */
void draw_dialog_modal(BufManager *bufmgr, Font font) {
    if (current_dialog == DIALOG_NONE) return;

    static bool just_opened = true;
    if (just_opened) {
        just_opened = false;
        return;
    }

    float current_font_size = (float)font.baseSize;
    Vector2 mouse_pos = GetMousePosition();

    EditorLayout layout = get_editor_layout(bufmgr);
    float screen_w = (float)layout.win_w;
    float screen_h = (float)layout.win_h;

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, g_theme.backdrop);

    float card_w = 480.0f;
    float card_h = 380.0f;
    float card_x = (screen_w - card_w) / 2.0f;
    float card_y = (screen_h - card_h) / 2.0f;
    Rectangle card_rect = {card_x, card_y, card_w, card_h};

    DrawRectangleRec(card_rect, g_theme.bg_card);
    DrawRectangleLinesEx(card_rect, 1.0f, g_theme.border);

    // Header Title & Tombol Close [X]
    const char *title = (current_dialog == DIALOG_HELP) ? "Help / Shortcuts" : "About Editor";
    DrawTextEx(font, title, (Vector2){card_x + 20, card_y + 16}, current_font_size + 2.0f, 1.0f,
               g_theme.cursor);

    Rectangle close_rect = {card_x + card_w - 32, card_y + 12, 24, 24};
    bool close_hovered = CheckCollisionPointRec(mouse_pos, close_rect);
    Color close_color = close_hovered ? g_theme.error : g_theme.text_muted;
    DrawTextEx(font, "X", (Vector2){close_rect.x + 6, close_rect.y + 2}, current_font_size, 1.0f,
               close_color);

    float header_bottom = card_y + current_font_size + 28.0f;
    DrawLine((int)card_x + 16, (int)header_bottom, (int)(card_x + card_w - 16), (int)header_bottom,
             g_theme.border);

    Rectangle content_area = {card_x + 16, header_bottom + 8.0f, card_w - 32,
                              card_h - (header_bottom - card_y + 16.0f)};

    if (current_dialog == DIALOG_HELP) {
        const char *shortcuts[] = {"--- File Operations ---",
                                   "Ctrl + N : New File",
                                   "Ctrl + O : Open File",
                                   "Ctrl + S : Save File",
                                   "Ctrl + Shift + S : Save As",
                                   "Ctrl + P : Create Folder",
                                   "Ctrl + F : File Manager",
                                   "",
                                   "--- Tab Management ---",
                                   "Ctrl + T : New Tab",
                                   "Ctrl + W : Close Tab",
                                   "Ctrl + Tab : Next Tab",
                                   "Ctrl + Shift + Tab : Prev Tab",
                                   "",
                                   "--- Editing ---",
                                   "Ctrl + Z : Undo",
                                   "Ctrl + R : Redo",
                                   "Ctrl + C : Copy",
                                   "Ctrl + X : Cut",
                                   "Ctrl + V : Paste",
                                   "Ctrl + B : Fuzzy Search",
                                   "",
                                   "--- LSP ---",
                                   "Ctrl + K : Show Hover Doc",
                                   "Escape / Esc : Hide LSP, Signature\nand Hover",
                                   "",
                                   "",
                                   "--- Navigation ---",
                                   "Ctrl + U : Jump up 5 lines",
                                   "Ctrl + D : Jump down 5 lines",
                                   "Ctrl + H : Move start of line",
                                   "Ctrl + L : Move end of line",
                                   "",
                                   "--- Application ---",
                                   "Ctrl + Q : Exit",
                                   "Ctrl + Shift + Q : Force Close"};

        int num_items = sizeof(shortcuts) / sizeof(shortcuts[0]);
        // Line height dinamis mengikuti ukuran font
        float line_height = current_font_size + 6.0f;
        float total_content_h = num_items * line_height;

        if (CheckCollisionPointRec(mouse_pos, card_rect)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                dialog_scroll_y -= wheel * (current_font_size * 1.5f);
            }
        }

        float max_scroll = total_content_h - content_area.height;
        if (max_scroll < 0) max_scroll = 0;
        if (dialog_scroll_y < 0) dialog_scroll_y = 0;
        if (dialog_scroll_y > max_scroll) dialog_scroll_y = max_scroll;

        BeginScissorMode((int)content_area.x, (int)content_area.y, (int)content_area.width,
                         (int)content_area.height);

        float line_y = content_area.y - dialog_scroll_y;
        for (int i = 0; i < num_items; i++) {
            if (shortcuts[i][0] == '-') {
                DrawTextEx(font, shortcuts[i], (Vector2){content_area.x + 4, line_y},
                           current_font_size, 1.0f, g_theme.cursor);
            } else {
                DrawTextEx(font, shortcuts[i], (Vector2){content_area.x + 12, line_y},
                           current_font_size, 1.0f, g_theme.text_normal);
            }
            line_y += line_height;
        }

        if (max_scroll > 0) {
            float scrollbar_h = (content_area.height / total_content_h) * content_area.height;
            float scrollbar_y = content_area.y + (dialog_scroll_y / max_scroll) *
                                                     (content_area.height - scrollbar_h);
            Rectangle scrollbar_rect = {card_x + card_w - 12, scrollbar_y, 4, scrollbar_h};
            DrawRectangleRounded(scrollbar_rect, 0.5f, 4, g_theme.text_muted);
        }

    } else if (current_dialog == DIALOG_ABOUT) {
        const char *help_msg = "TxtEd created and Maintained by Nash.";

        DrawTextEx(font, "TxtEd v1.0 - Simple Text Editor",
                   (Vector2){content_area.x + 8, content_area.y + 20}, current_font_size, 1.0f,
                   g_theme.text_highlight);
        DrawTextEx(font, help_msg,
                   (Vector2){content_area.x + 8, content_area.y + 20 + current_font_size + 10.0f},
                   current_font_size, 1.0f, g_theme.text_normal);
    }

    EndScissorMode();

    bool clicked_close =
        CheckCollisionPointRec(mouse_pos, (Rectangle){card_x + card_w - 32, card_y + 12, 24, 24}) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    bool clicked_outside =
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse_pos, card_rect);
    bool pressed_esc = IsKeyPressed(KEY_ESCAPE);

    if (clicked_close || clicked_outside || pressed_esc) {
        current_dialog = DIALOG_NONE;
        just_opened = true;

        CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_HELP);

        Buffer *buf = BufManager_getactive(bufmgr);
        if (buf) {
            CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
        }
    }
}
