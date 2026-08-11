#ifndef LSP_CONFIG_H
#define LSP_CONFIG_H

// Enum untuk type bahasa
typedef enum { GO, C, GENERAL } LangType;

/**
 * Struct untuk data Config LSP dan Tree-sitter saat Open File
 */
typedef struct {
    LangType lang;
    char *query_source;
    char *indent_source;
    const char *language_id;
    char **lsp_args;
    char *path_lsp;
} LangConfig;

LangConfig *LspConfig_detail(const char *filepath);
void LangConfig_free(LangConfig *config);

#endif
