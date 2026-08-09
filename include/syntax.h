#ifndef SYNTAX_H
#define SYNTAX_H

#include <stdbool.h>
#include <tree_sitter/tree-sitter-c.h>
#include <tree_sitter/api.h>
#include "lsp_config.h"

typedef struct {
    TSParser *parser;
    TSTree *tree;
    bool is_enabled;
    TSQuery *query;
} SyntaxState;

typedef struct {
    uint32_t start_byte;
    uint32_t end_byte;
    const char *capture_name;  // "keyword", "string", "function", dll.
} HighlightToken;

SyntaxState *Syntax_init(LangConfig *lang);
void Syntax_update(SyntaxState *state, const char *text, size_t len);
void Syntax_free(SyntaxState *state);
int Syntax_get_highlights(SyntaxState *state, const char *source_code, uint32_t start_byte,
                          uint32_t end_byte, HighlightToken *out_tokens, int max_tokens);

#endif
