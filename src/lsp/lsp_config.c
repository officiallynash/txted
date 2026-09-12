/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "lsp_config.h"

#include <raylib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern const TSLanguage *tree_sitter_c(void);  // Extern tree-sitter-c

/* *
 * Fungsi mencari executable dalam PATH [PRIVATE API]
 */
char *find_executable_in_path(const char *exec_name) {
    char *path_env = getenv("PATH");
    if (path_env) {
        char *path_copy = strdup(path_env);
        char *dir = strtok(path_copy, ":");
        char full_path[1024] = {0};

        while (dir != nullptr) {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, exec_name);
            if (access(full_path, X_OK) == 0) {
                free(path_copy);
                return strdup(full_path);
            }
            dir = strtok(nullptr, ":");
        }
        free(path_copy);
    }
    return nullptr;
}

/**
 * Helper internal untuk load Syntax Query (Tree Sitter)
 */
static char *Syntax_query(const char *lang_id, const char *scm_filename) {
    char path[256] = {0};
    snprintf(path, sizeof(path), "%squeries/%s/%s", GetApplicationDirectory(), lang_id,
             scm_filename);

    // Sebenarnya akan lebih bagus pakai Result dan FS_open
    // Tapi casting dari unsigned char * ke char * malah bikin error
    // Jadi, terpaksa pakai manual
    FILE *fp = fopen(path, "r");
    if (!fp) return nullptr;

    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = calloc(fsz + 1, sizeof(char));
    size_t got = fread(buf, 1, fsz, fp);
    buf[got] = '\0';
    fclose(fp);

    return buf;
}

/**
 * Fungsi detail LSP config [PUBLIC API]
 */
LangConfig *LspConfig_detail(const char *filepath) {
    if (!filepath) return nullptr;

    static char *clangd_args[] = {
        "clangd",       "--background-index",          "--header-insertion=iwyu",
        "--clang-tidy", "--completion-style=detailed", nullptr};

    // Cari titik '.' paling akhir
    const char *dot = strrchr(filepath, '.');
    if (!dot || dot == filepath) return nullptr;

    LangConfig *config = calloc(1, sizeof(LangConfig));

    if (strcmp(dot + 1, "c") == 0 || strcmp(dot + 1, "h") == 0) {
        config->lang = tree_sitter_c();
        config->language_id = "c";
        config->path_lsp = find_executable_in_path("clangd");
        config->lsp_args = clangd_args;
        config->query_source = Syntax_query(config->language_id, "highlights.scm");
        config->indent_source = Syntax_query(config->language_id, "indents.scm");
    } else {
        free(config);
        return nullptr;  // Kalau ga ada return NULL aja HHAHAHA
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
