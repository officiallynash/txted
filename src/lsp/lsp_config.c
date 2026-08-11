#include "lsp_config.h"

#include <raylib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* *
 * Fungsi mencari executable dalam PATH [PRIVATE API]
 */
char *find_executable_in_path(const char *exec_name) {
    char *path_env = getenv("PATH");
    if (path_env) {
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        char full_path[1024];

        while (dir != NULL) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, exec_name);
            if (access(full_path, X_OK) == 0) {
                free(path_copy);
                return strdup(full_path);
            }
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }

    char *home = getenv("HOME");
    if (home) {
        char cargo_bin[1024];
        snprintf(cargo_bin, sizeof(cargo_bin), "%s/.cargo/bin/%s", home, exec_name);
        if (access(cargo_bin, X_OK) == 0) {
            return strdup(cargo_bin);
        }
    }

    return NULL;
}

static char *Syntax_query(const char *lang_id, const char *scm_filename) {
    char path[256];
    snprintf(path, sizeof(path), "%squeries/%s/%s", GetApplicationDirectory(), lang_id,
             scm_filename);
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = malloc(size + 1);
    fread(buf, 1, size, fp);
    fclose(fp);
    buf[size] = '\0';

    return buf;
}
/**
 * Fungsi detail LSP config [PUBLIC API]
 */
LangConfig *LspConfig_detail(const char *filepath) {
    if (!filepath) return NULL;

    static char *clangd_args[] = {
        "clangd",       "--background-index",          "--header-insertion=iwyu",
        "--clang-tidy", "--completion-style=detailed", NULL};

    static char *go_ls[] = {"gopls", NULL};
    // Cari titik '.' paling akhir
    const char *dot = strrchr(filepath, '.');
    if (!dot || dot == filepath) return NULL;

    LangConfig *config = malloc(sizeof(LangConfig));

    if (strcmp(dot + 1, "c") == 0 || strcmp(dot + 1, "h") == 0) {
        config->lang = C;
        config->language_id = "c";
        config->path_lsp = find_executable_in_path("clangd");
        config->lsp_args = clangd_args;
        config->query_source = Syntax_query(config->language_id, "highlights.scm");
        config->indent_source = Syntax_query(config->language_id, "indents.scm");
    } else if (strcmp(dot + 1, "go") == 0) {
        config->lang = GO;
        config->language_id = "go";
        config->path_lsp = find_executable_in_path("gopls");
        config->lsp_args = go_ls;
        config->query_source = Syntax_query(config->language_id, "highlights.scm");
        config->indent_source = Syntax_query(config->language_id, "indents.scm");
    } else {
        free(config);
        return NULL;  // Kalau ga ada return NULL aja HHAHAHA
    }

    return config;
}

/**
 * Fungsi free LSP config [PUBLIC API]
 */
void LangConfig_free(LangConfig *config) {
    if (!config) return;
    if (config->path_lsp) {
        free(config->path_lsp);
    }

    if (config->indent_source) free(config->indent_source);
    if (config->query_source) free(config->query_source);

    free(config);
}
