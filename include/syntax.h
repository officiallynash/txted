/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdbool.h>
#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

#include "lsp_config.h"

/**
 * Struct untuk state dari Tree-Sitter (Yang disimpan di Buffer)
 */
typedef struct {
    TSParser *parser;
    TSTree *tree;
    TSQuery *query;
    TSQuery *indents_query;
    bool is_enabled;
} SyntaxState;

/**
 * Struct untuk menampung HighlightToken
 */
typedef struct {
    const char *capture_name;  // "keyword", "string", "function", dll.
    uint32_t start_byte;
    uint32_t end_byte;
} HighlightToken;

SyntaxState *Syntax_init(LangConfig *lang);
void Syntax_update(SyntaxState *state, const char *text, size_t len);
void Syntax_free(SyntaxState *state);
int Syntax_get_highlights(SyntaxState *state, const char *source_code, uint32_t start_byte,
                          uint32_t end_byte, HighlightToken *out_tokens, int max_tokens);

#endif
