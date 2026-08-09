#include <cJSON.h>
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
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

float lsp_debounce_timer = 0.0f;  // Debounce
LspUiState g_lsp_ui = {0};
extern int compare_scores(const void *a, const void *b);  // compare_scores (completion.c)

/* ================================
 * PRIVATE API
 * ================================ */

/**
 * Fungsi Scoring Pintar (Exact Case Bonus + Fuzzy)
 */
int calculate_score(const char *query, const char *label) {
    if (!query || !label) return -1;
    if (query[0] == '\0') return 0;

    size_t q_len = strlen(query);
    size_t l_len = strlen(label);

    // PREFIX MATCHING
    if (strncasecmp(label, query, q_len) == 0) {
        int base_score = 1000;
        if (strncmp(label, query, q_len) == 0) base_score += 500;
        base_score -= (int)(l_len - q_len);
        return base_score;
    }

    // SUBSTRING MATCHING
    char *found = strcasestr(label, query);
    if (found != NULL) {
        int base_score = 500;
        if (strncmp(found, query, q_len) == 0) base_score += 250;
        base_score -= (int)(found - label) * 10;
        base_score -= (int)(l_len - q_len);
        return base_score;
    }

    // FUZZY MATCHING untuk Snake_case & CamelCase
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
    if (g_lsp_ui.has_completion) {
        lsp_free_completion(&g_lsp_ui.completion);
        g_lsp_ui.completion.items = NULL;
        g_lsp_ui.completion.count = 0;
        g_lsp_ui.has_completion = false;
    }
}

/**
 * Fungsi untuk inisialisasi LSP UI [PRIVATE API]
 */
Result lsp_ui_init(const char *lsp_path, char **argv) {
    lsp_ui_clear_completion();
    // Amankan dulu root uri
    char *saved_root_uri = g_lsp_ui.root_uri;

    g_lsp_ui.request_pending = false;
    g_lsp_ui.has_completion = false;
    g_lsp_ui.selected_index = 0;
    g_lsp_ui.root_uri = saved_root_uri;
    g_lsp_ui.enabled = true;
    g_lsp_ui.visible = false;
    g_lsp_ui.signature_pending = false;
    g_lsp_ui.has_signature = false;

    // Inisiasi LSP dengan Result ala Rust
    if (!lsp_start(lsp_path, argv, g_lsp_ui.root_uri)) {
        return Err("LSP Gagal di Inisiasi!");
    } else {
        return Ok("LSP Berhasil di Inisiasi!");
    }
}
/* ====================================
 * PUBLIC API
 * ==================================== */

/**
 * Fungsi untuk Ensure Root URI [PUBLIC API]
 */
void Ensure_lsp_init(LangConfig *lang, const char *filepath) {
    if (!lang || !lang->path_lsp) {
        Notif_show("LSP server executable tidak ditemukan di PATH!", NOTIF_WARNING, 3.0f);
        return;
    }
    if (g_lsp_ui.root_uri != NULL) {
        return;
    }

    char *path_root = Fs_find_project_root(filepath);
    if (path_root) {
        char *temp_uri = Path_to_uri(path_root);

        if (g_lsp_ui.root_uri) {
            free(g_lsp_ui.root_uri);
        }

        size_t len = strlen(temp_uri);
        if (len > 0 && temp_uri[len - 1] != '/') {
            g_lsp_ui.root_uri = malloc(len + 2);
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
    if (g_lsp_ui.enabled) {
        g_lsp_ui.enabled = false;
        g_lsp_ui.visible = false;
        lsp_ui_clear_completion();
        lsp_stop();
        if (g_lsp_ui.root_uri != NULL) {
            free(g_lsp_ui.root_uri);
            g_lsp_ui.root_uri = NULL;
        }

        memset(&g_lsp_ui, 0, sizeof(g_lsp_ui));
    }
}

/**
 * Fungsi untuk menyembunyikan LSP UI [PUBLIC API]
 */
void lsp_ui_hide(void) {
    g_lsp_ui.visible = false;
    g_lsp_ui.request_pending = false;
    lsp_ui_clear_completion();
}

/**
 * Fungsi untuk toggle LSP UI [PUBLIC API]
 */
void lsp_ui_toggle(void) {
    if (!g_lsp_ui.enabled) return;
    g_lsp_ui.visible = !g_lsp_ui.visible;
    if (g_lsp_ui.visible) {
        g_lsp_ui.request_pending = true;
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
    g_lsp_ui.request_pending = true;
}

/**
 * Fungsi untuk update LSP UI [PUBLIC API]
 */
void lsp_ui_update(BufManager *bufmgr, float dt) {
    if (!g_lsp_ui.enabled) return;

    // PROSES DEBOUNCE TIMER
    if (lsp_debounce_timer > 0.0f) {
        lsp_debounce_timer -= dt;
        if (lsp_debounce_timer <= 0.0f) {
            // Timer habis -> Tandai request siap dikirim!
            g_lsp_ui.request_pending = true;
            lsp_debounce_timer = 0.0f;
            g_lsp_ui.selected_index = 0;  // Set selected index ke 0
        }
    }

    // Jika tidak visible dan tidak ada request pending, tidak perlu lakukan apa-apa
    if (!g_lsp_ui.has_hover && !g_lsp_ui.request_pending && !g_lsp_ui.visible &&
        !g_lsp_ui.signature_pending) {
        return;
    }

    // AUTO-HIDE KONTROL
    if (IsKeyPressed(KEY_ESCAPE)) {
        lsp_ui_hide();
        return;
    }

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) {
        lsp_ui_hide();
        return;
    }

    // EKSEKUSI REQUEST LSP (Saat Debounce Selesai)
    if (g_lsp_ui.request_pending) {
        g_lsp_ui.request_pending = false;
        if (buf->path) {
            char *uri = Path_to_uri((char *)buf->path);
            if (uri) {
                if (strcmp(g_lsp_ui.uri, uri) != 0) {
                    Bytes text = String_get(buf->str, 0, buf->str->len);
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
            Bytes text = String_get(buf->str, 0, buf->str->len);
            if (text.data) {
                lsp_did_change(g_lsp_ui.uri, (const char *)text.data, buf->lsp_version);

                buf->lsp_version++;  // Update lsp Version
                Bytes_free(&text);
            }

            char trigger_char = '\0';
            if (g_lsp_ui.last_character > 0) {
                // Ambil 1 karakter tepat sebelum posisi kursor
                char prev_c =
                    Buffer_get_char_at(buf, g_lsp_ui.last_line, g_lsp_ui.last_character - 1);

                // Cek apakah karakter tersebut merupakan trigger character LSP
                if (prev_c == '.' || prev_c == '>' || prev_c == ':' || prev_c == '#') {
                    trigger_char = prev_c;
                }
            }
            // Baru minta completion
            g_lsp_ui.completion = lsp_completion(g_lsp_ui.uri, g_lsp_ui.last_line,
                                                 g_lsp_ui.last_character, trigger_char);
            g_lsp_ui.has_completion = (g_lsp_ui.completion.count > 0);
            if (g_lsp_ui.has_completion) {
                g_lsp_ui.visible = true;
                g_lsp_ui.selected_index = 0;
            } else {
                g_lsp_ui.has_completion = false;
                g_lsp_ui.visible = false;
            }
        }
    }

    // Siganture Help
    if (g_lsp_ui.signature_pending) {
        g_lsp_ui.signature_pending = false;

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);
            // Bebaskan signature lama jika ada
            lsp_free_signature_help(&g_lsp_ui.signature_help);

            // Request signature help baru dari LSP Server
            g_lsp_ui.signature_help =
                lsp_signature_help(uri, (int)buf->cursor.y, (int)buf->cursor.x);

            // Set flag status agar UI siap me-render
            if (g_lsp_ui.signature_help.count > 0) {
                g_lsp_ui.has_signature = true;
            } else {
                g_lsp_ui.has_signature = false;
            }
            free(uri);
        }
    }

    // Hover
    if (g_lsp_ui.hover_pending) {
        g_lsp_ui.hover_pending = false;

        if (buf && buf->path) {
            char *uri = Path_to_uri(buf->path);

            lsp_free_hover(&g_lsp_ui.hover);
            g_lsp_ui.hover = lsp_hover(uri, (int)buf->cursor.y, (int)buf->cursor.x);

            if (g_lsp_ui.hover.contents && strlen(g_lsp_ui.hover.contents) > 0) {
                g_lsp_ui.has_hover = true;
            } else {
                g_lsp_ui.has_hover = false;
            }

            free(uri);
        }
    }

    // AUTO-HIDE JIKA KURSOR PINDAH BARIS
    if (g_lsp_ui.visible && g_lsp_ui.has_completion) {
        if (buf->cursor.y != (size_t)g_lsp_ui.last_line) {
            lsp_ui_hide();
        }
    }
}
