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
#include "editor.h"
#include "fs.h"
#include "notification.h"
#include "raygui.h"
#include "result.h"
#include "rope.h"
#include "ui.h"

extern char *format_pretty_path(const char *path);  // (fs.c)
extern void PromptBuffer_destroy(BufManager *bufmgr);

/**
 * Fungsi untuk navigasi atas
 */
void Nav_move_up(Buffer *buf) {
    if (buf->cursor.y > 0) {
        buf->cursor.y--;
        size_t rope_len = String_len(buf->str);
        size_t start = buf->lines.offset[buf->cursor.y];
        size_t end = (buf->cursor.y + 1 < buf->lines.line_count)
                         ? buf->lines.offset[buf->cursor.y + 1]
                         : rope_len;
        size_t max_x = end > start ? end - start : 0;

        if (max_x > 0 && buf->cursor.y + 1 < buf->lines.line_count) max_x--; /* jangan di atas \n */
        if (buf->cursor.x > max_x) buf->cursor.x = max_x;
        buf->cursor.cursor_pos = start + buf->cursor.x;
    }
}

/**
 * Fungsi untuk navigasi bawah
 */
void Nav_move_down(Buffer *buf) {
    if (buf->cursor.y + 1 < buf->lines.line_count) {
        buf->cursor.y++;
        size_t rope_len = String_len(buf->str);
        size_t start = buf->lines.offset[buf->cursor.y];
        size_t end = (buf->cursor.y + 1 < buf->lines.line_count)
                         ? buf->lines.offset[buf->cursor.y + 1]
                         : rope_len;

        size_t max_x = end > start ? end - start : 0;
        if (max_x > 0 && buf->cursor.y + 1 < buf->lines.line_count) max_x--;
        if (buf->cursor.x > max_x) buf->cursor.x = max_x;
        buf->cursor.cursor_pos = start + buf->cursor.x;
    }
}

/**
 * Fungsi helper untuk scroll y + penyesuaian kursor yang presisi
 */
void scroll_y_nav(BufManager *bufmgr, bool nav_up) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    int total_lines = (int)buf->lines.line_count;
    EditorLayout layout = get_editor_layout(bufmgr);
    int max_vis = layout.visible_lines;

    int max_scroll = total_lines - max_vis;
    if (max_scroll < 0) max_scroll = 0;

    // Geser kursor sesuai arah navigasi
    if (nav_up) {
        Nav_move_up(buf);
    } else {
        Nav_move_down(buf);
    }

    // Adjust Viewport (scroll_y) agar SELALU mengikuti posisi kursor
    if ((int)buf->cursor.y < buf->scroll_y) {
        // Kursor keluar lewat atas viewport
        buf->scroll_y = (int)buf->cursor.y;
    } else if ((int)buf->cursor.y >= buf->scroll_y + max_vis) {
        // Kursor keluar lewat bawah viewport
        buf->scroll_y = (int)buf->cursor.y - max_vis + 1;
    }

    // Clamp scroll_y agar tidak membal / out of bounds
    if (buf->scroll_y < 0) buf->scroll_y = 0;
    if (buf->scroll_y > max_scroll) buf->scroll_y = max_scroll;
}

/**
 * Fungsi untuk navigasi kiri
 */
void Nav_move_left(Buffer *buf) {
    if (buf->cursor.cursor_pos > 0) {
        buf->cursor.cursor_pos--;
        size_t y = 0;
        while (y + 1 < buf->lines.line_count && buf->lines.offset[y + 1] <= buf->cursor.cursor_pos)
            y++;
        buf->cursor.y = y;
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
    }
}

/**
 * Fungsi untuk navigasi kanan
 */
void Nav_move_right(Buffer *buf) {
    size_t rope_len = String_len(buf->str);
    if (buf->cursor.cursor_pos < rope_len) {
        buf->cursor.cursor_pos++;
        size_t y = 0;
        while (y + 1 < buf->lines.line_count && buf->lines.offset[y + 1] <= buf->cursor.cursor_pos)
            y++;
        buf->cursor.y = y;
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
    }
}

/**
 * Fungsi untuk scroll mouse
 */
void Nav_mouse_scroll(BufManager *bufmgr, float wheel) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;

    // Ambil total lines
    int total_lines = (int)buf->lines.line_count;
    EditorLayout layout = get_editor_layout(bufmgr);

    int max_vis = layout.visible_lines;

    // Pastikan max_scroll tidak pernah minus
    int max_scroll = total_lines - max_vis;
    if (max_scroll < 0) max_scroll = 0;

    buf->scroll_y -= (int)(wheel * 3);

    // Clamp scroll_y di rentang [0, max_scroll]
    if (buf->scroll_y < 0) buf->scroll_y = 0;
    if (buf->scroll_y > max_scroll) buf->scroll_y = max_scroll;

    int top_visible = buf->scroll_y;
    int bottom_visible = buf->scroll_y + max_vis - 1;

    // Jaga kursor agar tetap berada di dalam area visible
    if (buf->cursor.y < (size_t)top_visible) {
        buf->cursor.y = (size_t)top_visible;
    } else if (buf->cursor.y > (size_t)bottom_visible) {
        buf->cursor.y = (size_t)bottom_visible;
    }

    // Protection ekstra agar kursor tidak melebihi total baris aktual
    if (buf->cursor.y >= (size_t)total_lines && total_lines > 0) {
        buf->cursor.y = (size_t)(total_lines - 1);
    }
}

/**
 * Fungsi untuk Navigasi ke akhir baris
 */
void Nav_goto_end_of_line(Buffer *buf) {
    size_t end;
    size_t rope_len = String_len(buf->str);

    if (buf->cursor.y + 1 < buf->lines.line_count) {
        end = buf->lines.offset[buf->cursor.y + 1] - 1;
    } else {
        end = rope_len;
    }
    if (end < buf->lines.offset[buf->cursor.y]) end = buf->lines.offset[buf->cursor.y];
    buf->cursor.cursor_pos = end;
    buf->cursor.x = end - buf->lines.offset[buf->cursor.y];
}

/**
 * Fungsi untuk lompat 5 baris ke bawah
 */
void Nav_jump_down(BufManager *bufmgr) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf) {
        if (buf->cursor.y + 5 < buf->lines.line_count) {
            // Selama y + 5 masih di bawah line count, HAJARRRR
            buf->cursor.y += 5;
        } else {  // Kalau tidak cukup line count dikurang 1
            buf->cursor.y = buf->lines.line_count - 1;
        }

        // Pakai line text aja biar mudah HAHAHAHA
        char *text = Buffer_get_line_text(buf, buf->cursor.y);
        size_t line_len = text ? strlen(text) : 0;

        if (buf->cursor.x > line_len) buf->cursor.x = line_len;

        buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y] + buf->cursor.x;
        free(text);

        // Sinkronasi dengan viewport
        EditorLayout layout = get_editor_layout(bufmgr);
        int max_vis = layout.visible_lines;
        int total_lines = (int)buf->lines.line_count;
        int max_scroll = (total_lines > max_vis) ? (total_lines - max_vis) : 0;

        // Jika kursor melompat melebihi batas bawah viewport
        if ((int)buf->cursor.y >= buf->scroll_y + max_vis) {
            buf->scroll_y = (int)buf->cursor.y - max_vis + 1;
        }

        if (buf->scroll_y > max_scroll) buf->scroll_y = max_scroll;
        if (buf->scroll_y < 0) buf->scroll_y = 0;
    }
}

/**
 * Fungsi untuk Jump ke atas
 */
void Nav_jump_up(BufManager *bufmgr) {
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf) {
        if (buf->cursor.y <= 5) {  // Jika kurang atau sama dengan 5 langsung set 0 aja
            buf->cursor.y = 0;
        } else {  // Selebihnya biasa
            buf->cursor.y -= 5;
        }

        char *text = Buffer_get_line_text(buf, buf->cursor.y);
        size_t line_len = text ? strlen(text) : 0;

        if (buf->cursor.x > line_len) buf->cursor.x = line_len;

        buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y] + buf->cursor.x;
        free(text);

        // Sinkronasi dengan Viewport
        if ((int)buf->cursor.y < buf->scroll_y) {
            buf->scroll_y = (int)buf->cursor.y;
        }

        if (buf->scroll_y < 0) buf->scroll_y = 0;
    }
}

/**
 * Fungsi untuk membuat folder
 */
void Nav_create_folder(BufManager *bufmgr, char *folder_name) {
    char *cwd = getcwd(nullptr, 0);
    if (cwd) {
        // Guard jika nullptr atau asal pencet
        if (!folder_name) {
            Notif_show("Tidak ada nama Folder!", NOTIF_INFO, 3.0f);
            bufmgr->mode = WRITE;
            return;
        }

        int result = mkdir(folder_name, 0777);
        if (result == 0) {
            Notif_show("Folder berhasil dibuat!", NOTIF_SUCCESS, 3.0f);
        } else {
            Notif_show("Gagal membuat folder!", NOTIF_ERROR, 3.0f);
        }

        bufmgr->mode = WRITE;
        file_manager_refresh();
        // Safety free
        free(cwd);
        free(folder_name);
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
        Notif_show(
            "Masih ada buffer yang belum di save!\nGunakan Ctrl+Shift+Q untuk paksa "
            "keluar.",
            NOTIF_WARNING, 4.0f);
    } else {
        // Minta request Exit melalui Buffer Manager
        SET_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT);
    }
}

/**
 * Create New File
 */
void Nav_create_new_file(BufManager *bufmgr, char *filename) {
    char *cwd = getcwd(nullptr, 0);
    if (cwd == nullptr) return;

    if (!filename) {
        // Guard jika kepencet enter
        Notif_show("Nama file tidak boleh kosong!", NOTIF_INFO, 3.0f);
        bufmgr->mode = WRITE;
        return;
    }

    Result result = Fs_create(filename);
    if (result.type == RESULT_ERR) {
        Notif_show(result.data, NOTIF_ERROR, 3.0f);
    } else {
        BufManager_newtab(bufmgr, result.data);
    }

    // Free semua Heap
    free(filename);
    free(cwd);
    Result_free(&result);
    // Ini diluar aja.
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

    // Default ke mode Write
    Buffer_save(buf, filename);
    free(filename);
    bufmgr->mode = WRITE;
}

/**
 * Save
 */
void Nav_save(BufManager *bufmgr, char *filename) {
    Buffer *buf = BufManager_getactive(bufmgr);

    // Jika kepencet mending kasih notif
    if (!filename) {
        Notif_show("Nama file tidak boleh kosong!", NOTIF_INFO, 3.0f);
        bufmgr->mode = WRITE;
        return;
    }

    Buffer_save(buf, filename);
    free(filename);

    // Default
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
 * Fungsi untuk Copy
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
 * Fungsi untuk Cut
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
 * Fungsi untuk Paste
 */
void Nav_paste(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);

    if (!buf) return;
    Buffer_paste(buf, bufmgr->clp);
}

/**
 * Fungsi untuk Redo
 */
void Nav_redo(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;
    Buffer_redo(buf);
}

/**
 * Fungsi untuk Undo
 */
void Nav_undo(BufManager *bufmgr, Font font) {
    (void)font;

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) return;
    Buffer_undo(buf);
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

    const char *label = "";
    switch (type) {
        case PROMPT_TYPE_SAVE: {
            bufmgr->prompt->icon_id = ICON_FILE_SAVE;
            label = "Nama file:";
            break;
        }
        case PROMPT_TYPE_OPEN_FILE: {
            bufmgr->prompt->icon_id = ICON_FILE_OPEN;
            label = "Open file:";
            break;
        }
        case PROMPT_TYPE_NEW_FILE: {
            bufmgr->prompt->icon_id = ICON_FILE;
            label = "New File:";
            break;
        }
        case PROMPT_SAVE_AS: {
            bufmgr->prompt->icon_id = ICON_FILE_SAVE;
            label = "Nama file baru:";
            break;
        }
        case PROMPT_TYPE_SEARCH: {
            bufmgr->prompt->icon_id = ICON_LENS_BIG;
            label = "Search:";
            break;
        }
        case PROMPT_TYPE_NEW_FOLDER: {
            bufmgr->prompt->icon_id = ICON_FOLDER_ADD;
            label = "Nama folder:";
            break;
        }
    }

    snprintf(bufmgr->prompt->label, sizeof(bufmgr->prompt->label), "%s", label);

    // Alokasi fresh
    bufmgr->prompt->prb = PromptBuffer_init();
}

void FloatPrompt_execute(BufManager *bufmgr, char *text) {
    if (bufmgr->prompt->type == PROMPT_TYPE_NEW_FILE) {
        Nav_create_new_file(bufmgr, text);
    } else if (bufmgr->prompt->type == PROMPT_TYPE_SAVE) {
        Nav_save(bufmgr, text);
    } else if (bufmgr->prompt->type == PROMPT_SAVE_AS) {
        Nav_save_as(bufmgr, text);
    } else if (bufmgr->prompt->type == PROMPT_TYPE_NEW_FOLDER) {
        Nav_create_folder(bufmgr, text);
    } else if (bufmgr->prompt->type == PROMPT_TYPE_OPEN_FILE) {
        Nav_open_file(bufmgr, text);
    }

    // State akhir
    bufmgr->prompt->is_active = false;
    bufmgr->prompt->edit_mode = false;
};
