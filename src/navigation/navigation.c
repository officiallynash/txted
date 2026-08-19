/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <complex.h>
#include <ctype.h>
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "git_client.h"
#include "lsp_server.h"
#include "lsp_ui.h"
#include "notification.h"
#include "raygui.h"
#include "ui.h"

extern bool ExitWindowRequested;   // ExitWindowRequested (main.c)
extern bool Is_active_menu(void);  // Check if active menu is open (tab.c)
extern int visible_lines(void);    // Jumlah baris yang terlihat (render.c)
extern int calculate_score(const char *query, const char *label);  // Extern (lsp_ui.c)
extern void set_cursor_from_mouse(BufManager *bufmgr, Vector2 mouse, int scroll_y,
                                  Font font);        // set_cursor_from_mouse (nav_helper.c)
extern void sync_cursor_line_from_pos(Buffer *buf);  // sync_cursor_line_from_pos (nav_helper.c)
extern void Syntax_auto_indent(Buffer *active_buf);  // Syntax_auto_indent (nav_helper.c)

extern void Nav_move_up(Buffer *buf);                          // Nav_move_up (nav_utils.c)
extern void Nav_move_down(Buffer *buf);                        // Nav_move_down (nav_utils.c)
extern void Nav_move_left(Buffer *buf);                        // Nav_move_left (nav_utils.c)
extern void Nav_move_right(Buffer *buf);                       // Nav_move_right (nav_utils.c)
extern void Nav_mouse_scroll(Buffer *buf, float wheel);        // Nav_mouse_scroll (nav_utils.c)
extern void Nav_goto_end_of_line(Buffer *buf);                 // Nav_goto_end_of_line (nav_utils.c)
extern void Nav_jump_down(Buffer *buf);                        // Nav_jump_down (nav_utils.c)
extern void Nav_jump_up(Buffer *buf);                          // Nav_jump_up (nav_utils.c)
extern void Nav_create_folder(BufManager *bufmgr, Font font);  // Nav_create_folder (nav_utils.c)
extern void Nav_open_file(BufManager *bufmgr, Font font);      // Nav_Open_file (nav_utils.c)
extern void Nav_exit(BufManager *bufmgr, Font font);           // Nav_exit (nav_utils.c)
extern void Nav_save_as(BufManager *bufmgr, Font font);        // Nav_save_as (nav_utils.c)
extern void Nav_save(BufManager *bufmgr, Font font);           // Nav_save (nav_utils.c)
extern void Nav_create_new_file(BufManager *bufmgr,
                                Font font);                // Nav_create_new_file (nav_utils.c)
extern void Nav_close_tab(BufManager *bufmgr, Font font);  // Nav_close_tab (nav_utils.c)
extern void Nav_copy(BufManager *bufmgr, Font font);       // Nav_copy (nav_utils.c)
extern void Nav_cut(BufManager *bufmgr, Font font);        // Nav_cut (nav_utils.c)
extern void Nav_paste(BufManager *bufmgr, Font font);      // Nav_paste (nav_utils.c)
extern void Nav_redo(BufManager *bufmgr, Font font);       // Nav_redo (nav_utils.c)
extern void Nav_undo(BufManager *bufmgr, Font font);       // Nav_undo (nav_utils.c)

#define SIGNATURE_HIDE()                                   \
    if (g_lsp_ui.sig_y != buf->cursor.y) {                 \
        g_lsp_ui.has_signature = false;                    \
        g_lsp_ui.sig_y = 0;                                \
        lsp_free_signature_help(&g_lsp_ui.signature_help); \
    }

/**
 * Fungsi untuk Navigation mouse berbasis Focus mode
 */
static void Update_navigation_click(BufManager *bufmgr) {
    if (!bufmgr->show_fm) {
        bufmgr->focus_mode = WRITE;
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        EditorLayout Layout = get_editor_layout(bufmgr);

        // Jangan proses kalau klik di area Bar Menu Atas (Tab Bar)
        if (mouse.y <= TAB_H) return;

        // Klik di area File Manager Sidebar
        if (mouse.x >= Layout.fm_x && mouse.x < (Layout.fm_x + Layout.fm_w)) {
            bufmgr->focus_mode = FILE_MANAGER;  // atau FM
        }

        // Klik di area Write / Text Editor
        else if (mouse.x >= Layout.editor_x && mouse.x < (Layout.editor_x + Layout.editor_w)) {
            bufmgr->focus_mode = WRITE;
        }
    }
}

/**
 * Input handling
 **/
void handle_input(BufManager *bufmgr, Font font) {
    Update_navigation_click(bufmgr);
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

// Helper macro/lambda kecil internal
#define CHECK_SELECTION()                                  \
    if (is_shift) {                                        \
        if (!buf->selection.is_selected) {                 \
            buf->selection.start = buf->cursor.cursor_pos; \
            buf->selection.is_selected = true;             \
        }                                                  \
    } else {                                               \
        buf->selection.is_selected = false;                \
    }

    /* -------------------------------- *
     * Scroll
     * -------------------------------- */
    Vector2 mouse = GetMousePosition();
    EditorLayout Layout = get_editor_layout(bufmgr);

    // Cek apakah mouse berada di wilayah Editor
    bool is_mouse_in_editor = (mouse.x >= Layout.editor_x) &&
                              (mouse.x < Layout.editor_x + Layout.editor_w) && (mouse.y > TAB_H) &&
                              !Is_active_menu();

    bool is_mouse_scroll = false;
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && !bufmgr->show_help && !g_lsp_ui.has_hover) {  // Kalau ada hover matiin dulu
        if (g_lsp_ui.visible && g_lsp_ui.has_completion) {
            // Scroll pilihan popup via mouse wheel!
            if (wheel > 0)
                g_lsp_ui.selected_index--;
            else
                g_lsp_ui.selected_index++;

            // Clamp index
            if (g_lsp_ui.selected_index < 0) g_lsp_ui.selected_index = 0;
            if ((size_t)g_lsp_ui.selected_index >= g_lsp_ui.completion.count) {
                g_lsp_ui.selected_index = (int)g_lsp_ui.completion.count - 1;
            }
        } else if (is_mouse_in_editor) {
            is_mouse_scroll = true;
            SIGNATURE_HIDE();  // Biar auto hide si Signature Help
            Nav_mouse_scroll(buf, wheel);
        }
    }

    /* ---------------------------------------------------------------- *
     * Handling Mouse Selection (Click & Drag)
     * ---------------------------------------------------------------- */
    if (is_mouse_in_editor) {
        // PERTAMA KALI KLIK KIRI (Awal Selection)
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Pindahkan kursor ke posisi klik
            set_cursor_from_mouse(bufmgr, mouse, buf->scroll_y, font);
            SIGNATURE_HIDE();  // Auto hide Signature Help

            // Kunci titik anchor awal seleksi
            buf->selection.start = buf->cursor.cursor_pos;
            buf->selection.is_selected = false;  // Belum ter-select sebelum digeser
        }
        // KLIK KIRI DITAHAN DAN DIGESER / DRAG
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            // Update kursor ke posisi mouse yang baru saat di-drag
            set_cursor_from_mouse(bufmgr, mouse, buf->scroll_y, font);

            // Jika posisi kursor bergeser dari titik awal -> NYALAKAN SELECTION!
            // Tambahan jika sedang Drag scroll bar, maka Selection tidak aktif
            if (!buf->is_dragging && buf->cursor.cursor_pos != buf->selection.start) {
                buf->selection.is_selected = true;
            } else {
                buf->selection.is_selected = false;
            }
        }
    }

    /* -------------------- *
     * Keyboard Input
     * -------------------- */
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32) {
            char utf8[8];
            int n = 0;
            /* Raylib GetCharPressed codepoint */
            if (key < 0x80) {
                utf8[0] = (char)key;
                n = 1;
            } else {
                /* simple utf-8 encode */
                n = 0;
                /* pakai pendekatan sederhana: hanya BMP */
                if (key <= 0x7FF) {
                    utf8[n++] = (char)(0xC0 | (key >> 6));
                    utf8[n++] = (char)(0x80 | (key & 0x3F));
                } else {
                    utf8[n++] = (char)(0xE0 | (key >> 12));
                    utf8[n++] = (char)(0x80 | ((key >> 6) & 0x3F));
                    utf8[n++] = (char)(0x80 | (key & 0x3F));
                }
            }

            utf8[n] = '\0';  // Null Terminator

            // Auto pair
            if (key == '{') {
                Buffer_insert(buf, buf->cursor.cursor_pos, "{}");
                buf->cursor.cursor_pos--;
            } else if (key == '[') {
                Buffer_insert(buf, buf->cursor.cursor_pos, "[]");
                buf->cursor.cursor_pos--;
            } else if (key == '(') {
                Buffer_insert(buf, buf->cursor.cursor_pos, "()");
                g_lsp_ui.signature_pending = true;
                buf->cursor.cursor_pos--;
            } else if (key == '"') {
                Buffer_insert(buf, buf->cursor.cursor_pos, "\"\"");
                buf->cursor.cursor_pos--;
            } else if (key == '\'') {
                Buffer_insert(buf, buf->cursor.cursor_pos, "\'\'");
                buf->cursor.cursor_pos--;
            } else {
                Buffer_insert(buf, buf->cursor.cursor_pos, utf8);
            }

            sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope

            // Debounce untuk LSP
            if (isalnum(key) || key == '.' || key == '>' || key == ':' || key == '-' ||
                key == '#') {
                lsp_debounce_timer = LSP_DEBOUNCE_DELAY;
            } else if (key == ' ') {  // Kalau spasi, sembunyikan LSP popup
                lsp_ui_hide();
            }

            // Signature Help
            else if (key == '(' || key == ',') {
                g_lsp_ui.signature_pending = true;
                g_lsp_ui.sig_y = buf->cursor.y;  // Simpan Y untuk auto close
            } else if (key == ')' || key == ';') {
                g_lsp_ui.signature_pending = false;
                g_lsp_ui.has_signature = false;
                g_lsp_ui.sig_y = 0;
                lsp_free_signature_help(&g_lsp_ui.signature_help);
            }
        }
        key = GetCharPressed();
    }

    /* -------------------- *
     * Handling Navigation / Enter saat LSP Popup Aktif
     * -------------------- */
    bool lsp_handled = false;  // Flag penanda agar input tidak diproses dua kali

    if (g_lsp_ui.visible && g_lsp_ui.has_completion) {
        char current_word[256];
        Buffer_get_current_word(buf, current_word, sizeof(current_word));

        int total_items = 0;
        for (size_t i = 0; i < g_lsp_ui.completion.count && total_items < 256; i++) {
            const char *label = g_lsp_ui.completion.items[i].label;
            if (!label) continue;

            if (calculate_score(current_word, label) >= 0) {
                total_items++;
            }
        }

        if (total_items > 0) {
            if (IsKeyPressed(KEY_DOWN)) {
                g_lsp_ui.selected_index++;
                if (g_lsp_ui.selected_index >= total_items) {
                    g_lsp_ui.selected_index = 0;
                }
                lsp_handled = true;
            } else if (IsKeyPressed(KEY_UP)) {
                g_lsp_ui.selected_index--;
                if (g_lsp_ui.selected_index < 0) {
                    g_lsp_ui.selected_index = total_items - 1;
                }
                lsp_handled = true;
            } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                       IsKeyPressed(KEY_TAB)) {
                CompletionItem *items = lsp_get_selected_item(current_word);
                if (items) {
                    lsp_apply_completion(buf, items);
                }

                lsp_ui_hide();
                lsp_handled = true;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                lsp_ui_hide();
                lsp_handled = true;
            }
        }
    }

    // JIKA INPUT SUDAH DIMAKAN LSP, LOMPATI NAVIGASI EDITOR BIASA!
    if (lsp_handled) {
        goto sync_scroll;
    }

    // Matiin signature help dan Hover
    if (IsKeyPressed(KEY_ESCAPE)) {
        // Escape untuk menutup Hover
        if (g_lsp_ui.hover_pending || g_lsp_ui.has_hover) {
            g_lsp_ui.has_hover = false;
            g_lsp_ui.hover_pending = false;
            lsp_free_hover(&g_lsp_ui.hover);
        }
        // Escape untuk batalkan Selection
        if (buf->selection.is_selected) buf->selection.is_selected = false;
        // Escape untuk menutup Signature
        if (g_lsp_ui.has_signature || g_lsp_ui.signature_pending) {
            g_lsp_ui.has_signature = false;
            g_lsp_ui.signature_pending = false;
            lsp_free_signature_help(&g_lsp_ui.signature_help);
        }
    }

    /* -------------------- *
     * Handling Enter
     * -------------------- */
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        Syntax_auto_indent(buf);
    }

    /* -------------------- *
     * Handling Tab
     * -------------------- */
    if (IsKeyPressed(KEY_TAB) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_SHIFT)) {
        Buffer_insert(buf, buf->cursor.cursor_pos, "\t");
    }

    /* -------------------- *
     * Handling Backspace
     * -------------------- */
    if (IsKeyPressed(KEY_BACKSPACE)) {
        if (buf->selection.is_selected || buf->cursor.cursor_pos > 0) {
            Buffer_delete(buf, buf->cursor.cursor_pos);
            lsp_ui_hide();
        }
    }

    /* -------------------- *
     * Handling Left
     * -------------------- */
    if (IsKeyPressed(KEY_LEFT)) {
        CHECK_SELECTION();
        Nav_move_left(buf);
    }

    /* -------------------- *
     * Handling Right
     * -------------------- */
    if (IsKeyPressed(KEY_RIGHT)) {
        CHECK_SELECTION();
        Nav_move_right(buf);
    }

    /* -------------------- *
     * Handling Up
     * -------------------- */
    if (IsKeyPressed(KEY_UP)) {
        CHECK_SELECTION();
        Nav_move_up(buf);
        SIGNATURE_HIDE();  // Auto hide
    }

    /* -------------------- *
     * Handling Down
     * -------------------- */
    if (IsKeyPressed(KEY_DOWN)) {
        CHECK_SELECTION();
        Nav_move_down(buf);
        SIGNATURE_HIDE();  // Auto hide
    }

    /* -------------------- *
     * Handling Hotkeys CTRL
     * -------------------- */
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);  // Shift pressed
        /* --------------------- *
         * New Tab (CTRL + T)
         * Close Tab (CTRL + W)
         * Switch Tab (CTRL + TAB)
         * --------------------- */
        if (IsKeyPressed(KEY_T)) BufManager_newtab(bufmgr, NULL);

        if (is_shift && IsKeyPressed(KEY_W)) {
            buf->is_dirty = false;
            BufManager_closetab(bufmgr);
        } else if (!is_shift && IsKeyPressed(KEY_W)) {
            Nav_close_tab(bufmgr, font);
        }

        if (IsKeyPressed(KEY_TAB)) {
            if (is_shift) {
                BufManager_switchtab(bufmgr, PREV);
            } else {
                BufManager_switchtab(bufmgr, NEXT);
            }
        }

        /* -------------------- *
         * CTRL + G (Git Panel)
         * -------------------- */
        if (IsKeyPressed(KEY_G)) {
            if (!git_popup.open) GitPopup_open();
        }
        /* -------------------- *
         * CTRL + K (Render Hover LSP)
         * -------------------- */
        if (g_lsp_ui.enabled && IsKeyPressed(KEY_K)) {
            g_lsp_ui.hover_pending = true;
            g_lsp_ui.has_hover = true;
            g_lsp_ui.hover_scroll = 0.0f;
        }
        /* -------------------- *
         * Save File (CTRL + S)
         * Save As (CTRL + SHIFT + S)
         * -------------------- */
        if (!is_shift && IsKeyPressed(KEY_S)) {
            Nav_save(bufmgr, font);
        } else if (is_shift && IsKeyPressed(KEY_S)) {
            Nav_save_as(bufmgr, font);
        }

        /* -------------------- *
         * CTRL + P (Create Folder)
         * -------------------- */
        if (IsKeyPressed(KEY_P)) {
            Nav_create_folder(bufmgr, font);
        }

        /* -------------------- *
         * CTRL + N (Create New File)
         * -------------------- */
        if (IsKeyPressed(KEY_N)) {
            Nav_create_new_file(bufmgr, font);
        }

        /* -------------------- *
         * 1. Ctrl + Shift + Q (Exit Override / Paksa Keluar)
         * 2. Ctrl + Q biasa (Cek Dirty dulu)
         * -------------------- */
        if (is_shift && IsKeyPressed(KEY_Q)) {
            Notif_show("File yang belum disimpan akan diabaikan!", NOTIF_INFO, 3.0f);
            ExitWindowRequested = true;
        } else if (!is_shift && IsKeyPressed(KEY_Q)) {
            Nav_exit(bufmgr, font);
        }

        /* -------------------- *
         * FILE MANAGER (Ctrl + f)
         * -------------------- */
        if (IsKeyPressed(KEY_F)) {
            bufmgr->show_fm = !bufmgr->show_fm;
        }

        /* -------------------- *
         * Open file
         * -------------------- */
        if (IsKeyPressed(KEY_O)) {
            Nav_open_file(bufmgr, font);
        }

        /* -------------------- *
         * Copy, Cut dan Paste
         * -------------------- */
        if (IsKeyPressed(KEY_C)) {
            Nav_copy(bufmgr, font);
        }

        if (IsKeyPressed(KEY_X)) {
            Nav_cut(bufmgr, font);
        }

        if (IsKeyPressed(KEY_V)) {
            Nav_paste(bufmgr, font);
        }

        /* --------------------- *
         * Undo dan Redo
         * --------------------- */
        if (IsKeyPressed(KEY_Z)) {
            Nav_undo(bufmgr, font);
        }
        if (IsKeyPressed(KEY_R)) {
            Nav_redo(bufmgr, font);
        }

        /* -------------------- *
         * Handling awal Line dan akhir Line
         * -------------------- */
        if (IsKeyPressed(KEY_H)) {
            buf->cursor.x = 0;
            buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y];
        }
        if (IsKeyPressed(KEY_L)) {
            Nav_goto_end_of_line(buf);
        }

        /* -------------------- *
         * Jump ke atas dan ke bawah
         * -------------------- */
        if (IsKeyPressed(KEY_D)) {
            Nav_jump_down(buf);
        }
        if (IsKeyPressed(KEY_U)) {
            Nav_jump_up(buf);
        }
    }

    /* -------------------- *
     * Scroll Cursor
     * -------------------- */
sync_scroll:
    if (!is_mouse_scroll) {
        if ((int)buf->cursor.y < buf->scroll_y) buf->scroll_y = (int)buf->cursor.y;
        if ((int)buf->cursor.y >= buf->scroll_y + visible_lines())
            buf->scroll_y = (int)buf->cursor.y - visible_lines() + 1;
    }
}
