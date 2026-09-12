/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "buffer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tree_sitter/api.h>
#include <unistd.h>

#include "fs.h"
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

/**
 * Line Index Init [PRIVATE API]
 */
LineIndex LineIndex_init() {
    LineIndex li;
    li.capacity = 128;

    // Ganti pakai calloc biar lebih aman karena memang hanya
    // dipanggil sekali ketika init aplikasi
    li.offset = calloc(li.capacity, sizeof(size_t));
    li.line_count = 1;
    li.offset[0] = 0;

    return li;
}

/**
 * Line Index Insert [PRIVATE API]
 */
static void LineIndex_insert(LineIndex *li, const char *data, size_t len) {
    if (!li || !li->offset || !data) return;

    // Set internal dulu, pastikan kosong
    li->offset[0] = 0;
    li->line_count = 1;

    // Masuk ke perulangan teks sesuai dengan len
    for (size_t i = 0; i < len; i++) {
        if (data[i] != '\n') continue;  // Jika bukan '\n' lanjut

        if (li->line_count >= li->capacity) {
            li->capacity *= 2;
            size_t *new_offset = realloc(li->offset, sizeof(size_t) * li->capacity);
            if (!new_offset) return;  // Guard jika realloc gagal

            li->offset = new_offset;
        }
        li->offset[li->line_count++] = i + 1;
    }
}
/**
 * Helper untuk memastikan kapasitas LineIndex cukup [PRIVATE API]
 */
static bool line_index_reserve(LineIndex *li, size_t needed) {
    if (needed <= li->capacity) return true;

    size_t new_cap = li->capacity ? li->capacity * 2 : 128;
    while (new_cap < needed) new_cap *= 2;

    size_t *tmp = realloc(li->offset, new_cap * sizeof(size_t));
    if (!tmp) return false;

    li->offset = tmp;
    li->capacity = new_cap;
    return true;
}

/*
 * Helper internal untuk Insert newlines secara incremental.
 * newline_pos[] berisi offset relatif dari `pos` di mana karakter '\n' berada.
 * Semua nilai di newline_pos harus < inserted dan sudah terurut ascending.
 */
static bool line_index_insert_newlines(LineIndex *li, size_t pos, size_t inserted,
                                       const size_t *newline_pos, size_t newline_count) {
    if (inserted == 0) return true;

    /* Cari line pertama yang offset-nya > pos */
    size_t first = 0;
    while (first < li->line_count && li->offset[first] <= pos) first++;

    /* Geser semua offset setelah titik insert */
    for (size_t i = first; i < li->line_count; ++i) li->offset[i] += inserted;

    if (newline_count == 0) return true;

    if (!line_index_reserve(li, li->line_count + newline_count)) return false;

    /* Buat ruang untuk entry baru */
    memmove(li->offset + first + newline_count, li->offset + first,
            (li->line_count - first) * sizeof(size_t));

    /* Isi offset baru (menunjuk ke karakter setelah '\n') */
    for (size_t i = 0; i < newline_count; ++i) li->offset[first + i] = pos + newline_pos[i] + 1;

    li->line_count += newline_count;
    return true;
}

/*
 * Helper internal untuk Delete range [pos, pos + deleted).
 * Menghapus semua line start yang jatuh di dalam range tersebut
 * lalu menggeser offset yang tersisa.
 */
static bool line_index_delete_range(LineIndex *li, size_t pos, size_t deleted) {
    if (deleted == 0) return true;

    size_t end = pos + deleted;

    size_t first = 0;
    while (first < li->line_count && li->offset[first] <= pos) first++;

    size_t last = first;
    while (last < li->line_count && li->offset[last] <= end) last++;

    size_t remove_count = last - first;

    if (remove_count > 0) {
        memmove(li->offset + first, li->offset + last, (li->line_count - last) * sizeof(size_t));
        li->line_count -= remove_count;
    }

    /* Geser offset yang berada di belakang range */
    for (size_t i = first; i < li->line_count; ++i) li->offset[i] -= deleted;

    /* Jaga invariant: minimal 1 entry */
    if (li->line_count == 0) {
        li->offset[0] = 0;
        li->line_count = 1;
    }

    return true;
}

/**
 * Fungsi untuk mengambil start dan panjang teks di seleksi [PRIVATE API]
 */
void Get_selected_position(Buffer *buf, size_t *start, size_t *len) {
    size_t sel_start = buf->start;
    size_t current = buf->cursor.cursor_pos;

    // Logika min dan max
    size_t sel_min = (sel_start < current) ? sel_start : current;
    size_t sel_max = (sel_start > current) ? sel_start : current;

    // mencari len
    *len = sel_max - sel_min;
    *start = sel_min;

    if (*len == 0) {
        CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
        return;
    }
}

/**
 * Fungsi untuk sinkronisasi tree sitter [PRIVATE API]
 */
static void sync_syntax_tree(Buffer *buf) {
    if (!buf || !buf->state) return;

    size_t rope_len = String_len(buf->str);
    Bytes full_text = String_get(buf->str, 0, rope_len);
    if (full_text.data) {
        if (buf->state->tree) {
            ts_tree_delete(buf->state->tree);
        }

        buf->state->tree = ts_parser_parse_string(buf->state->parser, nullptr,
                                                  (const char *)full_text.data, (uint32_t)rope_len);
        Bytes_free(&full_text);
    }
}

/**
 * Helper untuk mengambil Nama file dari Path [PRIVATE API]
 */
static char *get_display_name(const char *filepath) {
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
    size_t rope_len = String_len(buf->str);
    if ((size_t)line >= buf->lines.line_count) return rope_len;

    size_t line_start = buf->lines.offset[line];
    size_t line_end =
        (line + 1 < (int)buf->lines.line_count) ? buf->lines.offset[line + 1] : rope_len;

    size_t offset = line_start + (size_t)character;
    if (offset > line_end) offset = line_end;
    if (offset > rope_len) offset = rope_len;
    return offset;
}

/**
 * Fungsi untuk Apply Auto Format [PRIVATE API]
 */
static bool lsp_apply_text_edits(Buffer *buf, TextEditList *edits) {
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
        size_t rope_len = String_len(buf->str);

        if (rope_len > 0) {
            Bytes all = String_get(buf->str, 0, rope_len);
            if (all.data) {
                LineIndex_insert(&buf->lines, (const char *)all.data, all.len);
                Bytes_free(&all);
            }
        }

        // Update cursor ke posisi aman
        if (buf->cursor.cursor_pos > rope_len) buf->cursor.cursor_pos = rope_len;

        // Sync syntax + dirty
        SET_FLAG(buf->buf_flags, BUF_IS_DIRTY);
        sync_syntax_tree(buf);
    }

    // Setelah semua edit, sync cursor line
    sync_cursor_line_from_pos(buf);

    return true;
}

/**
 * Fungsi untuk mengkonversi path ke URI [PUBLIC API]
 */
char *Path_to_uri(const char *path) {
    if (!path) return nullptr;

    // Jika path sudah ber-prefix file://, kembalikan copy-nya saja
    if (strncmp(path, "file://", 7) == 0) return strdup(path);

    // Alokasi memori yang aman
    size_t len = strlen(path) + 16;
    char *uri = calloc(len, sizeof(char));

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
    size_t rope_len = String_len(buf->str);
    size_t line_end = (line + 1 < buf->lines.line_count) ? buf->lines.offset[line + 1] : rope_len;

    // Pastikan col tidak melebihi panjang baris
    if (line_start + col >= line_end) return '\0';

    size_t target_pos = line_start + col;
    Bytes b = String_get(buf->str, target_pos, 1);

    char c = '\0';
    if (b.data && b.len > 0) {
        c = ((char *)b.data)[0];
    }

    Bytes_free(&b);
    return c;
}

/**
 * Fungsi untuk membuat buffer baru [PUBLIC API]
 */
Buffer *Buffer_new() {
    String *new = String_new();
    Position cursor = {.x = 0, .y = 0, .cursor_pos = 0};

    Buffer *new_buffer = calloc(1, sizeof(Buffer));

    new_buffer->str = new;
    new_buffer->cursor = cursor;
    new_buffer->lines = LineIndex_init();
    new_buffer->path = nullptr;
    new_buffer->filename = strdup("Untilted");
    new_buffer->buf_flags = 0;
    new_buffer->state = nullptr;
    new_buffer->language_id = nullptr;
    new_buffer->scroll_y = 0;          // UI State
    new_buffer->diagnostic = nullptr;  // Diagnostic

    Undo_init(&new_buffer->undo);  // Undo init

    return new_buffer;
}

/**
 * Fungsi untuk membuka file dan memasukkan ke dalam Buffer [PUBLIC API]
 */
Buffer *Buffer_open(const char *filename) {
    Buffer *new = nullptr;  // default pointer

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
    new = calloc(1, sizeof(Buffer));
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
    new->buf_flags = 0;

    new->scroll_y = 0;  // UI State

    // Undo init
    Undo_init(&new->undo);

    // LSP dan Syntax init
    LangConfig *lang = LspConfig_detail(new->path);

    if (lang != nullptr) {
        new->state = Syntax_init(lang);
        if (new->state) Syntax_update(new->state, (const char *)data->data, data->size);

        // LSP Set Root uri jika belum ada
        if (lang->path_lsp) Ensure_lsp_init(lang, new->path);

        new->language_id = strdup(lang->language_id);  // Lang Id
        new->lsp_version = 1;

        // Set document
        if (new->path) {
            char *uri = Path_to_uri(new->path);

            size_t rope_len = String_len(new->str);
            Bytes full_text = String_get(new->str, 0, rope_len);

            if (full_text.data != nullptr) {
                lsp_ui_set_document(uri, lang->language_id, (const char *)full_text.data);

                // Langsung kirim didChange
                new->lsp_version++;
                lsp_did_change(uri, (const char *)full_text.data, new->lsp_version);
                Bytes_free(&full_text);
            }

            new->diagnostic = nullptr;
            // Diagnostic jalan kalau sudah ada editing aja kali ya HAHAH

            // Konsepnya itu terpusat di Draw Diagsnostic bar (render_lsp_ui.c) dan render.c
            sync_syntax_tree(new);  // Sync syntax
            lsp_clear_all_diagnostics();
            lsp_ui_hide();
            free(uri);
        }

    } else {
        new->language_id = nullptr;
        new->state = nullptr;
        new->diagnostic = nullptr;  // Set ke NULL aja
    }

    Fs_metadata_free(data);
    LangConfig_free(lang);
    Result_free(&result);
    return new;
}

/**
 * Fungsi untuk memasukkan teks ke buffer [PUBLIC API]
 */
void Buffer_insert(Buffer *buf, size_t pos_idx, const char *ch) {
    if (!buf || !ch) return;

    SET_FLAG(buf->buf_flags, BUF_IS_DIRTY);
    size_t text_len = strlen(ch);
    if (text_len == 0) return;

    // Handle selection dulu
    if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
        Buffer_delete(buf, buf->cursor.cursor_pos);
        pos_idx = buf->cursor.cursor_pos;
    }

    Undo_push(&buf->undo, UNDO_INSERT, pos_idx, ch, text_len);

    // Insert ke rope
    String_insert(&buf->str, pos_idx, ch, text_len);

    // Kumpulkan posisi semua newline yang baru di-insert
    size_t newline_pos[64]; /* stack buffer kecil dulu */
    size_t newline_count = 0;
    bool need_heap = false;
    size_t *nl_buf = newline_pos;

    for (size_t i = 0; i < text_len; ++i) {
        if (ch[i] == '\n') {
            if (newline_count >= 64 && !need_heap) {
                /* fallback ke heap kalau terlalu banyak newline */
                nl_buf = malloc(text_len * sizeof(size_t));
                if (!nl_buf) {
                    goto full_rebuild;
                }

                memcpy(nl_buf, newline_pos, 64 * sizeof(size_t));
                need_heap = true;
            }
            nl_buf[newline_count++] = i;
        }
    }

    // Update LineIndex secara incremental
    if (!line_index_insert_newlines(&buf->lines, pos_idx, text_len, nl_buf, newline_count)) {
        /* Kalau reserve gagal, fallback ke rebuild */
        goto full_rebuild;
    }

    if (need_heap) free(nl_buf);

    // Update cursor
    buf->cursor.cursor_pos += text_len;
    sync_cursor_line_from_pos(buf);

    // Syntax + LSP
    size_t rope_len = String_len(buf->str);
    Bytes full_text = String_get(buf->str, 0, rope_len);

    sync_syntax_tree(buf);

    if (buf->language_id && buf->path) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            lsp_did_change(uri, (const char *)full_text.data, buf->lsp_version);
            buf->lsp_version++;
            free(uri);
        }
    }

    Bytes_free(&full_text);
    return;

full_rebuild:
    // Fallback path
    if (need_heap) free(nl_buf);

    free(buf->lines.offset);
    buf->lines = LineIndex_init();

    size_t rope_len2 = String_len(buf->str);
    Bytes full = String_get(buf->str, 0, rope_len2);
    if (full.data) {
        LineIndex_insert(&buf->lines, (const char *)full.data, full.len);
        Bytes_free(&full);
    }

    buf->cursor.cursor_pos += text_len;
    sync_cursor_line_from_pos(buf);
    sync_syntax_tree(buf);
}

/**
 * Fungsi untuk menghapus teks dari Buffer [PUBLIC API]
 */
void Buffer_delete(Buffer *buf, size_t pos_idx) {
    if (!buf || !buf->str) return;

    size_t len = 0;
    size_t start_del = 0;

    SET_FLAG(buf->buf_flags, BUF_IS_DIRTY);

    if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
        Get_selected_position(buf, &start_del, &len);
        CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);
    } else {
        if (pos_idx == 0) return;
        len = 1;
        start_del = pos_idx - 1;
    }

    size_t rope_len = String_len(buf->str);
    if (len == 0) return;
    if (start_del + len > rope_len) len = rope_len - start_del;

    // Ambil data yang akan dihapus (untuk undo + deteksi newline)
    Bytes del_bytes = String_get(buf->str, start_del, len);
    if (del_bytes.data) {
        Undo_push(&buf->undo, UNDO_DELETE, start_del, (const char *)del_bytes.data, len);
        Bytes_free(&del_bytes);
    }

    // Hapus dari rope dan update cursor pos
    String_delete(&buf->str, start_del, len);
    buf->cursor.cursor_pos = start_del;

    // Update LineIndex secara incremental
    line_index_delete_range(&buf->lines, start_del, len);

    // Sync cursor coordinates
    sync_cursor_line_from_pos(buf);

    // Syntax + LSP
    size_t new_len = String_len(buf->str);
    Bytes full_text = String_get(buf->str, 0, new_len);

    sync_syntax_tree(buf);

    if (buf->language_id && buf->path) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            lsp_did_change(uri, (const char *)full_text.data, buf->lsp_version);
            buf->lsp_version++;
            free(uri);
        }
    }
    Bytes_free(&full_text);
}

/**
 * Fungsi untuk save File [PUBLIC API]
 */
void Buffer_save(Buffer *buf, const char *filename) {
    if (!buf) return;
    if (filename != nullptr) {
        Result result = Fs_create(filename);

        if (result.type == RESULT_OK) {
            // Jaga2 untuk free buf path dan filename
            if (buf->path) free(buf->path);
            if (buf->filename) free(buf->filename);

            buf->path = strdup(result.data);
            buf->filename = strdup(get_display_name(buf->path));

            Result_free(&result);
        } else {
            Notif_show("Gagal menyimpan File!", NOTIF_WARNING, 3.0f);
            Result_free(&result);
            return;
        }
    }
    if (!buf->path) return;  // Memastikan path bener sebelum save

    // Proses Auto format jika hanya punya language id
    if (buf->language_id != nullptr) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            TextEditList edits = lsp_format(uri, 4, true);
            if (edits.count > 0) {
                lsp_apply_text_edits(buf, &edits);
                size_t rope_len = String_len(buf->str);  // Len sebelum auto format
                Bytes data = String_get(buf->str, 0, rope_len);
                if (data.data) {
                    lsp_did_change(uri, (const char *)data.data, buf->lsp_version);
                    buf->lsp_version++;
                    Bytes_free(&data);
                }
                lsp_free_text_edits(&edits);
            }
            free(uri);
        }
    }

    size_t final_len = String_len(buf->str);  // Len setelah di format oleh LSP
    Bytes data = String_get(buf->str, 0, final_len);
    if (data.data) {
        Result result = Fs_savefile(buf->path, (const char *)data.data, data.len);
        if (result.type == RESULT_OK) {
            CLR_FLAG(buf->buf_flags, BUF_IS_DIRTY);
            Notif_show(result.data, NOTIF_SUCCESS, 3.0f);
        } else {
            Notif_show(result.data, NOTIF_ERROR, 3.0f);
        }
        Result_free(&result);
        Bytes_free(&data);
    }
}

/**
 * Fungsi untuk copy dari buffer ke Clipboard [PUBLIC API]
 */
void Buffer_copy(Buffer *buf, Clipboard *clp) {
    if (!buf || !HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) return;

    size_t start, len;
    Get_selected_position(buf, &start, &len);

    Bytes clip = String_get(buf->str, start, len);

    Clipboard_set(clp, &clip);
}

/**
 * Fungsi untuk Copy dan Delete teks dari Buffer (CUT) [PUBLIC API]
 */
void Buffer_cut(Buffer *buf, Clipboard *clp) {
    if (!buf || !HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) return;

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
    if (HAS_FLAG(buf->buf_flags, BUF_IS_SELECT)) {
        Buffer_delete(buf, buf->cursor.cursor_pos);
    }

    // Insert teks dari Clipboard ke buffer
    Buffer_insert(buf, buf->cursor.cursor_pos, text);
}

/**
 * Fungsi untuk Undo [PUBLIC API]
 */
void Buffer_undo(Buffer *buf) {
    if (!buf) return;

    UndoAction a;
    if (!Undo_pop(&buf->undo, &a)) return;

    buf->undo.is_undoing = true;
    // Matikan seleksi agar tidak ngerusak delete
    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);

    if (a.type == UNDO_INSERT) {
        // Undo dari INSERT adalah DELETE teks tersebut
        buf->cursor.cursor_pos = a.offset + a.len;
        sync_cursor_line_from_pos(buf);
        Buffer_delete(buf, buf->cursor.cursor_pos);

        // Kembalikan kursor ke posisi awal sebelum insert
        buf->cursor.cursor_pos = a.offset;
        sync_cursor_line_from_pos(buf);
    } else if (a.type == UNDO_DELETE) {
        // Undo dari DELETE adalah INSERT kembali teks yang terhapus
        buf->cursor.cursor_pos = a.offset;
        sync_cursor_line_from_pos(buf);
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

    buf->undo.is_undoing = true;
    CLR_FLAG(buf->buf_flags, BUF_IS_SELECT);  // Matikan seleksi

    if (a.type == UNDO_INSERT) {
        // Redo INSERT = Insert ulang teks di offset asal
        buf->cursor.cursor_pos = a.offset;
        sync_cursor_line_from_pos(buf);
        Buffer_insert(buf, a.offset, a.text);
    } else if (a.type == UNDO_DELETE) {
        // Redo DELETE = Delete ulang teks tersebut
        buf->cursor.cursor_pos = a.offset + a.len;
        sync_cursor_line_from_pos(buf);
        Buffer_delete(buf, buf->cursor.cursor_pos);
    }

    buf->undo.is_undoing = false;
}

/**
 * Fungsi untuk mengambil text line berdasarkan y (line = y + 1) [PUBLIC API]
 */
char *Buffer_get_line_text(Buffer *buf, size_t y) {
    if (!buf || y >= buf->lines.line_count) return nullptr;

    // Mencari indeks posisi awal dan akhir dari line
    size_t start = buf->lines.offset[y];
    size_t end;

    size_t rope_len = String_len(buf->str);
    if (y + 1 < buf->lines.line_count) {
        end = buf->lines.offset[y + 1];
    } else {
        end = rope_len;
    }

    size_t length = end - start;  // Panjang teks

    Bytes data = String_get(buf->str, start, length);  // Ambil data dari buffer
    if (!data.data) return nullptr;

    char *result = calloc(length + 1, sizeof(char));
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

    // Tree sitter
    if (buf->state) {
        Syntax_free(buf->state);
        buf->state = nullptr;
    }

    // String Rope atau Buffer utama
    if (buf->str != nullptr) {
        String_release(buf->str);
        buf->str = nullptr;
    }

    // Buffer path ke file
    if (buf->path != nullptr) {
        char *uri = Path_to_uri(buf->path);
        if (uri) {
            lsp_did_close((const char *)uri);
            free(uri);
        }
        free(buf->path);
        buf->path = nullptr;
    }

    // Filename
    if (buf->filename != nullptr) {
        free(buf->filename);
        buf->filename = nullptr;
    }

    // Index Line
    if (buf->lines.offset) {
        free(buf->lines.offset);
        buf->lines.offset = nullptr;
    }

    // Undo
    Undo_free(&buf->undo);

    // Language ID
    if (buf->language_id != nullptr) {
        free(buf->language_id);
        buf->language_id = nullptr;
    }

    // Diagnostic
    if (buf->diagnostic != nullptr) {
        lsp_free_diagnostics(buf->diagnostic);
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
 * Helper internal untuk cek apakah Comment [PRIVATE API]
 */
bool is_node_comment(TSNode node) {
    while (!ts_node_is_null(node)) {
        const char *type = ts_node_type(node);

        if (strstr(type, "comment")) {
            return true;
        }
        node = ts_node_parent(node);
    }
    return false;
}

/**
 * Helper internal untuk mencari Posisi di Tree-sitter [PRIVATE API]
 */
bool is_position_in_comment(TSTree *tree, uint32_t start_byte, uint32_t end_byte) {
    if (!tree) return false;

    TSNode root = ts_tree_root_node(tree);
    TSNode node = ts_node_named_descendant_for_byte_range(root, start_byte, end_byte);
    return is_node_comment(node);
}

/**
 * Fungsi untuk akomodasi Search [PUBLIC API]
 */
int Buffer_search(Buffer *buf, const char *query, SearchHitBuffer *out, int max_hits) {
    if (!buf || !query || !query[0] || !out || max_hits <= 0) return 0;

    int count = 0;
    size_t q_len = strlen(query);

    for (size_t y = 0; y < buf->lines.line_count && count < max_hits; y++) {
        char *line = Buffer_get_line_text(buf, y);
        if (!line) continue;

        const char *p = line;
        size_t line_start_byte = buf->lines.offset[y];
        while ((p = strcasestr(p, query)) != nullptr) {
            size_t col = (size_t)(p - line);
            uint32_t match_start_byte = (uint32_t)(line_start_byte + col);
            uint32_t match_end_byte = match_start_byte + (uint32_t)q_len;

            if (buf->state &&
                is_position_in_comment(buf->state->tree, match_start_byte, match_end_byte)) {
                p += (q_len > 0 ? q_len : 1);
                continue;  // Lanjut cari kata kunci berikutnya tanpa dimasukkan ke
                           // results
            }

            SearchHitBuffer *h = &out[count++];
            h->line = y;
            h->col = col;

            snprintf(h->label, sizeof(h->label), "[%zu:%zu] %.20s...", y + 1, col + 1, p);

            p += (q_len > 0 ? q_len : 1);
            if (count >= max_hits) break;
        }
        free(line);
    }
    return count;
}
