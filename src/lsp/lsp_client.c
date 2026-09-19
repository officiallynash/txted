/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <cJSON.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "fs.h"
#include "lsp_config.h"
#include "lsp_server.h"
#include "lsp_ui.h"
#include "notification.h"
#include "raylib.h"
#include "result.h"
#include "rope.h"

// Struct untuk transport lsp did change
typedef struct {
    char uri[512];
    int version;
    char *text;
} LspUpdate;

// Internal state
float lsp_debounce_timer = 0.0f;  // Debounce
LspUiState g_lsp_ui = {};

extern int calculate_score(const char *query, const char *label);  // Deklarasi awal

/**
 * Fungsi untuk membandingkan score untuk qsort [PRIVATE API]
 */
int compare_scores(const void *a, const void *b) {
    FilteredItem *itemA = (FilteredItem *)a;
    FilteredItem *itemB = (FilteredItem *)b;
    return itemB->score - itemA->score;  // Descending (tertinggi di atas)
}

/**
 * Fungsi untuk memfilter dan mengurutkan completion [PRIVATE API]
 */
void filter_and_sort_completion(CompletionList *list, const char *query) {
    FilteredItem filtered[256] = {};
    int filtered_count = 0;

    for (size_t i = 0; i < list->count && filtered_count < 256; i++) {
        int score = calculate_score(query, list->items[i].label);
        if (score >= 0) {
            filtered[filtered_count].item = &list->items[i];
            filtered[filtered_count].score = score;
            filtered_count++;
        }
    }

    // Sort daftar pilihan berdasarkan score tertinggi!
    qsort(filtered, filtered_count, sizeof(FilteredItem), compare_scores);
}

/**
 * Mengambil item completion aktif sesuai urutan hasil Filter & Sort [PUBLIC API]
 */
CompletionItem *lsp_get_selected_item(const char *current_word) {
    if (!HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP) || g_lsp_ui.completion.count == 0)
        return nullptr;

    FilteredItem filtered[256] = {};
    int total_items = 0;

    for (size_t i = 0; i < g_lsp_ui.completion.count && total_items < 256; i++) {
        const char *label = g_lsp_ui.completion.items[i].label;
        if (!label) continue;

        int score = calculate_score(current_word, label);
        if (score >= 0) {
            filtered[total_items].item = &g_lsp_ui.completion.items[i];
            filtered[total_items].score = score;
            total_items++;
        }
    }

    if (total_items == 0) return nullptr;

    if (current_word[0] != '\0') {
        qsort(filtered, total_items, sizeof(FilteredItem), compare_scores);
    }

    if (g_lsp_ui.selected_index < 0 || g_lsp_ui.selected_index >= total_items) {
        return nullptr;
    }

    return filtered[g_lsp_ui.selected_index].item;
}

/**
 * Fungsi Scoring Pintar (Exact Case Bonus + Fuzzy)
 */
int calculate_score(const char *query, const char *label) {
    if (!query || !label) return -1;
    if (query[0] == '\0') return 0;

    size_t q_len = strlen(query);
    size_t l_len = strlen(label);

    // Prefix Matching
    if (strncasecmp(label, query, q_len) == 0) {
        int base_score = 1000;
        if (strncmp(label, query, q_len) == 0) base_score += 500;
        base_score -= (int)(l_len - q_len);
        return base_score;
    }

    // Substring Matching
    char *found = strcasestr(label, query);
    if (found != nullptr) {
        int base_score = 500;
        if (strncmp(found, query, q_len) == 0) base_score += 250;
        base_score -= (int)(found - label) * 10;
        base_score -= (int)(l_len - q_len);
        return base_score;
    }

    // Fuzzy matching untuk Snake_case & CamelCase
    int score = 0;
    const char *q = query;
    const char *l = label;
    bool is_first_char = true;

    while (*q && *l) {
        bool match = false;

        if (*q == *l) {
            score += 25;  // Exact case match
            match = true;
        } else if (tolower((unsigned char)*q) == tolower((unsigned char)*l)) {
            score += 10;  // Case-insensitive match
            match = true;
        }

        if (match) {
            if (is_first_char || *(l - 1) == '_' || isupper((unsigned char)*l)) {
                score += 40;
            }
            q++;
            is_first_char = false;
        } else {
            is_first_char = false;
        }
        l++;
    }

    if (*q != '\0') return -1;  // Tidak semua karakter query ketemu

    score -= (int)(l_len - q_len);
    return score;
}

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
    if (g_lsp_ui.root_uri != nullptr) {
        return;
    }

    char *path_root = Fs_find_project_root(filepath);
    if (path_root) {
        char *temp_uri = Path_to_uri(path_root);

        if (g_lsp_ui.root_uri) {
            free(g_lsp_ui.root_uri);  // Jaga2 agar memori tidak di isi garbage
        }

        size_t len = strlen(temp_uri);
        if (len > 0 && temp_uri[len - 1] != '/') {
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

/*
 * Fungsi untuk worker thread LSP
 */
void *Lsp_update_worker(void *args) {
    LspUpdate *lsp = (LspUpdate *)args;

    // Kirim lsp did change
    lsp_did_change(lsp->uri, lsp->text, lsp->version);

    // Free lsp
    if (lsp) {
        free(lsp->text);
        free(lsp);
    }

    return nullptr;
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
        if (buf->path) {
            char *uri = Path_to_uri((char *)buf->path);
            if (uri) {
                if (strcmp(g_lsp_ui.uri, uri) != 0) {
                    Bytes text = String_get(buf->str, 0, rope_len);
                    lsp_ui_set_document(uri, buf->language_id,
                                        text.data ? (const char *)text.data : "");
                    Bytes_free(&text);
                }
                snprintf(g_lsp_ui.uri, sizeof(g_lsp_ui.uri), "%s", uri);
                free(uri);
            }
        }

        g_lsp_ui.last_line = (int)buf->cursor.y;
        g_lsp_ui.last_character = (int)buf->cursor.x;

        lsp_ui_clear_completion();

        if (g_lsp_ui.uri[0] != '\0') {
            Bytes text = String_get(buf->str, 0, rope_len);
            if (text.data) {
                // Buat malloc
                LspUpdate *lsp = malloc(sizeof(LspUpdate));
                snprintf(lsp->uri, sizeof(lsp->uri), "%s", g_lsp_ui.uri);
                lsp->version = buf->lsp_version;
                lsp->text = strdup((char *)text.data);

                // Init Thread worker
                pthread_t thread;
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

                if (pthread_create(&thread, &attr, Lsp_update_worker, lsp) != 0) {
                    free(lsp->text);
                    free(lsp);
                }
                pthread_attr_destroy(&attr);

                buf->lsp_version++;  // Update lsp Version
                Bytes_free(&text);
            }

            char current_word[256] = {0};
            Buffer_get_current_word(buf, current_word, sizeof(current_word));

            char trigger_char = '\0';
            if (g_lsp_ui.last_character > 0) {
                // Ambil 1 karakter tepat sebelum posisi kursor
                char prev_c =
                    Buffer_get_char_at(buf, g_lsp_ui.last_line, g_lsp_ui.last_character - 1);

                // Cek apakah karakter tersebut merupakan trigger character LSP
                if (prev_c == '.') {
                    trigger_char = '.';
                } else if (prev_c == '>' && g_lsp_ui.last_character > 1) {
                    char prev_prev_c =
                        Buffer_get_char_at(buf, g_lsp_ui.last_line, g_lsp_ui.last_character - 2);
                    if (prev_prev_c == '-') {
                        trigger_char = '>';  // Valid operator ->
                    }
                } else if (prev_c == ':' || prev_c == '#') {
                    trigger_char = prev_c;
                }
            }

            if (trigger_char == '\0' && strlen(current_word) < 1) {
                lsp_ui_hide();
                return;
            }

            // Baru minta completion
            g_lsp_ui.completion = lsp_completion(g_lsp_ui.uri, g_lsp_ui.last_line,
                                                 g_lsp_ui.last_character, trigger_char);

            // Jika completion count lebih dari 0, set flag ke has completion
            if (g_lsp_ui.completion.count > 0) SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);

            if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
                g_lsp_ui.selected_index = 0;
            } else {
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP);
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE);
            }
        }
    }

    // Siganture Help
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_SIG_PENDING);

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);
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
            free(uri);
        }
    }

    // Hover
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING)) {
        CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HOV_PENDING);

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);

            lsp_free_hover(&g_lsp_ui.hover);
            g_lsp_ui.hover = lsp_hover(uri, (int)buf->cursor.y, (int)buf->cursor.x);

            if (g_lsp_ui.hover.contents && strlen(g_lsp_ui.hover.contents) > 0) {
                SET_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
            } else {
                CLR_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_HOVE);
            }

            free(uri);
        }
    }

    // Auto hide jika sudah pindah baris
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_VISIBLE) && HAS_FLAG(g_lsp_ui.lsp_flag, LSP_HAS_COMP)) {
        if (buf->cursor.y != (size_t)g_lsp_ui.last_line) {
            lsp_ui_hide();
        }
    }
}
