#include "lsp_config.h"

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

/**
 * Fungsi detail LSP config [PUBLIC API]
 */
LangConfig *LspConfig_detail(const char *filepath) {
    if (!filepath) return NULL;

    const char *go_highlights_query =
        ";; Keywords\n"
        "[\n"
        "  \"break\" \"case\" \"chan\" \"const\" \"continue\"\n"
        "  \"default\" \"defer\" \"else\" \"fallthrough\" \"for\"\n"
        "  \"func\" \"go\" \"goto\" \"if\" \"import\"\n"
        "  \"interface\" \"map\" \"package\" \"range\" \"return\"\n"
        "  \"select\" \"struct\" \"switch\" \"type\" \"var\"\n"
        "] @keyword\n\n"

        ";; Builtin Constants (Named Nodes)\n"
        "(nil) @keyword\n"
        "(true) @keyword\n"
        "(false) @keyword\n\n"

        ";; Types & Modules\n"
        "(type_identifier) @type\n"
        "(package_identifier) @type\n\n"

        ";; Strings & Literals\n"
        "(interpreted_string_literal) @string\n"
        "(raw_string_literal) @string\n"
        "(rune_literal) @string\n"
        "(int_literal) @number\n"
        "(float_literal) @number\n"
        "(comment) @comment\n\n"

        ";; Functions & Calls\n"
        "(function_declaration name: (identifier) @function)\n"
        "(method_declaration name: (field_identifier) @function)\n"
        "(call_expression function: (identifier) @function)\n"
        "(call_expression function: (selector_expression field: (field_identifier) @function))\n\n"

        ";; Operators\n"
        "[\n"
        "  \"+\" \"-\" \"*\" \"/\" \"%\" \"&\" \"|\" \"^\" \"<<\" \">>\" \"&^\"\n"
        "  \"+=\" \"-=\" \"*=\" \"/=\" \"%=\" \"&=\" \"|=\" \"^=\" \"<<=\" \">>=\" \"&^=\"\n"
        "  \"&&\" \"||\" \"<-\" \"++\" \"--\" \"==\" \"<\" \">\" \"=\" \"!\"\n"
        "  \"!=\" \"<=\" \">=\" \":=\" \"...\"\n"
        "] @operator\n\n"

        ";; Delimiters & Brackets\n"
        "[\n"
        "  \"(\" \")\" \"[\" \"]\" \"{\" \"}\"\n"
        "] @punctuation.bracket\n\n"

        "[\n"
        "  \".\" \",\" \";\" \":\"\n"
        "] @punctuation.delimiter\n\n"

        ";; Variables & Fields\n"
        "(field_identifier) @variable\n"
        "(identifier) @variable\n";

    const char *c_query_source =
        // 1. Types
        "(primitive_type) @type\n"
        "(type_identifier) @type\n"
        "[\"struct\" \"union\" \"enum\" \"typedef\" \"sizeof\" \"static\" \"const\" "
        "\"volatile\" \"extern\" \"inline\" \"register\" \"auto\" \"signed\" \"unsigned\" "
        "\"short\" \"long\"] @type\n"

        // 2. Keywords
        "[\"return\" \"if\" \"else\" \"while\" \"for\" \"do\" \"switch\" \"case\" \"default\" "
        "\"break\" \"continue\"] @keyword\n"
        "[\"struct\" \"union\" \"enum\" \"typedef\" \"sizeof\" \"static\" \"const\" \"volatile\" "
        "\"extern\" ] @keyword\n"

        // 3. Literals
        "(number_literal) @number\n"
        "(string_literal) @string\n"
        "(char_literal) @string\n"
        "(system_lib_string) @string\n"

        // 4. Functions
        "(call_expression function: (identifier) @function)\n"
        "(function_declarator declarator: (identifier) @function)\n"
        "(field_identifier) @type\n"
        "(call_expression function: (field_expression field: (field_identifier) @function))\n"

        // 5. Comments
        "(comment) @comment\n"

        // 6. Preprocessor (FIXED)
        "\"#include\" @keyword\n"
        "\"#define\" @keyword\n"
        "\"#ifdef\" @keyword\n"
        "\"#ifndef\" @keyword\n"
        "\"#endif\" @keyword\n"
        "(preproc_directive) @keyword\n";

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
        config->query_source = c_query_source;
        config->path_lsp = find_executable_in_path("clangd");
        config->lsp_args = clangd_args;
    } else if (strcmp(dot + 1, "go") == 0) {
        config->lang = GO;
        config->language_id = "go";
        config->query_source = go_highlights_query;
        config->path_lsp = find_executable_in_path("gopls");
        config->lsp_args = go_ls;
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

    free(config);
}
