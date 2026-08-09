#ifndef LSP_CONFIG_H
#define LSP_CONFIG_H

typedef enum { GO, C, GENERAL } LangType;
typedef struct {
    LangType lang;
    const char *query_source;
    const char *language_id;
    char **lsp_args;
    char *path_lsp;
} LangConfig;

LangConfig *LspConfig_detail(const char *filepath);
void LangConfig_free(LangConfig *config);

#endif
