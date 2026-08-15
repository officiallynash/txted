/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <asm-generic/errno.h>
#include <stdbool.h>
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cJSON.h"
#include "lsp_server.h"
#define MAX_DIAG_DOCS 16

/* =============================
 * INTERNAL STATE
 * ============================= */

// Diagnostic
static DiagnosticList g_diagnostics[MAX_DIAG_DOCS];
static int g_diag_count = 0;
extern char **environ;

// Pipe in lsp
static int stdin_fd = -1;
static int stdout_fd = -1;
static pid_t lsp_pid = -1;

static pthread_t reader_thread;
static volatile bool running = false;

// Mutex Guard
static pthread_mutex_t diag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pending_cond = PTHREAD_COND_INITIALIZER;

// Simple pending response (hanya 1 request aktif untuk completion)
static int pending_id = -1;
static cJSON *pending_result = NULL;
static bool response_received = false;

/* ================================
 * PRIVATE API
 * ================================ */

/**
 * Fungsi untuk mengirimkan request LSP [PRIVATE API]
 */
static void lsp_send_raw(const char *json) {
    char header[64];
    int len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(json));
    write(stdin_fd, header, len);
    write(stdin_fd, json, strlen(json));
}

/**
 * Fungsi untuk mendapatkan ID berikutnya [PRIVATE API]
 */
static int next_id(void) {
    static int id = 1;
    return id++;
}

/* ------------------------------------------------------- *
 * DIAGNOSTIC
 * ------------------------------------------------------- */

/**
 * Fungsi untuk membebaskan alokasi diagnostik [PRIVATE API]
 */
static void free_one_diagnostic_list(DiagnosticList *dl) {
    if (!dl) return;
    for (size_t i = 0; i < dl->count; i++) {
        free(dl->items[i].message);
        free(dl->items[i].source);
    }
    free(dl->items);
    dl->items = NULL;
    dl->count = 0;
    dl->uri[0] = '\0';
}

/**
 * Fungsi untuk membebaskan alokasi diagnostic [PUBLIC API]
 */
void lsp_free_diagnostics(DiagnosticList *list) { free_one_diagnostic_list(list); }

/**
 * Fungsi untuk menghapus semua diagnostic [PRIVATE API]
 */
void lsp_clear_all_diagnostics(void) {
    pthread_mutex_lock(&diag_mutex);
    for (int i = 0; i < g_diag_count; i++) {
        free_one_diagnostic_list(&g_diagnostics[i]);
    }
    g_diag_count = 0;
    pthread_mutex_unlock(&diag_mutex);
}

/**
 * Fungsi untuk mendapatkan diagnostic berdasarkan URI [PUBLIC API]
 */
DiagnosticList *lsp_get_diagnostics(const char *uri) {
    if (!uri) return NULL;

    pthread_mutex_lock(&diag_mutex);
    for (int i = 0; i < g_diag_count; i++) {
        if (strcmp(g_diagnostics[i].uri, uri) == 0) {
            pthread_mutex_unlock(&diag_mutex);
            return &g_diagnostics[i];
        }
    }
    pthread_mutex_unlock(&diag_mutex);
    return NULL;
}

/**
 * Simpan diagnostik dari LSP response
 */
static void store_diagnostics(const char *uri, cJSON *diagnostics_array) {
    if (!uri || !diagnostics_array || !cJSON_IsArray(diagnostics_array)) return;

    pthread_mutex_lock(&diag_mutex);  // Kasih pengaman dulu

    // Cari slot yang sama URI-nya, atau slot kosong
    int slot = -1;
    for (int i = 0; i < g_diag_count; i++) {
        if (strcmp(g_diagnostics[i].uri, uri) == 0) {
            slot = i;
            break;
        }
    }

    // Jika slot kosong
    if (slot < 0) {
        if (g_diag_count >= MAX_DIAG_DOCS) {
            // Buang yang paling lama
            free_one_diagnostic_list(&g_diagnostics[0]);
            memmove(&g_diagnostics[0], &g_diagnostics[1],
                    (g_diag_count - 1) * sizeof(DiagnosticList));
            g_diag_count--;
        }
        slot = g_diag_count++;
    } else {
        free_one_diagnostic_list(&g_diagnostics[slot]);
    }

    DiagnosticList *dl = &g_diagnostics[slot];
    snprintf(dl->uri, sizeof(dl->uri), "%s", uri);

    int n = cJSON_GetArraySize(diagnostics_array);
    if (n <= 0) {
        dl->items = NULL;
        dl->count = 0;
        pthread_mutex_unlock(&diag_mutex);
        return;
    }

    dl->items = calloc(n, sizeof(DiagnosticItem));
    dl->count = 0;

    for (int i = 0; i < n; i++) {
        cJSON *d = cJSON_GetArrayItem(diagnostics_array, i);
        if (!d) continue;

        cJSON *range = cJSON_GetObjectItem(d, "range");
        if (!range) continue;

        cJSON *start = cJSON_GetObjectItem(range, "start");
        cJSON *end = cJSON_GetObjectItem(range, "end");
        if (!start || !end) continue;

        DiagnosticItem *item = &dl->items[dl->count++];

        item->start_line =
            cJSON_GetObjectItem(start, "line") ? cJSON_GetObjectItem(start, "line")->valueint : 0;
        item->start_char = cJSON_GetObjectItem(start, "character")
                               ? cJSON_GetObjectItem(start, "character")->valueint
                               : 0;
        item->end_line =
            cJSON_GetObjectItem(end, "line") ? cJSON_GetObjectItem(end, "line")->valueint : 0;
        item->end_char = cJSON_GetObjectItem(end, "character")
                             ? cJSON_GetObjectItem(end, "character")->valueint
                             : 0;

        cJSON *sev = cJSON_GetObjectItem(d, "severity");
        item->severity = (sev && cJSON_IsNumber(sev)) ? sev->valueint : LSP_SEVERITY_ERROR;

        cJSON *msg = cJSON_GetObjectItem(d, "message");
        item->message = (msg && cJSON_IsString(msg)) ? strdup(msg->valuestring) : strdup("");

        cJSON *src = cJSON_GetObjectItem(d, "source");
        item->source = (src && cJSON_IsString(src)) ? strdup(src->valuestring) : NULL;
    }

    pthread_mutex_unlock(&diag_mutex);
}

/* ------------------------------------------------------------- *
 * COMPLETION
 * ------------------------------------------------------------- */

/**
 * Fungsi untuk mendapatkan hasil LSP [PRIVATE API]
 */
CompletionList lsp_completion(const char *uri, int line, int character, char trigger_char) {
    CompletionList list = {0};

    int id = next_id();

    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    cJSON_AddItemToObject(params, "position", pos);

    // Trigger character
    cJSON *context = cJSON_CreateObject();
    if (trigger_char != '\0') {
        cJSON_AddNumberToObject(context, "triggerKind", 2);  // TriggerCharacter
        char char_str[2] = {trigger_char, '\0'};
        cJSON_AddStringToObject(context, "triggerCharacter", char_str);
    } else {
        cJSON_AddNumberToObject(context, "triggerKind", 1);  // Invoked manual
    }
    cJSON_AddItemToObject(params, "context", context);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", "textDocument/completion");
    cJSON_AddItemToObject(req, "params", params);

    // Lock sebelum baca dan write
    pthread_mutex_lock(&pending_mutex);
    pending_id = id;
    response_received = false;
    if (pending_result) {
        cJSON_Delete(pending_result);
        pending_result = NULL;
    }

    char *json = cJSON_PrintUnformatted(req);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(req);

    // Set Timeout 1 Detik
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;

    // Tunggu sampai response_received bernilai true
    while (!response_received && running) {
        if (pthread_cond_timedwait(&pending_cond, &pending_mutex, &ts) == ETIMEDOUT) {
            break;
        }
    }

    cJSON *result = pending_result;
    pending_result = NULL;
    pending_id = -1;
    pthread_mutex_unlock(&pending_mutex);

    if (!result) return list;

    // Parsing result
    cJSON *items = NULL;
    if (cJSON_IsArray(result)) {
        items = result;
    } else if (cJSON_IsObject(result)) {
        items = cJSON_GetObjectItem(result, "items");
    }

    if (!items || !cJSON_IsArray(items)) {
        cJSON_Delete(result);
        return list;
    }

    int n = cJSON_GetArraySize(items);
    if (n > 0) {
        list.items = calloc(n, sizeof(CompletionItem));
        list.count = 0;

        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(items, i);
            if (!item) continue;

            cJSON *label = cJSON_GetObjectItem(item, "label");
            if (!cJSON_IsString(label) || !label->valuestring) continue;

            CompletionItem *ci = &list.items[list.count++];
            ci->label = strdup(label->valuestring);

            cJSON *insert = cJSON_GetObjectItem(item, "insertText");
            if (cJSON_IsString(insert) && insert->valuestring) {
                ci->insert_text = strdup(insert->valuestring);
            } else {
                ci->insert_text = strdup(label->valuestring);  // Fallback ke label
            }

            cJSON *detail = cJSON_GetObjectItem(item, "detail");
            if (cJSON_IsString(detail) && detail->valuestring) {
                ci->detail = strdup(detail->valuestring);
            } else {
                ci->detail = NULL;
            }

            ci->header_include = NULL;
            cJSON *add_edits = cJSON_GetObjectItem(item, "additionalTextEdits");
            if (add_edits && cJSON_IsArray(add_edits)) {
                cJSON *first_edit = cJSON_GetArrayItem(add_edits, 0);
                if (first_edit) {
                    cJSON *new_text = cJSON_GetObjectItem(first_edit, "newText");
                    if (cJSON_IsString(new_text) && new_text->valuestring) {
                        ci->header_include = strdup(new_text->valuestring);
                    }
                }
            }
        }
    }

    cJSON_Delete(result);
    return list;
}

/* -------------------------------------------------------- *
 * SIGNATURE HELP
 * -------------------------------------------------------- */

/**
 * Fungsi untuk SignatureHelp
 */
SignatureHelp lsp_signature_help(const char *uri, int line, int character) {
    SignatureHelp help = {0};

    int id = next_id();

    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    cJSON_AddItemToObject(params, "position", pos);

    // Context tambahan
    cJSON *context = cJSON_CreateObject();
    cJSON_AddBoolToObject(context, "isRetrigger", false);
    cJSON_AddStringToObject(context, "triggerCharacter", "(");
    cJSON_AddItemToObject(params, "context", context);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", "textDocument/signatureHelp");
    cJSON_AddItemToObject(req, "params", params);

    // kirim + tunggu response (sama seperti completion) ---
    pthread_mutex_lock(&pending_mutex);
    pending_id = id;
    response_received = false;
    if (pending_result) {
        cJSON_Delete(pending_result);
        pending_result = NULL;
    }

    char *json = cJSON_PrintUnformatted(req);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(req);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;

    while (!response_received && running) {
        if (pthread_cond_timedwait(&pending_cond, &pending_mutex, &ts) == ETIMEDOUT) break;
    }

    cJSON *result = pending_result;
    pending_result = NULL;
    pending_id = -1;
    pthread_mutex_unlock(&pending_mutex);

    if (!result || !cJSON_IsObject(result)) {
        if (result) cJSON_Delete(result);
        return help;
    }

    cJSON *signatures = cJSON_GetObjectItem(result, "signatures");
    if (!signatures || !cJSON_IsArray(signatures)) {
        cJSON_Delete(result);
        return help;
    }

    int n = cJSON_GetArraySize(signatures);
    if (n <= 0) {
        cJSON_Delete(result);
        return help;
    }

    help.items = calloc(n, sizeof(SignatureItem));
    help.count = 0;

    cJSON *active_sig = cJSON_GetObjectItem(result, "activeSignature");
    help.active_signature = (active_sig && cJSON_IsNumber(active_sig)) ? active_sig->valueint : 0;

    for (int i = 0; i < n; i++) {
        cJSON *sig = cJSON_GetArrayItem(signatures, i);
        if (!sig) continue;

        cJSON *label = cJSON_GetObjectItem(sig, "label");
        if (!cJSON_IsString(label)) continue;

        SignatureItem *item = &help.items[help.count++];
        item->label = strdup(label->valuestring);

        // Read active parameter
        cJSON *active_param = cJSON_GetObjectItem(sig, "activeParameter");
        if (!active_param) active_param = cJSON_GetObjectItem(result, "activeParameter");
        item->active_parameter =
            (active_param && cJSON_IsNumber(active_param)) ? active_param->valueint : 0;

        // Read documentation
        cJSON *doc = cJSON_GetObjectItem(sig, "documentation");
        if (cJSON_IsString(doc)) {
            item->documentation = strdup(doc->valuestring);
        } else if (cJSON_IsObject(doc)) {
            cJSON *value = cJSON_GetObjectItem(doc, "value");
            if (cJSON_IsString(value)) item->documentation = strdup(value->valuestring);
        }

        // Read parameters array
        cJSON *params_arr = cJSON_GetObjectItem(sig, "parameters");
        if (params_arr && cJSON_IsArray(params_arr)) {
            int p_count = cJSON_GetArraySize(params_arr);
            item->parameters = calloc(p_count, sizeof(ParameterInfo));
            item->parameter_count = 0;

            for (int j = 0; j < p_count; j++) {
                cJSON *p_item = cJSON_GetArrayItem(params_arr, j);
                if (!p_item) continue;

                ParameterInfo *p_info = &item->parameters[item->parameter_count++];

                // Check label offset [start, end]
                cJSON *p_label = cJSON_GetObjectItem(p_item, "label");
                if (p_label && cJSON_IsArray(p_label) && cJSON_GetArraySize(p_label) == 2) {
                    p_info->start = cJSON_GetArrayItem(p_label, 0)->valueint;
                    p_info->end = cJSON_GetArrayItem(p_label, 1)->valueint;
                } else if (p_label && cJSON_IsString(p_label) && p_label->valuestring) {
                    // Cari posisi offset substring di dalam string label utama
                    const char *param_str = p_label->valuestring;
                    const char *match = strstr(item->label, param_str);
                    if (match) {
                        p_info->start = (int)(match - item->label);
                        p_info->end = p_info->start + (int)strlen(param_str);
                    } else {
                        p_info->start = -1;
                        p_info->end = -1;
                    }
                } else {
                    p_info->start = -1;
                    p_info->end = -1;
                }

                // Parameter Doc
                cJSON *p_doc = cJSON_GetObjectItem(p_item, "documentation");
                if (cJSON_IsString(p_doc)) {
                    p_info->documentation = strdup(p_doc->valuestring);
                } else if (cJSON_IsObject(p_doc)) {
                    cJSON *v = cJSON_GetObjectItem(p_doc, "value");
                    if (cJSON_IsString(v)) p_info->documentation = strdup(v->valuestring);
                }
            }
        }
    }

    cJSON_Delete(result);
    return help;
}

/**
 * Fungsi untuk Clear Signature Help
 **/
void lsp_free_signature_help(SignatureHelp *help) {
    if (!help || !help->items) return;
    for (size_t i = 0; i < help->count; i++) {
        free(help->items[i].label);
        free(help->items[i].documentation);
        if (help->items[i].parameters) {
            for (size_t j = 0; j < help->items[i].parameter_count; j++) {
                free(help->items[i].parameters[j].documentation);
            }
            free(help->items[i].parameters);
        }
    }
    free(help->items);
    help->items = NULL;
    help->count = 0;
}

/* ------------------------------------ *
 * Hover
 * ------------------------------------ */

/**
 * Fungsi untuk inisiasi Hover
 */
HoverInfo lsp_hover(const char *uri, int line, int character) {
    HoverInfo hover = {0};
    if (!uri) return hover;

    int id = next_id();

    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", character);
    cJSON_AddItemToObject(params, "position", pos);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", "textDocument/hover");
    cJSON_AddItemToObject(req, "params", params);

    // --- kirim + tunggu ---
    pthread_mutex_lock(&pending_mutex);
    pending_id = id;
    response_received = false;
    if (pending_result) {
        cJSON_Delete(pending_result);
        pending_result = NULL;
    }

    char *json = cJSON_PrintUnformatted(req);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(req);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;

    while (!response_received && running) {
        if (pthread_cond_timedwait(&pending_cond, &pending_mutex, &ts) == ETIMEDOUT) break;
    }

    cJSON *result = pending_result;
    pending_result = NULL;
    pending_id = -1;
    pthread_mutex_unlock(&pending_mutex);

    if (!result || cJSON_IsNull(result)) {
        if (result) cJSON_Delete(result);
        return hover;
    }

    // contents bisa string, MarkupContent, atau array
    cJSON *contents = cJSON_GetObjectItem(result, "contents");
    if (contents) {
        if (cJSON_IsString(contents)) {
            hover.contents = strdup(contents->valuestring);
        } else if (cJSON_IsObject(contents)) {
            // MarkupContent: { "kind": "markdown", "value": "..." }
            cJSON *value = cJSON_GetObjectItem(contents, "value");
            if (cJSON_IsString(value)) {
                hover.contents = strdup(value->valuestring);
            }
        } else if (cJSON_IsArray(contents)) {
            // Gabungkan semua string
            size_t total = 0;
            int n = cJSON_GetArraySize(contents);
            for (int i = 0; i < n; i++) {
                cJSON *item = cJSON_GetArrayItem(contents, i);
                if (cJSON_IsString(item))
                    total += strlen(item->valuestring) + 2;
                else if (cJSON_IsObject(item)) {
                    cJSON *v = cJSON_GetObjectItem(item, "value");
                    if (cJSON_IsString(v)) total += strlen(v->valuestring) + 2;
                }
            }
            hover.contents = malloc(total + 1);
            hover.contents[0] = '\0';
            for (int i = 0; i < n; i++) {
                cJSON *item = cJSON_GetArrayItem(contents, i);
                const char *s = NULL;
                if (cJSON_IsString(item))
                    s = item->valuestring;
                else if (cJSON_IsObject(item)) {
                    cJSON *v = cJSON_GetObjectItem(item, "value");
                    if (cJSON_IsString(v)) s = v->valuestring;
                }
                if (s) {
                    if (hover.contents[0]) strcat(hover.contents, "\n");
                    strcat(hover.contents, s);
                }
            }
        }
    }

    // range (opsional)
    cJSON *range = cJSON_GetObjectItem(result, "range");
    if (range) {
        cJSON *start = cJSON_GetObjectItem(range, "start");
        cJSON *end = cJSON_GetObjectItem(range, "end");
        if (start && end) {
            hover.has_range = true;
            hover.start_line = cJSON_GetObjectItem(start, "line")->valueint;
            hover.start_char = cJSON_GetObjectItem(start, "character")->valueint;
            hover.end_line = cJSON_GetObjectItem(end, "line")->valueint;
            hover.end_char = cJSON_GetObjectItem(end, "character")->valueint;
        }
    }

    cJSON_Delete(result);
    return hover;
}

/**
 * Fungsi untuk menghapus Hover data
 */
void lsp_free_hover(HoverInfo *hover) {
    if (!hover) return;
    free(hover->contents);
    hover->contents = NULL;
    hover->has_range = false;
}

/* ------------------------------------ *
 * AUTO FORMAT
 * ------------------------------------ */

/**
 * Fungsi untuk Auto Format
 */
TextEditList lsp_format(const char *uri, int tab_size, bool insert_spaces) {
    TextEditList list = {0};
    if (!uri) return list;

    int id = next_id();

    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *options = cJSON_CreateObject();
    cJSON_AddNumberToObject(options, "tabSize", tab_size);
    cJSON_AddBoolToObject(options, "insertSpaces", insert_spaces);
    cJSON_AddItemToObject(params, "options", options);

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", id);
    cJSON_AddStringToObject(req, "method", "textDocument/formatting");
    cJSON_AddItemToObject(req, "params", params);

    // --- kirim + tunggu ---
    pthread_mutex_lock(&pending_mutex);
    pending_id = id;
    response_received = false;
    if (pending_result) {
        cJSON_Delete(pending_result);
        pending_result = NULL;
    }

    char *json = cJSON_PrintUnformatted(req);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(req);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 3;  // formatting kadang lebih lama

    while (!response_received && running) {
        if (pthread_cond_timedwait(&pending_cond, &pending_mutex, &ts) == ETIMEDOUT) break;
    }

    cJSON *result = pending_result;
    pending_result = NULL;
    pending_id = -1;
    pthread_mutex_unlock(&pending_mutex);

    if (!result) return list;

    // result = array of TextEdit
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(result);
        return list;
    }

    int n = cJSON_GetArraySize(result);
    if (n <= 0) {
        cJSON_Delete(result);
        return list;
    }

    list.edits = calloc(n, sizeof(TextEdit));
    list.count = 0;

    for (int i = 0; i < n; i++) {
        cJSON *edit = cJSON_GetArrayItem(result, i);
        if (!edit) continue;

        cJSON *range = cJSON_GetObjectItem(edit, "range");
        cJSON *new_text = cJSON_GetObjectItem(edit, "newText");
        if (!range || !cJSON_IsString(new_text)) continue;

        cJSON *start = cJSON_GetObjectItem(range, "start");
        cJSON *end = cJSON_GetObjectItem(range, "end");
        if (!start || !end) continue;

        TextEdit *te = &list.edits[list.count++];
        te->start_line = cJSON_GetObjectItem(start, "line")->valueint;
        te->start_char = cJSON_GetObjectItem(start, "character")->valueint;
        te->end_line = cJSON_GetObjectItem(end, "line")->valueint;
        te->end_char = cJSON_GetObjectItem(end, "character")->valueint;
        te->new_text = strdup(new_text->valuestring);
    }

    cJSON_Delete(result);
    return list;
}

/**
 * Fungsi untuk free Auto formatting
 */
void lsp_free_text_edits(TextEditList *list) {
    if (!list || !list->edits) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->edits[i].new_text);
    }
    free(list->edits);
    list->edits = NULL;
    list->count = 0;
}

/* --------------------------------------------------- *
 * CONNECTION
 * --------------------------------------------------- */

/**
 * Fungsi untuk membaca response LSP [PRIVATE API]
 */
static void *reader_func(void *arg) {
    (void)arg;

    size_t capacity = 16384;
    char *buf = malloc(capacity);
    size_t buf_len = 0;

    while (running) {
        if (buf_len + 4096 > capacity) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }

        ssize_t n = read(stdout_fd, buf + buf_len, capacity - buf_len - 1);
        if (n <= 0) break;

        buf_len += n;
        buf[buf_len] = '\0';

        while (1) {
            char *header_end = strstr(buf, "\r\n\r\n");
            if (!header_end) break;

            int content_length = -1;
            char *line = buf;
            while (line < header_end) {
                if (strncmp(line, "Content-Length:", 15) == 0) {
                    content_length = atoi(line + 15);
                    break;
                }
                char *next = strstr(line, "\r\n");
                if (!next) break;
                line = next + 2;
            }

            if (content_length < 0) {
                // header rusak, buang sampai \r\n\r\n
                size_t skip = (header_end + 4) - buf;
                memmove(buf, header_end + 4, buf_len - skip);
                buf_len -= skip;
                continue;
            }

            size_t header_size = (header_end + 4) - buf;
            if (buf_len < header_size + (size_t)content_length) break;

            char *body = header_end + 4;
            char saved = body[content_length];
            body[content_length] = '\0';

            cJSON *msg = cJSON_Parse(body);
            body[content_length] = saved;

            if (msg) {
                // Error
                cJSON *error = cJSON_GetObjectItem(msg, "error");
                if (error) {
                    char *err_str = cJSON_PrintUnformatted(error);
                    fprintf(stderr, "[LSP Server Error] %s\n", err_str ? err_str : "(null)");
                    free(err_str);
                }

                cJSON *method = cJSON_GetObjectItem(msg, "method");

                if (method && cJSON_IsString(method) &&
                    strcmp(method->valuestring, "textDocument/publishDiagnostics") == 0) {
                    cJSON *params = cJSON_GetObjectItem(msg, "params");
                    if (params) {
                        cJSON *uri = cJSON_GetObjectItem(params, "uri");
                        cJSON *diagnostics = cJSON_GetObjectItem(params, "diagnostics");

                        if (uri && cJSON_IsString(uri) && diagnostics) {
                            store_diagnostics(uri->valuestring, diagnostics);
                        }
                    }
                }

                // Baca id
                cJSON *id_item = cJSON_GetObjectItem(msg, "id");
                if (id_item && cJSON_IsNumber(id_item)) {
                    int id = id_item->valueint;

                    pthread_mutex_lock(&pending_mutex);
                    if (pending_id == id) {
                        cJSON *res = cJSON_GetObjectItem(msg, "result");
                        if (res) {
                            if (pending_result) cJSON_Delete(pending_result);
                            pending_result = cJSON_Duplicate(res, true);
                        }
                        response_received = true;
                        pthread_cond_signal(&pending_cond);
                    }
                    pthread_mutex_unlock(&pending_mutex);
                }
                cJSON_Delete(msg);
            }

            size_t remaining = buf_len - (header_size + content_length);
            memmove(buf, body + content_length, remaining);
            buf_len = remaining;
            buf[buf_len] = '\0';
        }
    }
    free(buf);
    return NULL;
}

/**
 * Fungsi untuk memulai LSP [PUBLIC API]
 */
bool lsp_start(const char *lsp_path, char **argv, const char *workspace_root) {
    if (!lsp_path || !workspace_root) return false;

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0 || pipe(out_pipe) < 0) return false;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);

    // argv handling
    char *default_argv[] = {(char *)lsp_path, NULL};
    char **final_argv = argv ? argv : default_argv;

    // Pastikan argv[0] = full path
    if (final_argv && final_argv[0]) {
        final_argv[0] = (char *)lsp_path;
    }

    if (posix_spawn(&lsp_pid, lsp_path, &actions, NULL, final_argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return false;
    }
    posix_spawn_file_actions_destroy(&actions);

    close(in_pipe[0]);
    close(out_pipe[1]);
    stdin_fd = in_pipe[1];
    stdout_fd = out_pipe[0];

    running = true;
    pthread_create(&reader_thread, NULL, reader_func, NULL);

    // ---------- INITIALIZE ----------
    int init_id = next_id();

    pthread_mutex_lock(&pending_mutex);
    pending_id = init_id;
    pending_result = NULL;
    response_received = false;
    pthread_mutex_unlock(&pending_mutex);

    cJSON *params = cJSON_CreateObject();

    cJSON_AddNumberToObject(params, "processId", getpid());
    cJSON_AddStringToObject(params, "rootUri", workspace_root);

    // rootPath (buat jaga2)
    if (strncmp(workspace_root, "file://", 7) == 0) {
        cJSON_AddStringToObject(params, "rootPath", workspace_root + 7);
    } else {
        cJSON_AddStringToObject(params, "rootPath", workspace_root);
    }

    // clientInfo (sangat direkomendasikan)
    cJSON *client_info = cJSON_CreateObject();
    cJSON_AddStringToObject(client_info, "name", "MyEditor");
    cJSON_AddStringToObject(client_info, "version", "0.1.0");
    cJSON_AddItemToObject(params, "clientInfo", client_info);

    // workspaceFolders
    cJSON *workspace_folders = cJSON_CreateArray();
    cJSON *folder = cJSON_CreateObject();
    cJSON_AddStringToObject(folder, "uri", workspace_root);
    cJSON_AddStringToObject(folder, "name", "root");
    cJSON_AddItemToArray(workspace_folders, folder);
    cJSON_AddItemToObject(params, "workspaceFolders", workspace_folders);

    // ---------- CAPABILITIES ----------
    cJSON *caps = cJSON_CreateObject();

    // Workspace
    cJSON *workspace = cJSON_CreateObject();
    cJSON_AddBoolToObject(workspace, "workspaceFolders", true);
    cJSON_AddItemToObject(caps, "workspace", workspace);

    // Text Document
    cJSON *textDoc = cJSON_CreateObject();

    // Synchronization
    cJSON *sync = cJSON_CreateObject();
    cJSON_AddBoolToObject(sync, "didSave", true);
    cJSON_AddBoolToObject(sync, "willSave", false);
    cJSON_AddBoolToObject(sync, "willSaveWaitUntil", false);
    cJSON_AddItemToObject(textDoc, "synchronization", sync);

    // Completion
    cJSON *completion = cJSON_CreateObject();
    cJSON_AddBoolToObject(completion, "dynamicRegistration", false);
    cJSON_AddBoolToObject(completion, "contextSupport", true);
    cJSON_AddItemToObject(textDoc, "completion", completion);

    // Definition
    cJSON *definition = cJSON_CreateObject();
    cJSON_AddBoolToObject(definition, "dynamicRegistration", false);
    cJSON_AddItemToObject(textDoc, "definition", definition);

    cJSON_AddItemToObject(caps, "textDocument", textDoc);
    cJSON_AddItemToObject(params, "capabilities", caps);

    // SignatureHelp
    cJSON *signature_help = cJSON_CreateObject();
    cJSON_AddBoolToObject(signature_help, "dynamicRegistration", false);

    cJSON *trigger = cJSON_CreateObject();
    cJSON *chars = cJSON_CreateArray();
    cJSON_AddItemToArray(chars, cJSON_CreateString("("));
    cJSON_AddItemToArray(chars, cJSON_CreateString(","));
    cJSON_AddItemToObject(trigger, "triggerCharacters", chars);
    cJSON_AddItemToObject(signature_help, "triggerCharacters", chars);
    cJSON_AddItemToObject(textDoc, "signatureHelp", signature_help);

    // Hover
    cJSON *hover = cJSON_CreateObject();
    cJSON_AddBoolToObject(hover, "dynamicRegistration", false);

    cJSON *content_format = cJSON_CreateArray();
    cJSON_AddItemToArray(content_format, cJSON_CreateString("markdown"));
    cJSON_AddItemToArray(content_format, cJSON_CreateString("plaintext"));
    cJSON_AddItemToObject(hover, "contentFormat", content_format);

    cJSON_AddItemToObject(textDoc, "hover", hover);

    // Auto formatting
    cJSON *formatting = cJSON_CreateObject();
    cJSON_AddBoolToObject(formatting, "dynamicRegistration", false);
    cJSON_AddItemToObject(textDoc, "formatting", formatting);

    cJSON *range_formatting = cJSON_CreateObject();
    cJSON_AddBoolToObject(range_formatting, "dynamicRegistration", false);
    cJSON_AddItemToObject(textDoc, "rangeFormatting", range_formatting);

    // Request
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(req, "id", init_id);
    cJSON_AddStringToObject(req, "method", "initialize");
    cJSON_AddItemToObject(req, "params", params);

    char *json = cJSON_PrintUnformatted(req);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(req);

    // ---------- TUNGGU RESPONSE ----------
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 8;  // Maksimum timeout

    pthread_mutex_lock(&pending_mutex);
    while (!response_received && running) {
        if (pthread_cond_timedwait(&pending_cond, &pending_mutex, &ts) == ETIMEDOUT) {
            break;
        }
    }

    cJSON *init_result = pending_result;
    pending_result = NULL;
    pending_id = -1;
    response_received = false;
    pthread_mutex_unlock(&pending_mutex);

    if (!init_result) {
        return false;
    }

    cJSON_Delete(init_result);

    // ---------- initialized notification ----------
    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "initialized");
    cJSON_AddItemToObject(notif, "params", cJSON_CreateObject());

    json = cJSON_PrintUnformatted(notif);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(notif);

    return true;
}

/**
 * Fungsi untuk mengirimkan request LSP [PUBLIC API]
 */
void lsp_did_open(const char *uri, const char *language_id, const char *text) {
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddStringToObject(td, "languageId", language_id);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", text);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "textDocument/didOpen");
    cJSON_AddItemToObject(notif, "params", params);

    char *json = cJSON_PrintUnformatted(notif);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(notif);
}

/**
 * Fungsi untuk mengirimkan request LSP [PUBLIC API]
 */
void lsp_did_change(const char *uri, const char *text, int version) {
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddNumberToObject(td, "version", version++);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *change = cJSON_CreateObject();
    cJSON_AddStringToObject(change, "text", text);  // Full text replacement

    cJSON *changes = cJSON_CreateArray();
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(params, "contentChanges", changes);

    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "textDocument/didChange");
    cJSON_AddItemToObject(notif, "params", params);

    char *json = cJSON_PrintUnformatted(notif);
    lsp_send_raw(json);
    free(json);
    cJSON_Delete(notif);
}

/**
 * Fungsi untuk mengirim Close ke LSP [PUBLIC API]
 */
void lsp_did_close(const char *uri) {
    if (!uri) return;
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddItemToObject(params, "textDocument", td);

    cJSON *notif = cJSON_CreateObject();
    cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    cJSON_AddStringToObject(notif, "method", "textDocument/didClose");
    cJSON_AddItemToObject(notif, "params", params);

    char *json = cJSON_PrintUnformatted(notif);
    lsp_send_raw(json);  // Kirim raw JSONRPC
    free(json);
    cJSON_Delete(notif);
}

/**
 * Fungsi untuk membebaskan memori hasil LSP [PUBLIC API]
 */
void lsp_free_completion(CompletionList *list) {
    if (!list || !list->items) return;
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].label);
        free(list->items[i].insert_text);
        free(list->items[i].detail);
        free(list->items[i].header_include);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

/**
 * Fungsi untuk menghentikan LSP [PUBLIC API]
 */
void lsp_stop(void) {
    if (!running) return;
    lsp_clear_all_diagnostics();

    if (stdin_fd >= 0) {
        int shutdown_id = next_id();
        char shutdown_req[128];
        snprintf(shutdown_req, sizeof(shutdown_req),
                 "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\"}", shutdown_id);
        lsp_send_raw(shutdown_req);

        struct timespec req = {.tv_sec = 0, .tv_nsec = 50000000L};
        nanosleep(&req, NULL);

        const char *exit_notif = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\"}";
        lsp_send_raw(exit_notif);

        close(stdin_fd);
        stdin_fd = -1;
    }

    running = false;
    if (stdout_fd >= 0) {
        close(stdout_fd);
        stdout_fd = -1;
    }

    if (lsp_pid > 0) {
        waitpid(lsp_pid, NULL, 0);
        lsp_pid = -1;
    }

    pthread_join(reader_thread, NULL);
}
