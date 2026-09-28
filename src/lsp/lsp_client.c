/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "fs.h"
#include "lsp.h"
#include "matches.h"
#include "notification.h"
#include "raylib.h"
#include "result.h"
#include "rope.h"
#include "types.h"

// Untuk optimasi compare byte
#define XOR_CHECK(src, val) ((src ^ val) == 0)

// Internal state
float lsp_debounce_timer = 0.0f;  // Debounce
LspUiState g_lsp_ui = {};

/**
 * Fungsi untuk membersihkan completion
 */
static void lsp_ui_clear_completion(void) {
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
        lsp_free_completion(&g_lsp_ui.completion);
        g_lsp_ui.completion.items = nullptr;
        g_lsp_ui.completion.count = 0;

        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);
    }
}

/**
 * Fungsi untuk inisialisasi LSP UI [PRIVATE API]
 */
Result lsp_ui_init(const char *lsp_path, char **argv) {
    lsp_ui_clear_completion();
    // Amankan dulu root uri
    char *saved_root_uri = g_lsp_ui.root_uri;

    g_lsp_ui.lsp_flag = 0;
    g_lsp_ui.selected_index = 0;
    g_lsp_ui.root_uri = saved_root_uri;
    g_lsp_ui.sig_y = 0;

    // Set flag LSP Enable
    SET_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE);

    // Inisiasi LSP dengan Result ala Rust
    if (!lsp_start(lsp_path, argv, g_lsp_ui.root_uri)) {
        return Err("LSP Gagal di Inisiasi!");
    } else {
        return Ok("LSP Berhasil di Inisiasi!");
    }
}

/**
 * Fungsi untuk Ensure Root URI [PUBLIC API]
 */
void Ensure_lsp_init(LangConfig *lang, const char *filepath) {
    if (!lang || !lang->path_lsp) {
        Notif_show("LSP server executable tidak ditemukan di PATH!", NOTIF_WARNING, 3.0f);
        return;
    }
    // Jika g_lsp_ui sudah di deklarasikan root_uri berarti sudah aktif si LSP
    if (g_lsp_ui.root_uri != nullptr) return;

    char *path_root = Fs_find_project_root(filepath);
    if (path_root) {
        char *temp_uri = Path_to_uri(path_root);

        if (g_lsp_ui.root_uri) {
            free(g_lsp_ui.root_uri);  // Jaga2 agar memori tidak di isi garbage
        }

        size_t len = strlen(temp_uri);
        if (len > 0 && !XOR_CHECK(temp_uri[len - 1], '/')) {
            g_lsp_ui.root_uri = calloc(len + 2, sizeof(char));
            snprintf(g_lsp_ui.root_uri, len + 2, "%s/", temp_uri);
            free(temp_uri);
        } else {
            g_lsp_ui.root_uri = temp_uri;
        }

        Result init_lsp = lsp_ui_init(lang->path_lsp, lang->lsp_args);
        if (init_lsp.type == RESULT_OK) {
            Notif_show(init_lsp.data, NOTIF_SUCCESS, 3.0f);
        } else {
            Notif_show(init_lsp.data, NOTIF_ERROR, 3.0f);
        }

        Result_free(&init_lsp);
        free(path_root);
    }
}

/**
 * Fungsi untuk shutdown LSP UI [PUBLIC API]
 */
void lsp_ui_shutdown(void) {
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE);
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);

        lsp_ui_clear_completion();
        lsp_stop();
        if (g_lsp_ui.root_uri != nullptr) {
            free(g_lsp_ui.root_uri);
            g_lsp_ui.root_uri = nullptr;
        }

        if (g_lsp_ui.filtered) {
            free(g_lsp_ui.filtered);
        }

        memset(&g_lsp_ui, 0, sizeof(g_lsp_ui));
    }
}

/**
 * Fungsi untuk menyembunyikan LSP UI [PUBLIC API]
 */
void lsp_ui_hide(void) {
    CLR_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
    CLR_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING);
    lsp_ui_clear_completion();

    // Sepertinya perlu di free disini, karena akan terjadi nullptr
    if (g_lsp_ui.filtered) {
        free(g_lsp_ui.filtered);
        g_lsp_ui.filtered = nullptr;
    }

    g_lsp_ui.item_count = 0;  // Set item count ke 0
}

/**
 * Fungsi untuk toggle LSP UI [PUBLIC API]
 */
void lsp_ui_toggle(void) {
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE)) return;
    SET_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);

    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE)) {
        SET_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING);
    } else {
        lsp_ui_clear_completion();
    }
}

/**
 * Fungsi untuk set document LSP UI [PUBLIC API]
 */
void lsp_ui_set_document(const char *uri, const char *language_id, const char *text) {
    if (!uri || !language_id || !text) return;

    snprintf(g_lsp_ui.uri, sizeof(g_lsp_ui.uri), "%s", uri);
    snprintf(g_lsp_ui.language_id, sizeof(g_lsp_ui.language_id), "%s", language_id);
    snprintf(g_lsp_ui.current_text, sizeof(g_lsp_ui.current_text), "%s", text);

    lsp_did_open(g_lsp_ui.uri, g_lsp_ui.language_id, g_lsp_ui.current_text);
    SET_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING);
}

/**
 * Fungsi untuk update LSP UI [PUBLIC API]
 */
void lsp_ui_update(BufManager *bufmgr, float dt) {
    if (!HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE)) return;

    // Proses Debouncer Timer
    if (lsp_debounce_timer > 0.0f) {
        lsp_debounce_timer -= dt;
        if (lsp_debounce_timer <= 0.0f) {
            // Timer habis -> Tandai request siap dikirim!
            SET_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING);
            lsp_debounce_timer = 0.0f;
            g_lsp_ui.selected_index = 0;  // Set selected index ke 0
        }
    }

    // Jika tidak visible dan tidak ada request pending, tidak perlu lakukan apa-apa
    if (!HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE) &&
        !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING) &&
        !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) && !HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG)) {
        return;
    }

    // Auto Hide control
    if (IsKeyPressed(KEY_ESCAPE)) {
        lsp_ui_hide();
        return;
    }

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) {
        lsp_ui_hide();
        return;
    }
    size_t rope_len = String_len(buf->str);

    // Ekseskusi Request LSP (Saat Debounce Selesai)
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_REQUEST_PENDING);

        // Set document ke LSP lebih baik di main thread
        // Hanya set ketika buf->path atau filepath tidak kosong
        if (buf->path) {
            char *uri = Path_to_uri((char *)buf->path);
            if (!uri) return;

            if (strcmp(g_lsp_ui.uri, uri) != 0) {
                Bytes text = String_get(buf->str, 0, rope_len);
                lsp_ui_set_document(uri, buf->language_id,
                                    text.data ? (const char *)text.data : "");
                Bytes_free(&text);
            }

            snprintf(g_lsp_ui.uri, sizeof(g_lsp_ui.uri), "%s", uri);
            free(uri);  // Safety free
        }
    }

    g_lsp_ui.last_line = (int)buf->cursor.y;
    g_lsp_ui.last_character = (int)buf->cursor.x;

    lsp_ui_clear_completion();

    if (!XOR_CHECK(g_lsp_ui.uri[0], '\0')) {
        Bytes text = String_get(buf->str, 0, rope_len);
        if (text.data) {
            lsp_did_change(g_lsp_ui.uri, (const char *)text.data, buf->lsp_version++);
            Bytes_free(&text);
        }

        char current_word[256] = {0};
        Buffer_get_current_word(buf, current_word, sizeof(current_word));

        char trigger_char = '\0';
        if (g_lsp_ui.last_character > 0) {
            // Hitung panjang prefix
            size_t word_len = strlen(current_word);

            if ((size_t)g_lsp_ui.last_character > word_len) {
                size_t trigger_col = (size_t)g_lsp_ui.last_character - word_len - 1;
                // Ambil 1 karakter tepat sebelum posisi kursor
                char prev_c = Buffer_get_char_at(buf, g_lsp_ui.last_line, trigger_col);

                // Cek apakah karakter tersebut merupakan trigger character LSP
                if (XOR_CHECK(prev_c, '.')) {
                    trigger_char = '.';
                } else if (XOR_CHECK(prev_c, '>') && trigger_col > 0) {
                    char prev_prev_c = Buffer_get_char_at(buf, g_lsp_ui.last_line, trigger_col - 1);
                    if (XOR_CHECK(prev_prev_c, '-')) {
                        trigger_char = '>';  // Valid operator ->
                    }
                } else if (XOR_CHECK(prev_c, ':') || XOR_CHECK(prev_c, '#')) {
                    trigger_char = prev_c;
                }
            }
        }

        if (XOR_CHECK(trigger_char, '\0') && strlen(current_word) < 1) {
            lsp_ui_hide();
            return;
        }

        // Baru minta completion
        g_lsp_ui.completion =
            lsp_completion(g_lsp_ui.uri, g_lsp_ui.last_line, g_lsp_ui.last_character, trigger_char);

        // Jika completion count lebih dari 0, set flag ke has completion
        if (g_lsp_ui.completion.count > 0) SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);

        if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
            bool is_visible = HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
            SET_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
            if (!is_visible) g_lsp_ui.selected_index = 0;

            // Jika filtered kosong, langsung alokasi lagi
            if (!g_lsp_ui.filtered) {
                g_lsp_ui.item_capacity = 512;
                g_lsp_ui.filtered = malloc(g_lsp_ui.item_capacity * sizeof(FilteredItem));
            }

            // Setting Filtered
            g_lsp_ui.item_count = 0;
            // Realloc kalau count lebih dari capacity
            if ((int)g_lsp_ui.completion.count > g_lsp_ui.item_capacity) {
                int new_cap = g_lsp_ui.item_capacity <<= 1;
                FilteredItem *new_item = realloc(g_lsp_ui.filtered, sizeof(FilteredItem) * new_cap);
                if (!new_item) return;

                g_lsp_ui.filtered = new_item;
                g_lsp_ui.item_capacity = new_cap;
            }

            // FILTER DAN HITUNG SKOR
            for (size_t i = 0; i < g_lsp_ui.completion.count; i++) {
                g_lsp_ui.filtered[i].label = g_lsp_ui.completion.items[i].label;
                g_lsp_ui.filtered[i].original_idx = (int)i;
                g_lsp_ui.filtered[i].item_ptr = &g_lsp_ui.completion.items[i];
            }

            g_lsp_ui.item_count = filter_and_sort_completion(current_word, g_lsp_ui.filtered,
                                                             g_lsp_ui.completion.count);

        } else {
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);
            CLR_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
        }
    }

    // Siganture Help
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);
            if (!uri) return;

            // Bebaskan signature lama jika ada
            lsp_free_signature_help(&g_lsp_ui.signature_help);

            // Request signature help baru dari LSP Server
            g_lsp_ui.signature_help =
                lsp_signature_help(uri, (int)buf->cursor.y, (int)buf->cursor.x);

            // Set flag status agar UI siap me-render
            if (g_lsp_ui.signature_help.count > 0) {
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);
            } else {
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_SIG);
            }

            free(uri);  // Safety free
        }
    }

    // Hover
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING);

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);
            if (!uri) return;

            lsp_free_hover(&g_lsp_ui.hover);
            g_lsp_ui.hover = lsp_hover(uri, (int)buf->cursor.y, (int)buf->cursor.x);

            if (g_lsp_ui.hover.contents && strlen(g_lsp_ui.hover.contents) > 0) {
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
            } else {
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
            }

            free(uri);  // Safety free
        }
    }

    // Auto hide jika sudah pindah baris
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
        if (buf->cursor.y != (size_t)g_lsp_ui.last_line) lsp_ui_hide();
    }
}
