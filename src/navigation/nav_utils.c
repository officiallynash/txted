/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "fs.h"
#include "notification.h"
#include "raygui.h"
#include "result.h"
#include "rope.h"
#include "ui.h"

extern char *format_pretty_path(const char *path);  // (fs.c)

/**
 * Helper internal untuk update koordinat (x, y) kursor berdasarkan cursor_pos
 */
static void sync_cursor_coords_from_pos(Buffer *buf) {
    size_t y = 0;
    while (y + 1 < buf->lines.line_count && buf->lines.offset[y + 1] <= buf->cursor.cursor_pos) {
        y++;
    }

    buf->cursor.y = y;
    buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
}

/**
 * Helper internal untuk clamp x dan kalkulasi pos setelah pindah baris
 */
static void sync_cursor_pos_from_coords(Buffer *buf) {
    char text[1024];
    size_t line_len = Buffer_get_line_text(buf, buf->cursor.y, text, sizeof(text));
    if (buf->cursor.x > line_len) buf->cursor.x = line_len;
    buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y] + buf->cursor.x;
}

/**
 * Helper untuk Scroll_y
 */
void Buffer_clamp_scroll(Buffer *buf, int visible_lines) {
    if (!buf) return;
    int total_lines = (int)buf->lines.line_count;
    int max_scroll = total_lines - visible_lines;

    if (max_scroll < 0) max_scroll = 0;

    if ((int)buf->cursor.y >= buf->scroll_y + visible_lines) {
        buf->scroll_y = (int)buf->cursor.y - visible_lines + 1;
    }
    if ((int)buf->cursor.y < buf->scroll_y) {
        buf->scroll_y = (int)buf->cursor.y;
    }

    // Buat jaga-jaga kalau ada case spesial
    if (buf->scroll_y < 0) buf->scroll_y = 0;
    if (buf->scroll_y > max_scroll) buf->scroll_y = max_scroll;
}

/**
 * Navigasi Atas
 */
void Nav_move_up(Buffer *buf, int visible_lines) {
    if (buf->cursor.y > 0) {
        buf->cursor.y--;
        sync_cursor_pos_from_coords(buf);
        Buffer_clamp_scroll(buf, visible_lines);
    }
}

/**
 * Navigasi Bawah
 */
void Nav_move_down(Buffer *buf, int visible_lines) {
    if (buf->cursor.y + 1 < buf->lines.line_count) {
        buf->cursor.y++;
        sync_cursor_pos_from_coords(buf);
        Buffer_clamp_scroll(buf, visible_lines);
    }
}

/**
 * Navigasi Kiri
 */
void Nav_move_left(Buffer *buf) {
    if (buf->cursor.cursor_pos > 0) {
        buf->cursor.cursor_pos--;
        sync_cursor_coords_from_pos(buf);
    }
}

/**
 * Navigasi Kanan
 */
void Nav_move_right(Buffer *buf) {
    size_t rope_len = String_len(buf->str);
    if (buf->cursor.cursor_pos < rope_len) {
        buf->cursor.cursor_pos++;
        sync_cursor_coords_from_pos(buf);
    }
}

/**
 * Scroll Mouse
 */
void Nav_mouse_scroll(BufManager *bufmgr, float wheel) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    int total_lines = (int)buf->lines.line_count;
    EditorLayout layout = get_editor_layout(bufmgr);
    int max_vis = layout.visible_lines;

    int max_scroll = total_lines - max_vis;
    if (max_scroll < 0) max_scroll = 0;

    buf->scroll_y -= (int)(wheel * 3);

    if (buf->scroll_y < 0) buf->scroll_y = 0;
    if (buf->scroll_y > max_scroll) buf->scroll_y = max_scroll;

    int top_visible = buf->scroll_y;
    int bottom_visible = buf->scroll_y + max_vis - 1;

    if (buf->cursor.y < (size_t)top_visible) {
        buf->cursor.y = (size_t)top_visible;
    } else if (buf->cursor.y > (size_t)bottom_visible) {
        buf->cursor.y = (size_t)bottom_visible;
    }

    if (buf->cursor.y >= (size_t)total_lines && total_lines > 0) {
        buf->cursor.y = (size_t)(total_lines - 1);
    }
}

/**
 * Navigasi ke akhir baris
 */
void Nav_goto_end_of_line(Buffer *buf) {
    size_t end = (buf->cursor.y + 1 < buf->lines.line_count)
                     ? buf->lines.offset[buf->cursor.y + 1] - 1
                     : String_len(buf->str);

    if (end < buf->lines.offset[buf->cursor.y]) {
        end = buf->lines.offset[buf->cursor.y];
    }

    buf->cursor.cursor_pos = end;
    buf->cursor.x = end - buf->lines.offset[buf->cursor.y];
}

/**
 * Lompat 5 baris ke bawah
 */
void Nav_jump_down(Buffer *buf, int visible_lines) {
    if (!buf) return;

    if (buf->cursor.y + 5 < buf->lines.line_count) {
        buf->cursor.y += 5;
    } else {
        buf->cursor.y = buf->lines.line_count - 1;
    }

    sync_cursor_pos_from_coords(buf);
    Buffer_clamp_scroll(buf, visible_lines);
}

/**
 * Jump 5 baris ke atas
 */
void Nav_jump_up(Buffer *buf, int visible_lines) {
    if (!buf) return;

    buf->cursor.y = (buf->cursor.y <= 5) ? 0 : buf->cursor.y - 5;

    sync_cursor_pos_from_coords(buf);
    Buffer_clamp_scroll(buf, visible_lines);
}

/**
 * Membuat folder
 */
void Nav_create_folder(BufManager *bufmgr, char *folder_name) {
    char *cwd = getcwd(nullptr, 0);
    if (cwd) {
        if (!folder_name) {
            Notif_show("Tidak ada nama Folder!", NOTIF_INFO, 3.0f);
            bufmgr->mode = WRITE;
            free(cwd);
            return;
        }

        if (mkdir(folder_name, 0777) == 0) {
            Notif_show("Folder berhasil dibuat!", NOTIF_SUCCESS, 3.0f);
        } else {
            Notif_show("Gagal membuat folder!", NOTIF_ERROR, 3.0f);
        }

        bufmgr->mode = WRITE;
        file_manager_refresh();

        free(folder_name);
        free(cwd);
    }
}

/**
 * Open File
 */
void Nav_open_file(BufManager *bufmgr, char *filename) {
    BufManager_open(bufmgr, filename);
    free(filename);
    bufmgr->mode = WRITE;
}

/**
 * Exit
 */
void Nav_exit(BufManager *bufmgr, Font font) {
    (void)font;
    if (BufManager_checkdirty(bufmgr) > 0) {
        Notif_show("Masih ada buffer yang belum di save!\nGunakan Ctrl+Shift+Q untuk paksa keluar.",
                   NOTIF_WARNING, 4.0f);
    } else {
        SET_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT);
    }
}

/**
 * Create New File
 */
void Nav_create_new_file(BufManager *bufmgr, char *filename) {
    char *cwd = getcwd(nullptr, 0);
    if (!cwd) return;

    if (!filename) {
        Notif_show("Nama file tidak boleh kosong!", NOTIF_INFO, 3.0f);
        bufmgr->mode = WRITE;
        free(cwd);
        return;
    }

    Result result = Fs_create(filename);
    if (result.type == RESULT_ERR) {
        Notif_show(result.data, NOTIF_ERROR, 3.0f);
    } else {
        BufManager_newtab(bufmgr, result.data);
    }

    free(filename);
    free(cwd);
    Result_free(&result);
    bufmgr->mode = WRITE;
}

/**
 * Save As
 */
void Nav_save_as(BufManager *bufmgr, char *filename) {
    Buffer *buf = BufManager_getactive(bufmgr);

    if (!filename) {
        Notif_show("Nama file tidak boleh kosong!", NOTIF_INFO, 3.0f);
        bufmgr->mode = WRITE;
        return;
    }

    Buffer_save(buf, filename);
    free(filename);
    bufmgr->mode = WRITE;
}

/**
 * Save
 */
void Nav_save(BufManager *bufmgr, char *filename) {
    Buffer *buf = BufManager_getactive(bufmgr);

    if (!filename) {
        Notif_show("Nama file tidak boleh kosong!", NOTIF_INFO, 3.0f);
        bufmgr->mode = WRITE;
        return;
    }

    Buffer_save(buf, filename);
    free(filename);
    bufmgr->mode = WRITE;
}

/**
 * Close Tab
 */
void Nav_close_tab(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (HAS_FLAG(buf->buf_flags, BUF_IS_DIRTY)) {
        Notif_show("Simpan Buffer dahulu! \nCtrl+Shift+W untuk paksa tutup!", NOTIF_INFO, 3.0f);
        return;
    }
    BufManager_closetab(bufmgr);
}

/**
 * Copy
 */
void Nav_copy(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
        Notif_show("Selection belum aktif!", NOTIF_WARNING, 3.0f);
        return;
    }
    Buffer_copy(buf, bufmgr->clp);
    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
}

/**
 * Cut
 */
void Nav_cut(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
        Notif_show("Seleksi belum di pilih!", NOTIF_WARNING, 3.0f);
        return;
    }
    Buffer_cut(buf, bufmgr->clp);
    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
}

/**
 * Paste
 */
void Nav_paste(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf) Buffer_paste(buf, bufmgr->clp);
}

/**
 * Redo
 */
void Nav_redo(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf) Buffer_redo(buf);
}

/**
 * Undo
 */
void Nav_undo(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf) Buffer_undo(buf);
}

/**
 * Config untuk Float Prompt
 */
void prompt_ui_config(BufManager *bufmgr, PromptType type) {
    bufmgr->mode = POPUP;
    bufmgr->prompt->is_active = true;
    bufmgr->prompt->edit_mode = true;
    bufmgr->prompt->selected_idx = 0;
    bufmgr->prompt->type = type;

    char *pretty_cwd = bufmgr->path_root ? format_pretty_path(bufmgr->path_root) : strdup("Root");

    switch (type) {
        case PROMPT_TYPE_SAVE:
            bufmgr->prompt->icon_id = ICON_FILE_SAVE;
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "Filename (%s)",
                     pretty_cwd);
            break;
        case PROMPT_TYPE_OPEN_FILE:
            bufmgr->prompt->icon_id = ICON_FILE_OPEN;
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "Open file (%s)",
                     pretty_cwd);
            break;
        case PROMPT_TYPE_NEW_FILE:
            bufmgr->prompt->icon_id = ICON_FILE;
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "New file (%s)",
                     pretty_cwd);
            break;
        case PROMPT_SAVE_AS:
            bufmgr->prompt->icon_id = ICON_FILE_SAVE;
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "New filename (%s)",
                     pretty_cwd);
            break;
        case PROMPT_TYPE_SEARCH: {
            bufmgr->prompt->icon_id = ICON_LENS_BIG;
            Buffer *buf = BufManager_getactive(bufmgr);
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "Search (%s)",
                     buf->filename);
            break;
        }
        case PROMPT_TYPE_NEW_FOLDER:
            bufmgr->prompt->icon_id = ICON_FOLDER_ADD;
            snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "New folder name (%s)",
                     pretty_cwd);
            break;
    }

    bufmgr->prompt->prb = PromptBuffer_init();

    if (pretty_cwd) free(pretty_cwd);
}

/**
 * Execute dari Float Prompt
 */
void FloatPrompt_execute(BufManager *bufmgr, char *text) {
    switch (bufmgr->prompt->type) {
        case PROMPT_TYPE_NEW_FILE:
            Nav_create_new_file(bufmgr, text);
            break;
        case PROMPT_TYPE_SAVE:
            Nav_save(bufmgr, text);
            break;
        case PROMPT_SAVE_AS:
            Nav_save_as(bufmgr, text);
            break;
        case PROMPT_TYPE_NEW_FOLDER:
            Nav_create_folder(bufmgr, text);
            break;
        case PROMPT_TYPE_OPEN_FILE:
            Nav_open_file(bufmgr, text);
            break;
        default:
            break;
    }

    bufmgr->prompt->is_active = false;
    bufmgr->prompt->edit_mode = false;
}
