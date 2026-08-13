#include "buffer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fs.h"
#include "git_client.h"
#include "lsp_config.h"
#include "lsp_server.h"
#include "lsp_ui.h"
#include "notification.h"
#include "result.h"
#include "rope.h"
#include "syntax.h"
#include "undo.h"

extern void sync_cursor_line_from_pos(Buffer *buf);  // didefinisikan di navigation.c
extern void lsp_clear_all_diagnostics(void);         // Clear Diagnostic [lsp_client.c]

/* =============================
 * PRIVATE API
 * ============================= */

/**
 * Line Index Init [PRIVATE API]
 */
LineIndex LineIndex_init() {
    LineIndex li;
    li.capacity = 32;
    li.offset = malloc(sizeof(size_t) * li.capacity);
    li.line_count = 1;
    li.offset[0] = 0;

    return li;
}

/**
 * Line Index Insert [PRIVATE API]
 */
void LineIndex_insert(LineIndex *li, const char *data, size_t len) {
    if (!li || !li->offset || !data) return;

    // Set internal dulu, pastikan kosong
    li->offset[0] = 0;
    li->line_count = 1;

    // Masuk ke perulangan teks sesuai dengan len
    for (size_t i = 0; i < len; i++) {
        if (data[i] != '\n') continue;  // Jika bukan '\n' lanjut

        if (li->line_count >= li->capacity) {
            size_t new_cap = li->capacity ? li->capacity * 2 : 32;
            size_t *new_offset = realloc(li->offset, sizeof(size_t) * new_cap);
            if (!new_offset) return;  // Guard jika realloc gagal

            li->offset = new_offset;
            li->capacity = new_cap;
        }
        li->offset[li->line_count++] = i + 1;
    }
}

/**
 * Line Index insert (\n) [PRIVATE API]
 */
void LineIndex_insert_newline(LineIndex *li, size_t line_idx, size_t newline_pos) {
    if (!li || !li->offset) return;

    // Alokasi ulang memori jika kapasitas penuh
    if (li->line_count >= li->capacity) {
        size_t new_cap = li->capacity ? li->capacity * 2 : 32;
        size_t *new_offset = realloc(li->offset, sizeof(size_t) * new_cap);
        if (!new_offset) return;  // Guard alokasi memori

        li->offset = new_offset;
        li->capacity = new_cap;
    }

    // Geser offset elemen di belakangnya jika baris disisipkan di tengah-tengah
    if (line_idx + 1 < li->line_count) {
        size_t line_to_move = li->line_count - (line_idx + 1);
        memmove(&li->offset[line_idx + 2], &li->offset[line_idx + 1],
                line_to_move * sizeof(size_t));
    }

    // Isikan posisi offset baris baru
    li->offset[line_idx + 1] = newline_pos + 1;
    li->line_count++;
}

/**
 * Fungsi untuk mengambil start dan panjang teks di seleksi [PRIVATE API]
 */
void Get_selected_position(Buffer *buf, size_t *start, size_t *len) {
    size_t sel_start = buf->selection.start;
    size_t current = buf->cursor.cursor_pos;

    // Logika min dan max
    size_t sel_min = (sel_start < current) ? sel_start : current;
    size_t sel_max = (sel_start > current) ? sel_start : current;

    // mencari len
    *len = sel_max - sel_min;
    *start = sel_min;

    if (*len == 0) {
        buf->selection.is_selected = false;
        return;
    }
}

/**
 * Fungsi untuk sinkronisasi tree sitter [PRIVATE API]
 */
void sync_syntax_tree(Buffer *buf) {
    if (!buf || !buf->state) return;

    Bytes full_text = String_get(buf->str, 0, buf->str->len);
    if (full_text.data) {
        if (buf->state->tree) {
            ts_tree_delete(buf->state->tree);
        }
        buf->state->tree = ts_parser_parse_string(
            buf->state->parser, NULL, (const char *)full_text.data, (uint32_t)buf->str->len);
        Bytes_free(&full_text);
    }
}

/**
 * Helper untuk mengambil Nama file dari Path [PRIVATE API]
 */
char *get_display_name(const char *filepath) {
    if (!filepath) return "Untilted";
    const char *slash = strrchr(filepath, '/');

#if defined(_WIN32)
    if (!slash) slash = strrchr(filepath, '\\');
#endif

    return strdup(slash ? (slash + 1) : filepath);
}

/**
 * Helper position to Offset [PRIVATE API]
 */
static size_t position_to_offset(Buffer *buf, int line, int character) {
    if (!buf || line < 0) return 0;
    if ((size_t)line >= buf->lines.line_count) return buf->str->len;

    size_t line_start = buf->lines.offset[line];
    size_t line_end =
        (line + 1 < (int)buf->lines.line_count) ? buf->lines.offset[line + 1] : buf->str->len;

    size_t offset = line_start + (size_t)character;
    if (offset > line_end) offset = line_end;
    if (offset > buf->str->len) offset = buf->str->len;
    return offset;
}

/**
 * Fungsi untuk Apply Auto Format [PRIVATE API]
 */
bool lsp_apply_text_edits(Buffer *buf, TextEditList *edits) {
    if (!buf || !edits || edits->count == 0) return false;

    // Apply dari belakang biar offset tidak bergeser
    for (int i = (int)edits->count - 1; i >= 0; i--) {
        TextEdit *te = &edits->edits[i];

        size_t start = position_to_offset(buf, te->start_line, te->start_char);
        size_t end = position_to_offset(buf, te->end_line, te->end_char);

        if (end < start) {
            size_t tmp = start;
            start = end;
            end = tmp;
        }

        size_t delete_len = end - start;

        // Hapus range lama
        if (delete_len > 0) {
            // Buffer_delete kamu menghapus 1 karakter / selection.
            // Lebih aman pakai String_delete langsung kalau ada.
            String_delete(&buf->str, start, delete_len);
        }

        // Insert teks baru
        if (te->new_text && te->new_text[0] != '\0') {
            String_insert(&buf->str, start, te->new_text, strlen(te->new_text));
        }

        // Rebuild line index (karena bisa banyak newline berubah)
        free(buf->lines.offset);
        buf->lines = LineIndex_init();
        if (buf->str->len > 0) {
            Bytes all = String_get(buf->str, 0, buf->str->len);
            if (all.data) {
                LineIndex_insert(&buf->lines, (const char *)all.data, all.len);
                Bytes_free(&all);
            }
        }

        // Update cursor ke posisi aman
        if (buf->cursor.cursor_pos > buf->str->len) buf->cursor.cursor_pos = buf->str->len;

        // Sync syntax + dirty
        buf->is_dirty = true;
        sync_syntax_tree(buf);
    }

    // Setelah semua edit, sync cursor line
    sync_cursor_line_from_pos(buf);

    return true;
}

/**
 * Fungsi untuk memastikan meta_capacity [PRIVATE API]
 */
static void Buffer_ensure_git_meta_capacity(Buffer *buf, size_t needed_cap) {
    if (!buf || needed_cap <= buf->meta_capacity) return;

    size_t new_cap = needed_cap * 2;
    LineGitMeta *new_git = realloc(buf->line_git, new_cap * sizeof(LineGitMeta));
    if (!new_git) return;

    // Clean up alokasi baru
    for (size_t i = buf->meta_capacity; i < new_cap; i++) {
        new_git[i].status = GUTTER_NONE;
        new_git[i].last_edited_at = 0;
        new_git[i].author[0] = '\0';
    }

    buf->line_git = new_git;
    buf->meta_capacity = new_cap;
}

/* =============================
 * PUBLIC API
 * ============================= */

/**
 * Fungsi untuk mengkonversi path ke URI [PUBLIC API]
 */
char *Path_to_uri(const char *path) {
    if (!path) return NULL;

    // Jika path sudah ber-prefix file://, kembalikan copy-nya saja
    if (strncmp(path, "file://", 7) == 0) return strdup(path);

    // Alokasi memori yang aman
    size_t len = strlen(path) + 16;
    char *uri = malloc(len);

    // Pastikan jika path diawali '/', maka pakai file:// (jadi file:///)
    if (path[0] == '/') {
        snprintf(uri, len, "file://%s", path);
    } else {
        snprintf(uri, len, "file:///%s", path);
    }

    return uri;
}

/**
 * Helper mengambil 1 karakter berdasarkan posisi line & col [PUBLIC API]
 */
char Buffer_get_char_at(Buffer *buf, size_t line, size_t col) {
    if (!buf || line >= buf->lines.line_count) return '\0';

    size_t line_start = buf->lines.offset[line];
    size_t line_end =
        (line + 1 < buf->lines.line_count) ? buf->lines.offset[line + 1] : buf->str->len;

    // Pastikan col tidak melebihi panjang baris
    if (line_start + col >= line_end) return '\0';

    size_t target_pos = line_start + col;
    Bytes b = String_get(buf->str, target_pos, 1);
    char c = '\0';
    if (b.data && b.len > 0) {
        c = ((char *)b.data)[0];
        Bytes_free(&b);
    }
    return c;
}

/**
 * Fungsi untuk membuat buffer baru [PUBLIC API]
 */
Buffer *Buffer_new() {
    String *new = String_new();
    Position cursor = {.x = 0, .y = 0, .cursor_pos = 0};

    Buffer *new_buffer = malloc(sizeof(Buffer));

    new_buffer->str = new;
    new_buffer->cursor = cursor;
    new_buffer->lines = LineIndex_init();
    new_buffer->path = NULL;
    new_buffer->filename = strdup("Untilted");
    new_buffer->selection.is_selected = false;
    new_buffer->is_dirty = false;
    new_buffer->state = NULL;
    new_buffer->language_id = NULL;
    new_buffer->scroll_y = 0;  // UI State

    // Metadata Git
    new_buffer->meta_capacity = new_buffer->lines.line_count ? new_buffer->lines.line_count : 64;
    new_buffer->line_git = calloc(new_buffer->meta_capacity, sizeof(LineGitMeta));

    Undo_init(&new_buffer->undo);  // Undo init

    return new_buffer;
}

/**
 * Fungsi untuk membuka file dan memasukkan ke dalam Buffer [PUBLIC API]
 */
Buffer *Buffer_open(const char *filename) {
    Buffer *new = NULL;  // default pointer

    // Buka file
    Result result = Fs_open(filename);
    if (result.type == RESULT_ERR) {
        Notif_show((const char *)result.data, NOTIF_ERROR, 3.0f);
        new = Buffer_new();
        Result_free(&result);
        return new;
    }

    FileData *data = (FileData *)result.data;

    // Inisiasi data
    new = malloc(sizeof(Buffer));
    String *new_str = String_new();
    String_insert(&new_str, 0, (const char *)data->data, data->size);

    // Setting default untuk cursor
    Position cursor = {.x = 0, .y = 0, .cursor_pos = 0};
    new->str = new_str;
    new->cursor = cursor;

    // Setting untuk default line termasuk cache Offset atau start of line
    LineIndex lines = LineIndex_init();
    LineIndex_insert(&lines, (const char *)data->data, data->size);
    new->lines = lines;

    // Setting untuk path dan filename
    new->path = strdup(data->full_path);
    new->filename = strdup(get_display_name(new->path));
    new->is_dirty = false;

    // Setting default untuk new selection (Default is false)
    new->selection.is_selected = false;
    new->scroll_y = 0;  // UI State

    // Git
    new->meta_capacity = (lines.line_count > 64) ? lines.line_count : 64;
    new->line_git = calloc(new->meta_capacity, sizeof(LineGitMeta));

    // Undo init
    Undo_init(&new->undo);

    // LSP dan Syntax init
    LangConfig *lang = LspConfig_detail(new->path);
    if (lang != NULL) {
        new->state = Syntax_init(lang);
        if (new->state) Syntax_update(new->state, (const char *)data->data, data->size);

        // LSP Set Root uri jika belum ada
        if (lang->path_lsp) Ensure_lsp_init(lang, new->path);

        new->language_id = strdup(lang->language_id);  // Lang Id
        new->lsp_version = 1;

        // Set document
        if (new->path) {
            char *uri = Path_to_uri(new->path);
            Bytes full_text = String_get(new->str, 0, new->str->len);
            if (full_text.data) {
                lsp_ui_set_document(uri, lang->language_id, (const char *)full_text.data);

                // Langsung kirim didChange
                new->lsp_version++;
                lsp_did_change(uri, (const char *)full_text.data, new->lsp_version);
                Bytes_free(&full_text);
            }

            sync_syntax_tree(new);  // Sync syntax
            lsp_clear_all_diagnostics();
            lsp_ui_hide();
            free(uri);
        }

    } else {
        new->language_id = NULL;
        new->state = NULL;
    }

    LangConfig_free(lang);   // LangConfig free
    Fs_metadata_free(data);  // Safety free
    Result_free(&result);    // Free Result
    return new;
}

/**
 * Fungsi untuk memasukkan teks ke buffer [PUBLIC API]
 */
void Buffer_insert(Buffer *buf, size_t pos_idx, const char *ch) {
    if (!buf || !ch) return;

    buf->is_dirty = true;
    size_t text_len = strlen(ch);
    if (text_len == 0) return;

    // Save Line Count dan Original Y
    size_t old_line_count = buf->lines.line_count;
    size_t orig_y = buf->cursor.y;

    Undo_push(&buf->undo, UNDO_INSERT, pos_idx, ch, text_len);  // UndoStack
    // Jika ada seleksi, hapus dulu baru nulis
    if (buf->selection.is_selected) {
        Buffer_delete(buf, buf->cursor.cursor_pos);
        pos_idx = buf->cursor.cursor_pos;
    }

    // Masukkan teks ke Rope / String
    String_insert(&buf->str, pos_idx, ch, text_len);

    // Cek apakah karakter yang di-insert mengandung newline '\n'
    bool contains_newline = false;
    for (size_t i = 0; i < text_len; i++) {
        if (ch[i] == '\n') {
            contains_newline = true;
            break;
        }
    }

    // Jika TIDAK ADA newline (ngetik huruf biasa) -> Update offset biasa (Cepat)
    if (!contains_newline) {
        for (size_t i = buf->cursor.y + 1; i < buf->lines.line_count; i++) {
            buf->lines.offset[i] += text_len;
        }
        buf->cursor.x += text_len;
        buf->cursor.cursor_pos += text_len;
    }
    // Jika ADA newline (pencet Enter / Paste multi-line) -> Rebuild LineIndex biar sinkron
    else {
        buf->cursor.cursor_pos += text_len;

        free(buf->lines.offset);
        buf->lines = LineIndex_init();

        if (buf->str->len > 0) {
            Bytes all = String_get(buf->str, 0, buf->str->len);
            if (all.data) {
                LineIndex_insert(&buf->lines, (const char *)all.data, all.len);
                Bytes_free(&all);
            }
        }

        // Cari posisi Y dan X kursor yang presisi dari cursor_pos
        size_t y = 0;
        while (y + 1 < buf->lines.line_count &&
               buf->lines.offset[y + 1] <= buf->cursor.cursor_pos) {
            y++;
        }
        buf->cursor.y = y;
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
    }

    // Sinkronisasi Metadata Git
    Buffer_ensure_git_meta_capacity(buf, buf->lines.line_count);

    // Sinkronisasi dengan Git jika Path adalah Repo
    if (git.is_repo && contains_newline && buf->line_git) {
        size_t added_lines = buf->lines.line_count - old_line_count;

        // Geser metadata di bawah baris yang terbelah ke arah bawah
        if (orig_y + 1 < old_line_count) {
            size_t lines_to_move = old_line_count - (orig_y + 1);
            memmove(&buf->line_git[orig_y + 1 + added_lines], &buf->line_git[orig_y + 1],
                    lines_to_move * sizeof(LineGitMeta));
        }

        // Tandai baris-baris baru hasil pecahan/insert sebagai MODIFIED/ADDED
        for (size_t i = orig_y; i <= orig_y + added_lines; i++) {
            buf->line_git[i].status = GUTTER_MODIFIED;
            buf->line_git[i].last_edited_at = (double)time(NULL);
            strncpy(buf->line_git[i].author, git.author[0] ? git.author : "You",
                    sizeof(buf->line_git[i].author) - 1);
        }
    } else if (git.is_repo && buf->line_git) {  // Sinkronisasi jika Path adalah Repo
        // Edit biasa (1 baris)
        size_t y = buf->cursor.y;
        buf->line_git[y].status = GUTTER_MODIFIED;
        buf->line_git[y].last_edited_at = (double)time(NULL);
        strncpy(buf->line_git[y].author, git.author[0] ? git.author : "You",
                sizeof(buf->line_git[y].author) - 1);
    }

    sync_syntax_tree(buf);  // Update syntax tree

    // Update LSP
    if (buf->language_id) {
        Bytes all_text = String_get(buf->str, 0, buf->str->len);
        if (all_text.data) {
            char *uri = Path_to_uri(buf->path);
            lsp_did_change(uri, (const char *)all_text.data, buf->lsp_version);

            buf->lsp_version++;  // Update LSP Version
            free(uri);
            Bytes_free(&all_text);
        }
    }
}

/**
 * Fungsi untuk menghapus teks dari Buffer [PUBLIC API]
 */
void Buffer_delete(Buffer *buf, size_t pos_idx) {
    if (!buf || !buf->str || buf->str->len == 0) return;

    size_t len = 0;
    size_t start_del = 0;
    buf->is_dirty = true;

    if (buf->selection.is_selected) {
        Get_selected_position(buf, &start_del, &len);
        buf->selection.is_selected = false;
    } else {
        if (pos_idx == 0) return;
        len = 1;
        start_del = pos_idx - 1;
    }

    // Simpan old Line Count
    size_t old_line_count = buf->lines.line_count;

    if (len == 0) return;
    if (start_del + len > buf->str->len) len = buf->str->len - start_del;

    // Cek apakah ada karakter '\n' di area yang mau dihapus
    bool contains_newline = false;
    Bytes del_bytes = String_get(buf->str, start_del, len);
    if (del_bytes.data) {
        for (size_t i = 0; i < len; i++) {
            if (del_bytes.data[i] == '\n') {
                contains_newline = true;
                break;
            }
        }
        Undo_push(&buf->undo, UNDO_DELETE, start_del, (const char *)del_bytes.data, len);
        Bytes_free(&del_bytes);
    }

    // Hapus dari rope
    String_delete(&buf->str, start_del, len);
    buf->cursor.cursor_pos = start_del;

    /* Jika TIDAK ADA newline, update offset biasa */
    if (!contains_newline) {
        for (size_t i = buf->cursor.y + 1; i < buf->lines.line_count; i++) {
            buf->lines.offset[i] -= len;
        }
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[buf->cursor.y];
    }
    // Jika ADA newline yang terhapus, Rebuild LineIndex
    else {
        free(buf->lines.offset);
        buf->lines = LineIndex_init();

        if (buf->str->len > 0) {
            Bytes all = String_get(buf->str, 0, buf->str->len);
            if (all.data) {
                LineIndex_insert(&buf->lines, (const char *)all.data, all.len);
                Bytes_free(&all);
            }
        }

        if (buf->lines.line_count == 0) {
            buf->lines.line_count = 1;
            buf->lines.offset[0] = 0;
        }

        size_t y = 0;
        while (y + 1 < buf->lines.line_count &&
               buf->lines.offset[y + 1] <= buf->cursor.cursor_pos) {
            y++;
        }
        buf->cursor.y = y;
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
    }

    // Sinkronisasi Metadata Git
    Buffer_ensure_git_meta_capacity(
        buf, old_line_count > buf->lines.line_count ? old_line_count : buf->lines.line_count);

    // Git Metadata
    if (git.is_repo && contains_newline && buf->line_git) {  // Jika Path adalah Repo
        size_t deleted_lines = old_line_count - buf->lines.line_count;
        size_t cur_y = buf->cursor.y;

        // Geser metadata di bawah baris terhapus ke ATAS
        if (cur_y + 1 + deleted_lines < old_line_count) {
            size_t lines_to_move = old_line_count - (cur_y + 1 + deleted_lines);
            memmove(&buf->line_git[cur_y + 1], &buf->line_git[cur_y + 1 + deleted_lines],
                    lines_to_move * sizeof(LineGitMeta));
        }

        // Bersihkan slot tersisa di ekor array
        for (size_t i = buf->lines.line_count; i < old_line_count; i++) {
            buf->line_git[i].status = GUTTER_NONE;
            buf->line_git[i].last_edited_at = 0;
            buf->line_git[i].author[0] = '\0';
        }

        // Update status baris penggabungan saat ini
        if (cur_y < buf->meta_capacity) {
            buf->line_git[cur_y].status = GUTTER_MODIFIED;
            buf->line_git[cur_y].last_edited_at = (double)time(NULL);
            strncpy(buf->line_git[cur_y].author, git.author[0] ? git.author : "You",
                    sizeof(buf->line_git[cur_y].author) - 1);
        }
    } else if (git.is_repo && buf->line_git) {  // Jika Path adalah Repo
        // Delete biasa 1 baris
        size_t y = buf->cursor.y;
        if (y < buf->meta_capacity) {
            buf->line_git[y].status = GUTTER_MODIFIED;
            buf->line_git[y].last_edited_at = (double)time(NULL);
            strncpy(buf->line_git[y].author, git.author[0] ? git.author : "You",
                    sizeof(buf->line_git[y].author) - 1);
        }
    }

    // Sync Syntax Tree & LSP
    sync_syntax_tree(buf);

    if (buf->language_id && buf->path) {
        Bytes full_text = String_get(buf->str, 0, buf->str->len);
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            lsp_did_change(uri, (const char *)full_text.data, buf->lsp_version);
            buf->lsp_version++;
            free(uri);
        }
        Bytes_free(&full_text);
    }
}

/**
 * Fungsi untuk save File [PUBLIC API]
 */
void Buffer_save(Buffer *buf, const char *filename) {
    if (!buf) return;
    if (filename != NULL) {
        Result result = Fs_create(filename);

        if (result.type == RESULT_OK) {
            buf->path = strdup(result.data);
            buf->filename = strdup(get_display_name(buf->path));

            Result_free(&result);
        } else {
            Notif_show("Gagal menyimpan File!", NOTIF_WARNING, 3.0f);
            Result_free(&result);
            return;
        }
    }

    // Proses Auto format jika hanya punya language id
    if (buf->language_id != NULL) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            TextEditList edits = lsp_format(uri, 4, true);
            if (edits.count > 0) {
                lsp_apply_text_edits(buf, &edits);
                Bytes data = String_get(buf->str, 0, buf->str->len);
                if (data.data) {
                    lsp_did_change(uri, (const char *)data.data, buf->lsp_version);
                    buf->lsp_version++;
                    Bytes_free(&data);
                }
                lsp_free_text_edits(&edits);
                free(uri);
            }
        }
    }

    Bytes data = String_get(buf->str, 0, buf->str->len);

    Result result = Fs_savefile(buf->path, (const char *)data.data, data.len);
    if (result.type == RESULT_OK) {
        buf->is_dirty = false;
        Notif_show(result.data, NOTIF_SUCCESS, 3.0f);
    } else {
        Notif_show(result.data, NOTIF_ERROR, 3.0f);
    }

    GitStatus_force();  // Force update GitStatus

    Result_free(&result);
    Bytes_free(&data);
}

/**
 * Fungsi untuk copy dari buffer ke Clipboard [PUBLIC API]
 */
void Buffer_copy(Buffer *buf, Clipboard *clp) {
    if (!buf || !buf->selection.is_selected) return;

    size_t start, len;
    Get_selected_position(buf, &start, &len);

    Bytes clip = String_get(buf->str, start, len);

    Clipboard_set(clp, &clip);
}

/**
 * Fungsi untuk Copy dan Delete teks dari Buffer (CUT) [PUBLIC API]
 */
void Buffer_cut(Buffer *buf, Clipboard *clp) {
    if (!buf || !buf->selection.is_selected) return;

    Buffer_copy(buf, clp);
    Buffer_delete(buf, buf->cursor.cursor_pos);
}

/**
 * Fungsi untuk paste dari Clipboard ke Buffer [PUBLIC API]
 */
void Buffer_paste(Buffer *buf, Clipboard *clp) {
    if (!buf || !clp) return;

    // Selalu sinkronkan isi clipboard terbaru dari OS
    const char *text = Clipboard_get_text(clp);
    if (!text || strlen(text) == 0) return;

    // Jika sedang ada seleksi, hapus dulu area yang di-select
    if (buf->selection.is_selected) {
        Buffer_delete(buf, buf->cursor.cursor_pos);
    }

    // Insert teks dari Clipboard ke buffer
    Buffer_insert(buf, buf->cursor.cursor_pos, text);
}

/**
 * Fungsi untuk Undo [PUBLIC API]
 */
void Buffer_undo(Buffer *buf) {
    UndoAction a;
    if (!Undo_pop(&buf->undo, &a)) return;

    buf->undo.is_undoing = true;

    if (a.type == UNDO_INSERT) {
        buf->cursor.cursor_pos = a.offset + a.len;
        Buffer_delete(buf, buf->cursor.cursor_pos);
    } else if (a.type == UNDO_DELETE) {
        Buffer_insert(buf, a.offset, a.text);
    }

    buf->undo.is_undoing = false;
}

/**
 * Fungsi untuk Redo [PUBLIC API]
 */
void Buffer_redo(Buffer *buf) {
    if (!buf) return;

    UndoAction a;
    if (!Redo_pop(&buf->undo, &a)) return;

    buf->undo.is_undoing = true;  // Kunci biar gak nge-push undo baru

    if (a.type == UNDO_INSERT) {
        // Redo INSERT = Lakukan insert teks lagi
        Buffer_insert(buf, a.offset, a.text);
    } else if (a.type == UNDO_DELETE) {
        // Redo DELETE = Lakukan delete lagi
        buf->cursor.cursor_pos = a.offset + a.len;
        Buffer_delete(buf, buf->cursor.cursor_pos);
    }

    buf->undo.is_undoing = false;
}

/**
 * Fungsi untuk mengambil text line berdasarkan y (line = y + 1) [PUBLIC API]
 */
char *Buffer_get_line_text(Buffer *buf, size_t y) {
    if (!buf || y >= buf->lines.line_count) return NULL;

    // Mencari indeks posisi awal dan akhir dari line
    size_t start = buf->lines.offset[y];
    size_t end;

    if (y + 1 < buf->lines.line_count) {
        end = buf->lines.offset[y + 1];
    } else {
        end = buf->str->len;
    }

    size_t length = end - start;  // Panjang teks

    Bytes data = String_get(buf->str, start, length);  // Ambil data dari buffer
    if (!data.data) return NULL;

    char *result = malloc(length + 1);
    if (result) {
        memcpy(result, data.data, length);
        result[length] = '\0';
    }

    Bytes_free(&data);  // Safety free
    return result;
}

/**
 * Fungsi untuk menghapus buffer seperti reset ketika membuat buffer baru [PUBLIC API]
 */
void Buffer_free(Buffer *buf) {
    if (!buf) return;

    if (buf->state) {
        Syntax_free(buf->state);
        buf->state = NULL;
    }

    if (buf->str != NULL) {
        String_release(buf->str);
        buf->str = NULL;
    }

    if (buf->path != NULL) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            lsp_did_close((const char *)uri);
            free(uri);
        }
        free(buf->path);
        buf->path = NULL;
    }

    if (buf->filename != NULL) {
        free(buf->filename);
        buf->filename = NULL;
    }

    if (buf->lines.offset) {
        free(buf->lines.offset);
        buf->lines.offset = NULL;
    }
    if (buf->line_git != NULL) {
        free(buf->line_git);
        buf->line_git = NULL;
    }

    Undo_free(&buf->undo);

    if (buf->language_id != NULL) {
        free(buf->language_id);
        buf->language_id = NULL;
    }

    free(buf);
}

/**
 * Mengambil kata yang sedang diketik di sekitar kursor [PUBLIC API]
 */
void Buffer_get_current_word(Buffer *buf, char *out_str, size_t max_len) {
    if (!buf || !out_str || max_len == 0) return;
    out_str[0] = '\0';

    // Ambil teks pada baris kursor saat ini
    char *line_text = Buffer_get_line_text(buf, buf->cursor.y);
    if (!line_text) return;

    size_t col = buf->cursor.x;
    size_t line_len = strlen(line_text);

    // Pastikan batas kolom tidak melebihi panjang teks baris
    if (col > line_len) col = line_len;

    // Mundur ke belakang dari posisi kursor untuk mencari awal kata
    size_t start = col;
    while (start > 0) {
        char c = line_text[start - 1];
        if (!isalnum((unsigned char)c) && c != '_') {
            break;  // Stop jika bertemu spasi, simbol, atau operator
        }
        start--;
    }

    // Salin prefix kata ke buffer output
    size_t word_len = col - start;
    if (word_len >= max_len) word_len = max_len - 1;

    if (word_len > 0) {
        strncpy(out_str, line_text + start, word_len);
        out_str[word_len] = '\0';
    }

    free(line_text);  // Bersihkan alokasi memori dari Buffer_get_line_text
}

/**
 * Fungsi Apply LSP Completion ke Buffer (Anti Numpuk!)
 */
void lsp_apply_completion(Buffer *buf, const CompletionItem *item) {
    if (!buf || !item) return;

    // Ambil teks yang mau di-insert
    const char *text_to_insert =
        (item->insert_text && strlen(item->insert_text) > 0) ? item->insert_text : item->label;
    if (!text_to_insert) return;

    // Hapus Prefix Kata yang Sedang Diketik (misal hapus 'prin')
    size_t prefix_len = 0;
    size_t col = buf->cursor.x;
    size_t line = buf->cursor.y;

    while (col > 0) {
        char c = Buffer_get_char_at(buf, line, col - 1);
        if (!isalnum((unsigned char)c) && c != '_') break;
        prefix_len++;
        col--;
    }

    if (prefix_len > 0) {
        for (size_t i = 0; i < prefix_len; i++) {
            Buffer_delete(buf, buf->cursor.cursor_pos);
        }
    }

    // Insert header di Paling Atas (index 0) JIKA ADA
    if (item->header_include && strlen(item->header_include) > 0) {
        // Insert ke baris paling atas
        Buffer_insert(buf, 0, item->header_include);

        // Hitung ulang posisi baris Y & kolom X kursor dari cursor_pos
        size_t y = 0;
        while (y + 1 < buf->lines.line_count &&
               buf->lines.offset[y + 1] <= buf->cursor.cursor_pos) {
            y++;
        }

        buf->cursor.y = y;
        buf->cursor.x = buf->cursor.cursor_pos - buf->lines.offset[y];
    }

    // Insert teks fungsi/variabel utama di posisi kursor sekarang
    Buffer_insert(buf, buf->cursor.cursor_pos, text_to_insert);
}

/**
 * Fungsi untuk Mark Line edited Git
 */
void Buffer_mark_line_edited(Buffer *buf, size_t line_idx) {
    if (!buf) return;

    if (line_idx >= buf->meta_capacity) {
        size_t new_cap = (line_idx + 1) * 2;
        LineGitMeta *new_git = realloc(buf->line_git, new_cap * sizeof(LineGitMeta));
        if (!new_git) return;

        buf->line_git = new_git;
        for (size_t i = buf->meta_capacity; i < new_cap; i++) {
            buf->line_git[i].status = GUTTER_NONE;
            buf->line_git[i].last_edited_at = 0;
            buf->line_git[i].author[0] = '\0';
        }
        buf->meta_capacity = new_cap;
    }

    buf->line_git[line_idx].status = GUTTER_MODIFIED;
    buf->line_git[line_idx].last_edited_at = (double)time(NULL);  // Gunakan time(NULL)
    strncpy(buf->line_git[line_idx].author, git.author[0] ? git.author : "You",
            sizeof(buf->line_git[line_idx].author) - 1);
}
