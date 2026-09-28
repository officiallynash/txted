/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
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
#include "lsp.h"
#include "notification.h"
#include "raygui.h"
#include "result.h"
#include "rope.h"
#include "types.h"
#include "ui.h"

extern bool Is_active_menu(void);         // Check if active menu is open (tab.c)
extern void Nav_move_left(Buffer *buf);   // Nav_move_left (nav_utils.c)
extern void Nav_move_right(Buffer *buf);  // Nav_move_right (nav_utils.c)
extern void Nav_mouse_scroll(BufManager *bufmgr, float wheel);  // Nav_mouse_scroll (nav_utils.c)
extern void Nav_goto_end_of_line(Buffer *buf);              // Nav_goto_end_of_line (nav_utils.c)
extern void Nav_jump_down(Buffer *buf, int visible_lines);  // Nav_jump_down (nav_utils.c)
extern void Nav_jump_up(Buffer *buf, int visible_lines);    // Nav_jump_up (nav_utils.c)
extern void Nav_exit(BufManager *bufmgr, Font font);        // Nav_exit (nav_utils.c)
extern void Nav_close_tab(BufManager *bufmgr, Font font);   // Nav_close_tab (nav_utils.c)
extern void Nav_copy(BufManager *bufmgr, Font font);        // Nav_copy (nav_utils.c)
extern void Nav_cut(BufManager *bufmgr, Font font);         // Nav_cut (nav_utils.c)
extern void Nav_paste(BufManager *bufmgr, Font font);       // Nav_paste (nav_utils.c)
extern void Nav_redo(BufManager *bufmgr, Font font);        // Nav_redo (nav_utils.c)
extern void Nav_undo(BufManager *bufmgr, Font font);        // Nav_undo (nav_utils.c)
extern void Nav_move_up(Buffer *buf, int visible_lines);    // Move up (nav_utils.c)
extern void Nav_move_down(Buffer *buf, int visible_lines);  // Move down (nav_utils.c)
extern void prompt_ui_config(BufManager *bufmgr, PromptType type);
extern void Buffer_clamp_scroll(Buffer *buf, int visible_lines);  // Clamp scroll_y

static bool is_mouse_scroll = false;

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
    }                                                   \
    if (HAS_FLAG(buf->buf_flags, BUF_IS_SEARCH)) CLR_FLAG(buf->buf_flags, BUF_IS_SEARCH);

/**
 * Mengatur kursor berdasarkan posisi mouse
 */
static void set_cursor_from_mouse(BufManager *bufmgr, Vector2 mouse, int scroll_y, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf || !buf->lines.offset) return;

    EditorLayout Layout = get_editor_layout(bufmgr);
    int editor_top = Layout.editor_y;  // Pake layout aktual!
    int editor_h = Layout.editor_h;
    float font_size = (float)font.baseSize;

    if (mouse.x >= Layout.text_screen_x && mouse.y >= editor_top + PAD_Y &&
        mouse.y < editor_top + editor_h - PAD_Y) {
        // Hitung Baris Target (Y)
        int rel_y = (int)((mouse.y - (editor_top + PAD_Y)) / LINE_H);
        size_t target_y = (size_t)(scroll_y + rel_y);

        if (target_y >= buf->lines.line_count) {
            target_y = buf->lines.line_count > 0 ? buf->lines.line_count - 1 : 0;
        }

        buf->cursor.y = target_y;

        // Hitung Kolom Target (X)
        char line_text[1024] = {0};
        size_t len = Buffer_get_line_text(buf, target_y, line_text, sizeof(line_text));
        if (len > 0) [[clang::likely]] {
            if (line_text[len - 1] == '\n') line_text[len - 1] = '\0';
            len = strlen(line_text);  // Update si len

            float space_w = MeasureTextEx(font, " ", font_size, 1.0f).x;
            float click_x_rel = mouse.x - Layout.text_screen_x;

            float current_visual_x = 0.0f;
            size_t char_idx = 0;
            int col_visual = 0;

            while (char_idx < len) {
                int codepoint_bytes = 0;
                int codepoint = GetCodepointNext(&line_text[char_idx], &codepoint_bytes);
                if (codepoint_bytes <= 0) break;

                float advance = space_w;
                if (codepoint == '\t') {
                    int spaces = 4 - (col_visual & 3);  // Setara dengan col_visual % 4
                    advance = space_w * spaces;
                    col_visual += spaces;
                } else {
                    char ch[2] = {line_text[char_idx], '\0'};
                    advance = MeasureTextEx(font, ch, font_size, 1.0f).x;
                    col_visual++;
                }

                // Jika klik mouse lebih dekat ke pertengahan karakter ini, berhenti
                if (click_x_rel < current_visual_x + (advance / 2.0f)) {
                    break;
                }

                current_visual_x += advance;
                char_idx++;
            }

            buf->cursor.x = char_idx;
            buf->cursor.cursor_pos = buf->lines.offset[target_y] + buf->cursor.x;

        } else {
            buf->cursor.x = 0;
            buf->cursor.cursor_pos = buf->lines.offset[target_y];
        }
    }
}

/**
 * Fungsi untuk sync Cursor / Tidak di static karena di extern ke buffer.c
 */
void sync_cursor_line_from_pos(Buffer *buf) {
    if (!buf || !buf->lines.offset || buf->lines.line_count == 0) return;

    size_t rope_len = String_len(buf->str);
    // Pastikan cursor_pos berada dalam range buffer yang valid
    if (buf->cursor.cursor_pos > rope_len) {
        buf->cursor.cursor_pos = rope_len;
    }

    size_t pos = buf->cursor.cursor_pos;

    // Binary search untuk mencari upper bound
    size_t low = 0;
    size_t high = buf->lines.line_count;
    while (low < high) {
        size_t mid = (high + low) >> 1;  // Setara dengan low + (high - low) / 2
        if (buf->lines.offset[mid] <= pos) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }

    // low - 1 adalah index-nya
    size_t line = (low > 0) ? (low - 1) : 0;
    buf->cursor.y = line;
    buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[line];
}

/**
 * Fungsi untuk Navigation mouse berbasis Focus mode
 */
static void Update_navigation_click(BufManager *bufmgr, EditorLayout layout) {
    if (bufmgr->mode == POPUP) return;

    // Jika bukan Show FM maka set ke Mode Write
    if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
        bufmgr->mode = WRITE;
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();

        int fm_x = layout.fm_x;
        int fm_w = layout.fm_w;
        int editor_x = layout.editor_x;
        int editor_w = layout.editor_w;
        // Jangan proses kalau klik di area Bar Menu Atas (Tab Bar)
        if (mouse.y <= TAB_H) return;

        // Klik di area File Manager Sidebar
        if (mouse.x >= fm_x && mouse.x < (fm_x + fm_w)) {
            bufmgr->mode = FILE_MANAGER;
        } else if (mouse.x >= editor_x && mouse.x < (editor_x + editor_w)) {
            bufmgr->mode = WRITE;
        }
    }
}

/**
 * Fungsi helper untuk menghitung indentasi eksis di baris saat ini
 */
static int get_current_line_indent(Buffer *buf) {
    if (!buf || buf->cursor.y >= buf->lines.line_count) return 0;

    char line_buf[256] = {0};
    size_t len = Buffer_get_line_text(buf, buf->cursor.y, line_buf, sizeof(line_buf));
    if (len == 0) return 0;

    int visual_indent = 0;
    for (size_t i = 0; i < len; i++) {
        if (line_buf[i] == ' ') {
            visual_indent += 1;
        } else if (line_buf[i] == '\t') {
            visual_indent += 4;
        } else {
            break;  // Berhenti ketika ketemu karakter non-whitespace pertama
        }
    }

    return visual_indent;
}

/**
 * Fungsi untuk Smart Auto Indent dengan Integrasi Tree-sitter indents.scm
 */
static void Syntax_auto_indent(Buffer *active_buf, int visible_lines) {
    if (!active_buf || !active_buf->str) return;

    size_t pos = active_buf->cursor.cursor_pos;
    size_t len = String_len(active_buf->str);
    if (pos > len) pos = len;

    // Ambil Indentasi Eksis Baris Sekarang
    int base_indent = get_current_line_indent(active_buf);

    char prev_char =
        (pos > 0) ? Buffer_get_char_at(active_buf, active_buf->cursor.y, pos - 1) : '\0';

    // space to add itu nilai awalnya base_indent
    int space_to_add = base_indent;

    // Hanya tambah +4 jika karakter tepat sebelum enter adalah blok pemicu!
    if (prev_char == '{' || prev_char == '(' || prev_char == '[' || prev_char == ':') {
        space_to_add += 4;
    }

    if (space_to_add < 0) space_to_add = 0;
    if (space_to_add > 200) space_to_add = 200;

    char insert_str[256] = {0};
    insert_str[0] = '\n';
    memset(insert_str + 1, ' ', space_to_add);
    insert_str[space_to_add + 1] = '\0';

    Buffer_insert(active_buf, pos, insert_str);

    active_buf->cursor.cursor_pos = pos + strlen(insert_str);
    sync_cursor_line_from_pos(active_buf);
    Buffer_clamp_scroll(active_buf, visible_lines);
}

/**
 * Mouse Handling [PUBLIC API]
 */
void handle_mouse_input(BufManager *bufmgr, Font font) {
    if (bufmgr->mode == POPUP) return;

    (void)font;  // Font ga kepake
    if (!bufmgr) return;
    EditorLayout layout = get_editor_layout(bufmgr);
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    // Simpan layout ke state
    int editor_x = layout.editor_x;
    int editor_w = layout.editor_w;

    // penanda apakah lsp aktif
    bool lsp_enable =
        HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) || HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE);

    /* -------------------------------- *
     * Scroll
     * -------------------------------- */
    Vector2 mouse = GetMousePosition();

    // Cek apakah mouse berada di wilayah Editor
    bool is_mouse_in_editor = (mouse.x >= editor_x) && (mouse.x < editor_x + editor_w) &&
                              (mouse.y > TAB_H) && !Is_active_menu();

    float wheel = GetMouseWheelMove();

    // Bool untuk show help
    if (wheel != 0 && !HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_HELP) &&
        !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE)) {  // Kalau ada hover matiin dulu
        if (lsp_enable && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
            // Scroll pilihan popup via mouse wheel!
            if (wheel > 0) {
                g_lsp_ui.selected_index--;
            } else {
                g_lsp_ui.selected_index++;
            }

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
}

/**
 * Input handling [PUBLIC API]
 **/
void handle_input(BufManager *bufmgr, Font font) {
    EditorLayout layout = get_editor_layout(bufmgr);
    // Visible lines
    int visible_lines = layout.visible_lines;

    Update_navigation_click(bufmgr, layout);
    if (bufmgr->mode != WRITE) return;

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    bool is_shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool has_comp = HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);
    bool lsp_enable =
        HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) || HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE);

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
         * CTRL + G (Git Panel)
         * -------------------- */
        if (IsKeyPressed(KEY_G)) {
            bufmgr->mode = POPUP;
            GitPopup_open(bufmgr);
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
            if (!buf->path) {
                prompt_ui_config(bufmgr, PROMPT_TYPE_SAVE);
            } else {
                Buffer_save(buf, nullptr);
            }
        } else if (is_shift && IsKeyPressed(KEY_S)) {
            prompt_ui_config(bufmgr, PROMPT_SAVE_AS);
        }

        /* -------------------- *
         * CTRL + P (Create Folder)
         * -------------------- */
        if (IsKeyPressed(KEY_P)) {
            prompt_ui_config(bufmgr, PROMPT_TYPE_NEW_FOLDER);
        }

        /* -------------------- *
         * CTRL + N (Create New File)
         * -------------------- */
        if (IsKeyPressed(KEY_N)) {
            prompt_ui_config(bufmgr, PROMPT_TYPE_NEW_FILE);
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
            // Jika belum ada flag Show FM set dulu ke FM
            // Ini juga berguna biar ga bolak balik pakai mouse atau touch pad
            if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
                bufmgr->mode = FILE_MANAGER;
                SET_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
            } else {
                // Kalau sudah ada dan masih aktif, setelah open file
                // bisa pencet tombol ini biar kembali ke full bar
                bufmgr->mode = WRITE;
                CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
            }
        }

        /* -------------------- *
         * Fuzzy Search (Ctrl + B)
         * -------------------- */
        if (IsKeyPressed(KEY_B)) {
            prompt_ui_config(bufmgr, PROMPT_TYPE_SEARCH);
        }

        /* -------------------- *
         * Open file
         * -------------------- */
        if (IsKeyPressed(KEY_O)) {
            prompt_ui_config(bufmgr, PROMPT_TYPE_OPEN_FILE);
        }

        /* -------------------- *
         * Copy, Cut dan Paste
         * -------------------- */
        if (IsKeyPressed(KEY_C)) Nav_copy(bufmgr, font);
        if (IsKeyPressed(KEY_X)) Nav_cut(bufmgr, font);
        if (IsKeyPressed(KEY_V)) Nav_paste(bufmgr, font);

        /* --------------------- *
         * Undo dan Redo
         * --------------------- */
        if (IsKeyPressed(KEY_Z)) Nav_undo(bufmgr, font);
        if (IsKeyPressed(KEY_R)) Nav_redo(bufmgr, font);

        /* -------------------- *
         * Handling awal Line dan akhir Line
         * -------------------- */
        if (IsKeyPressed(KEY_H)) {
            buf->cursor.x = 0;
            buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y];
        }
        if (IsKeyPressed(KEY_L)) Nav_goto_end_of_line(buf);

        /* -------------------- *
         * Jump ke atas dan ke bawah
         * -------------------- */
        if (IsKeyPressed(KEY_D)) Nav_jump_down(buf, visible_lines);
        if (IsKeyPressed(KEY_U)) Nav_jump_up(buf, visible_lines);

        /* -------------------- *
         * Pindah ke FM Jika fm aktif
         * --------------------- */
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_LEFT)) {
            if (HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) {
                bufmgr->mode = FILE_MANAGER;
            }
        }
    }

    /*
     * LSP HANDLING
     */
    bool lsp_hand = false;
    if (lsp_enable && has_comp) {
        int total_items =
            (g_lsp_ui.item_count > 0) ? g_lsp_ui.item_count : (int)g_lsp_ui.completion.count;

        if (total_items > 0) {
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
                g_lsp_ui.selected_index++;
                if (g_lsp_ui.selected_index >= total_items) {
                    g_lsp_ui.selected_index = total_items - 1;
                }
                lsp_hand = true;
            } else if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
                g_lsp_ui.selected_index--;
                if (g_lsp_ui.selected_index < 0) {
                    g_lsp_ui.selected_index = 0;
                }
                lsp_hand = true;
            } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                if (g_lsp_ui.filtered && g_lsp_ui.selected_index < g_lsp_ui.item_count &&
                    g_lsp_ui.item_count >= 0) {
                    CompletionItem *selected = g_lsp_ui.filtered[g_lsp_ui.selected_index].item_ptr;
                    if (selected) {
                        lsp_apply_completion(buf, selected);
                    }
                }
                lsp_ui_hide();
                lsp_hand = true;
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                lsp_ui_hide();
                lsp_hand = true;
            }
        }
    }

    if (lsp_hand) {
        goto sync_scroll;
    }

    /* -------------------- *
     * Keyboard Input
     * -------------------- */
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32) {
            char ch[2] = {(char)key, '\0'};

            // Auto pair check (menggunakan utf8_char[0])
            switch (key) {
                case '{':
                    Buffer_insert(buf, buf->cursor.cursor_pos, "{}");
                    buf->cursor.cursor_pos--;
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
                case '[':
                    Buffer_insert(buf, buf->cursor.cursor_pos, "[]");
                    buf->cursor.cursor_pos--;
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
                case '(':
                    Buffer_insert(buf, buf->cursor.cursor_pos, "()");
                    SET_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
                    buf->cursor.cursor_pos--;
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
                case '"':
                    Buffer_insert(buf, buf->cursor.cursor_pos, "\"\"");
                    buf->cursor.cursor_pos--;
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
                case '\'':
                    Buffer_insert(buf, buf->cursor.cursor_pos, "''");
                    buf->cursor.cursor_pos--;
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
                default:
                    Buffer_insert(buf, buf->cursor.cursor_pos, ch);
                    sync_cursor_line_from_pos(buf);  // Sync cursor dengan Pos Rope
                    break;
            }

            Buffer_clamp_scroll(buf, visible_lines);

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
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);
            lsp_free_signature_help(&g_lsp_ui.signature_help);
        }
    }

    /* -------------------- *
     * Handling Enter
     * -------------------- */
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
        Syntax_auto_indent(buf, visible_lines);
    }

    /* -------------------- *
     * Handling Tab
     * -------------------- */
    if (IsKeyPressed(KEY_TAB) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_RIGHT_SHIFT)) {
        Buffer_insert(buf, buf->cursor.cursor_pos, "\t");
        sync_cursor_line_from_pos(buf);
    }

    /* -------------------- *
     * Handling Backspace
     * -------------------- */
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT) || buf->cursor.cursor_pos > 0) {
            Buffer_delete(buf, buf->cursor.cursor_pos);
            lsp_ui_hide();
        }
    }

    /* -------------------- *
     * Handling Left
     * -------------------- */
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
        CHECK_SELECTION();
        Nav_move_left(buf);
    }

    /* -------------------- *
     * Handling Right
     * -------------------- */
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
        CHECK_SELECTION();
        Nav_move_right(buf);
    }

    /* -------------------- *
     * Handling Up
     * -------------------- */
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        CHECK_SELECTION();
        Nav_move_up(buf, visible_lines);
        SIGNATURE_HIDE();  // Auto hide
    }

    /* -------------------- *
     * Handling Down
     * -------------------- */
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        CHECK_SELECTION();
        Nav_move_down(buf, visible_lines);
        SIGNATURE_HIDE();  // Auto hide
    }

    /* -------------------- *
     * Scroll Cursor
     * -------------------- */
sync_scroll:
    if (!is_mouse_scroll) {
        Buffer_clamp_scroll(buf, visible_lines);
    }
}
