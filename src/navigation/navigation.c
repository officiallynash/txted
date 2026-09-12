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
#include "lsp_server.h"
#include "lsp_ui.h"
#include "notification.h"
#include "raygui.h"
#include "result.h"
#include "ui.h"

extern bool Is_active_menu(void);  // Check if active menu is open (tab.c)
extern int calculate_score(const char *query, const char *label);  // Extern (lsp_ui.c)
extern void set_cursor_from_mouse(BufManager *bufmgr, Vector2 mouse, int scroll_y,
                                  Font font);        // set_cursor_from_mouse (nav_helper.c)
extern void sync_cursor_line_from_pos(Buffer *buf);  // sync_cursor_line_from_pos (nav_helper.c)
extern void Syntax_auto_indent(Buffer *active_buf);  // Syntax_auto_indent (nav_helper.c)

extern void Nav_move_up(Buffer *buf);                           // Nav_move_up (nav_utils.c)
extern void Nav_move_down(Buffer *buf);                         // Nav_move_down (nav_utils.c)
extern void Nav_move_left(Buffer *buf);                         // Nav_move_left (nav_utils.c)
extern void Nav_move_right(Buffer *buf);                        // Nav_move_right (nav_utils.c)
extern void Nav_mouse_scroll(BufManager *bufmgr, float wheel);  // Nav_mouse_scroll (nav_utils.c)
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

// helper untuk hide Signature Help (pakai macro aja kali ya HAHAHA)
#define SIGNATURE_HIDE()                                                                \
    if (g_lsp_ui.sig_y != buf->cursor.y || HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE)) { \
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);                                       \
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);                                      \
        g_lsp_ui.sig_y = 0;                                                             \
        lsp_free_signature_help(&g_lsp_ui.signature_help);                              \
    }

// Helper macro/lambda kecil internal
#define CHECK_SELECTION()                               \
    if (is_shift) {                                     \
        if (!HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) { \
            buf->start = buf->cursor.cursor_pos;        \
            SET_FLAG(buf->buf_flags, BUF_IS_SELECT);    \
        }                                               \
    } else {                                            \
        CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);        \
    }

/**
 * Fungsi untuk Navigation mouse berbasis Focus mode
 */
static void Update_navigation_click(BufManager *bufmgr) {
    // Jika bukan Show FM maka set ke Mode Write
    if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
        CLR_FLAG(bufmgr->win_flags, TXTED_FILE_MANAGER);  /// Matiin dulu File Manager
        SET_FLAG(bufmgr->win_flags, TXTED_WRITE);
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        EditorLayout Layout = get_editor_layout(bufmgr);

        // Jangan proses kalau klik di area Bar Menu Atas (Tab Bar)
        if (mouse.y <= TAB_H) return;

        // Klik di area File Manager Sidebar
        if (mouse.x >= Layout.fm_x && mouse.x < (Layout.fm_x + Layout.fm_w)) {
            CLR_FLAG(bufmgr->win_flags, TXTED_WRITE);         // Matiin dulu si Write
            SET_FLAG(bufmgr->win_flags, TXTED_FILE_MANAGER);  // set ke FM
        }

        // Klik di area Write / Text Editor
        else if (mouse.x >= Layout.editor_x && mouse.x < (Layout.editor_x + Layout.editor_w)) {
            CLR_FLAG(bufmgr->win_flags, TXTED_FILE_MANAGER);  // Matiin dulu si File Manager
            SET_FLAG(bufmgr->win_flags, TXTED_WRITE);         // Set default ke Write
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

    EditorLayout layout = get_editor_layout(bufmgr);

    bool lsp_enable =
        HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) || HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE);

    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

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

    // Bool untuk show help
    if (wheel != 0 && !HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_HELP) &&
        !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE)) {  // Kalau ada hover matiin dulu
        if (lsp_enable && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
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
            Nav_mouse_scroll(bufmgr, wheel);
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
            buf->start = buf->cursor.cursor_pos;
            CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);  // Belum ter-select sebelum digeser
        }
        // KLIK KIRI DITAHAN DAN DIGESER / DRAG
        else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            // Update kursor ke posisi mouse yang baru saat di-drag
            set_cursor_from_mouse(bufmgr, mouse, buf->scroll_y, font);

            // Jika posisi kursor bergeser dari titik awal -> NYALAKAN SELECTION!
            // Tambahan jika sedang Drag scroll bar, maka Selection tidak aktif
            if (!HAS_FLAG(buf->buf_flags, BUF_IS_DRAGGING) &&
                buf->cursor.cursor_pos != buf->start) {
                SET_FLAG(buf->buf_flags, BUF_IS_SELECT);
            } else {
                CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
            }
        }
    }

    /* -------------------- *
     * Keyboard Input
     * -------------------- */
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32) {
            char utf8[8] = {0};
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
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
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
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
                g_lsp_ui.sig_y = buf->cursor.y;  // Simpan Y untuk auto close
            } else if (key == ')' || key == ';') {
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);
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

    if (lsp_enable && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
        char current_word[256] = {0};
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
        if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING) ||
            HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE)) {
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING);
            lsp_free_hover(&g_lsp_ui.hover);
        }
        // Escape untuk batalkan Selection
        if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
        // Escape untuk menutup Signature
        if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG) ||
            HAS_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING)) {
            HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);
            HAS_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
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
        if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT) || buf->cursor.cursor_pos > 0) {
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
        if (IsKeyPressed(KEY_T)) BufManager_newtab(bufmgr, nullptr);

        if (is_shift && IsKeyPressed(KEY_W)) {
            CLR_FLAG(buf->buf_flags, BUF_IS_DIRTY);
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
         * CTRL + K (Render Hover LSP)
         * -------------------- */
        if (lsp_enable && IsKeyPressed(KEY_K)) {
            SET_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING);
            SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
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
            SET_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT);
        } else if (!is_shift && IsKeyPressed(KEY_Q)) {
            Nav_exit(bufmgr, font);
        }

        /* -------------------- *
         * FILE MANAGER (Ctrl + f)
         * -------------------- */
        if (IsKeyPressed(KEY_F)) {
            if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
                SET_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
            } else {
                CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
            }
        }

        /* -------------------- *
         * Fuzzy Search (Ctrl + B)
         * -------------------- */
        if (IsKeyPressed(KEY_B)) {
            SearchPrompt_ask(bufmgr, font);
            return;
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
        if ((int)buf->cursor.y >= buf->scroll_y + layout.visible_lines)
            buf->scroll_y = (int)buf->cursor.y - layout.visible_lines + 1;
    }
}
