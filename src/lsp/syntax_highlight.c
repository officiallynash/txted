#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>
#include <tree_sitter/tree-sitter-go.h>

#include "lsp_config.h"
#include "notification.h"
#include "syntax.h"

/**
 * Fungsi untuk inisialisasi state syntax highlighting [PUBLIC API]
 */
SyntaxState *Syntax_init(LangConfig *lang) {
    if (!lang) return NULL;
    SyntaxState *state = malloc(sizeof(SyntaxState));
    if (!state) return NULL;

    state->parser = ts_parser_new();
    state->query = NULL;  // Pastikan ter-nullify dulu

    uint32_t error_offset;
    TSQueryError error_type;

    const TSLanguage *ts_lang = NULL;
    switch (lang->lang) {
        case C:
            ts_lang = tree_sitter_c();
            break;
        case GO:
            ts_lang = tree_sitter_go();
            break;
        case GENERAL:
            ts_lang = NULL;
            break;
    }

    if (state->parser) {
        ts_parser_set_language(state->parser, ts_lang);
    }

    if (lang->query_source) {
        state->query = ts_query_new(ts_lang, lang->query_source, strlen(lang->query_source),
                                    &error_offset, &error_type);
        if (!state->query) {
            char msg[254];
            snprintf(msg, sizeof(msg),
                     "[TREE-SITTER ERROR] Failed at character index: %u, Error code: %d\n",
                     error_offset, error_type);

            Notif_show(msg, NOTIF_ERROR, 3.0f);
        }
    }
    if (lang->indent_source) {
        state->indents_query = ts_query_new(
            ts_lang, lang->indent_source, strlen(lang->indent_source), &error_offset, &error_type);

        if (!state->indents_query) {
            char msg[254];
            snprintf(msg, sizeof(msg),
                     "[TREE-SITTER ERROR] Failed at character index: %u, Error code: %d\n",
                     error_offset, error_type);

            Notif_show(msg, NOTIF_ERROR, 3.0f);
        }
    }

    state->tree = NULL;
    state->is_enabled = true;

    return state;
}

/**
 * Fungsi untuk memperbarui state syntax highlighting [PUBLIC API]
 */
void Syntax_update(SyntaxState *state, const char *text, size_t len) {
    if (!state || !state->is_enabled) return;

    TSTree *new_tree = ts_parser_parse_string(state->parser, state->tree, text, len);
    if (new_tree) {
        if (state->tree) ts_tree_delete(state->tree);
        state->tree = new_tree;
    }
}

/**
 * Fungsi untuk membersihkan state syntax highlighting [PUBLIC API]
 */
void Syntax_free(SyntaxState *state) {
    if (!state) return;
    if (state->tree) ts_tree_delete(state->tree);
    if (state->parser) ts_parser_delete(state->parser);
    if (state->query) ts_query_delete(state->query);
    if (state->indents_query) ts_query_delete(state->indents_query);
    free(state);
}

/**
 * Fungsi untuk mendapatkan highlight tokens [PUBLIC API]
 */
int Syntax_get_highlights(SyntaxState *state, const char *source_code, uint32_t start_byte,
                          uint32_t end_byte, HighlightToken *out_tokens, int max_tokens) {
    if (!state || !state->tree || !state->query || !source_code) return 0;

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);
    ts_query_cursor_exec(cursor, state->query, ts_tree_root_node(state->tree));

    TSQueryMatch match;
    int count = 0;

    while (ts_query_cursor_next_match(cursor, &match) && count < max_tokens) {
        for (int i = 0; i < match.capture_count; i++) {
            TSQueryCapture capture = match.captures[i];
            uint32_t c_len;
            const char *cap_name =
                ts_query_capture_name_for_id(state->query, capture.index, &c_len);

            out_tokens[count].start_byte = ts_node_start_byte(capture.node);
            out_tokens[count].end_byte = ts_node_end_byte(capture.node);
            out_tokens[count].capture_name = cap_name;
            count++;

            if (count >= max_tokens) break;
        }
    }

    ts_query_cursor_delete(cursor);  // Cuma cursor ringan yang dibersihkan
    return count;
}
