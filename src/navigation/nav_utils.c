#include <raylib.h>
#include <stdbool.h>
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
#include "ui.h"

extern int visible_lines();                         // Visible lines (render.c)
extern bool ExitWindowRequested;                    // ExitWindowRequested (main.c)
extern char *format_pretty_path(const char *path);  // (fs.c)

/**
 * Fungsi untuk navigasi atas
 */
void Nav_move_up(Buffer *buf) {
    if (buf->cursor.y > 0) {
        buf->cursor.y--;
        size_t start = buf->lines.offset[buf->cursor.y];
        size_t end = (buf->cursor.y + 1 < buf->lines.line_count)
                         ? buf->lines.offset[buf->cursor.y + 1]
                         : buf->str->len;
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
        size_t start = buf->lines.offset[buf->cursor.y];
        size_t end = (buf->cursor.y + 1 < buf->lines.line_count)
                         ? buf->lines.offset[buf->cursor.y + 1]
                         : buf->str->len;
        size_t max_x = end > start ? end - start : 0;
        if (max_x > 0 && buf->cursor.y + 1 < buf->lines.line_count) max_x--;
        if (buf->cursor.x > max_x) buf->cursor.x = max_x;
        buf->cursor.cursor_pos = start + buf->cursor.x;
    }
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
    if (buf->cursor.cursor_pos < buf->str->len) {
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
void Nav_mouse_scroll(Buffer *buf, float wheel) {
    int total_lines = (int)buf->lines.line_count;
    int max_vis = visible_lines();

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
    if (buf->cursor.y + 1 < buf->lines.line_count) {
        end = buf->lines.offset[buf->cursor.y + 1] - 1;
    } else {
        end = buf->str->len;
    }
    if (end < buf->lines.offset[buf->cursor.y]) end = buf->lines.offset[buf->cursor.y];
    buf->cursor.cursor_pos = end;
    buf->cursor.x = end - buf->lines.offset[buf->cursor.y];
}

/**
 * Fungsi untuk lompat 5 baris ke bawah
 */
void Nav_jump_down(Buffer *buf) {
    if (buf) {
        if (buf->cursor.y + 5 <
            buf->lines.line_count) {  // Selama y + 5 masih di bawah line count, HAJARRRR
            buf->cursor.y += 5;
        } else {  // Kalau tidak cukup line count dikurang 1
            buf->cursor.y = buf->lines.line_count - 1;
        }

        char *text =
            Buffer_get_line_text(buf, buf->cursor.y);  // Pakai line text aja biar mudah HAHAHAHA
        size_t line_len = text ? strlen(text) : 0;

        if (buf->cursor.x > line_len) buf->cursor.x = line_len;

        buf->cursor.cursor_pos = buf->lines.offset[buf->cursor.y] + buf->cursor.x;
        free(text);
    }
}

/**
 * Fungsi untuk Jump ke atas
 */
void Nav_jump_up(Buffer *buf) {
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
    }
}

/**
 * Fungsi untuk membuat folder
 */
void Nav_create_folder(BufManager *bufmgr, Font font) {
    char *cwd = getcwd(NULL, 0);
    if (cwd) {
        char *pretty_name = format_pretty_path(cwd);  // Current directory

        // Pesan
        char msg[128];
        snprintf(msg, sizeof(msg), "Folder name (%s)", pretty_name);
        char *folder_name =
            FloatPrompt_ask(&g_prompt, (const char *)msg, "", ICON_FOLDER, font, bufmgr);

        if (folder_name) {
            int result = mkdir(folder_name, 0777);
            if (result == 0) {
                Notif_show("Folder berhasil dibuat!", NOTIF_SUCCESS, 3.0f);
            } else {
                Notif_show("Gagal membuat folder!", NOTIF_ERROR, 3.0f);
            }
        }

        // Safety free
        free(cwd);
        free(folder_name);
        free(pretty_name);
    }
}

/**
 * Open File
 */
void Nav_open_file(BufManager *bufmgr, Font font) {
    FileList *file_list = FileList_init(128);
    Scan_project_files(".", file_list);

    char *selected =
        FloatPrompt_ask_with_items(&g_prompt, "Open File (Search File)", "", ICON_FILE_OPEN, font,
                                   bufmgr, file_list->items, file_list->item_count);
    if (selected != NULL) {
        BufManager_open(bufmgr, selected);
        free(selected);
    }

    FileList_free(file_list);
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
        ExitWindowRequested = true;
    }
}

/**
 * Create New File
 */
void Nav_create_new_file(BufManager *bufmgr, Font font) {
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) return;
    char *pretty_name = format_pretty_path(cwd);  // Current directory

    // Pesan
    char msg[128];
    snprintf(msg, sizeof(msg), "Nama File baru (%s)", pretty_name);
    char *filename = FloatPrompt_ask(&g_prompt, (const char *)msg, "", ICON_FILE, font, bufmgr);
    if (filename) {
        Result result = Fs_create(filename);
        if (result.type == RESULT_ERR) {
            Notif_show(result.data, NOTIF_ERROR, 3.0f);
        } else {
            BufManager_newtab(bufmgr, result.data);
        }

        free(filename);
        free(cwd);
        free(pretty_name);
        Result_free(&result);
    }
}

/**
 * Save As
 */
void Nav_save_as(BufManager *bufmgr, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);

    char *filename = FloatPrompt_ask(&g_prompt, "Nama File baru", "", ICON_FILE_SAVE, font, bufmgr);
    if (filename) {
        Buffer_save(buf, filename);
        free(filename);
    }
}

/**
 * Save
 */
void Nav_save(BufManager *bufmgr, Font font) {
    Buffer *buf = BufManager_getactive(bufmgr);

    if (buf->path == NULL) {
        char *filename = FloatPrompt_ask(&g_prompt, "Nama File", "", ICON_FILE_SAVE, font, bufmgr);

        if (filename) {
            Buffer_save(buf, filename);
            free(filename);
        }
    } else {
        Buffer_save(buf, NULL);
    }
}

/**
 * Close Tab
 */
void Nav_close_tab(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (buf->is_dirty) {
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
    if (!buf->selection.is_selected) {
        Notif_show("Selection belum aktif!", NOTIF_WARNING, 3.0f);
        return;
    }
    Buffer_copy(buf, bufmgr->clp);
    buf->selection.is_selected = false;
}

/**
 * Fungsi untuk Cut
 */
void Nav_cut(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf->selection.is_selected) {
        Notif_show("Seleksi belum di pilih!", NOTIF_WARNING, 3.0f);
        return;
    }
    Buffer_cut(buf, bufmgr->clp);
    buf->selection.is_selected = false;
}

/**
 * Fungsi untuk Paste
 */
void Nav_paste(BufManager *bufmgr, Font font) {
    (void)font;
    Buffer *buf = BufManager_getactive(bufmgr);

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
