/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stddef.h>
#ifndef TXTED_LSP_UI_H
#define TXTED_LSP_UI_H

#include "buffer_manager.h"
#include "lsp_config.h"
#include "lsp_server.h"
#include "raylib.h"

typedef struct {
    CompletionItem *item;
    int score;
} FilteredItem;

typedef enum { POPUP_BELOW, POPUP_ABOVE } PopupSide;

typedef struct {
    bool enabled;
    bool visible;
    bool request_pending;
    char *root_uri;

    // Completion
    CompletionList completion;
    bool has_completion;
    PopupSide completion_side;
    PopupSide signature_side;

    // Signature
    SignatureHelp signature_help;
    bool has_signature;
    bool signature_pending;
    size_t sig_y;

    // Hover
    HoverInfo hover;
    bool has_hover;
    bool hover_pending;
    float hover_scroll;

    char uri[512];
    char language_id[32];
    char current_text[8192];

    int selected_index;
    int last_line;
    int last_character;
} LspUiState;

extern LspUiState g_lsp_ui;

// Debounce timer for LSP requests
extern float lsp_debounce_timer;
#define LSP_DEBOUNCE_DELAY 0.20f

// LSP configuration

void lsp_ui_set_document(const char *uri, const char *language_id, const char *text);
void lsp_ui_shutdown(void);
void lsp_ui_toggle(void);
void lsp_ui_hide(void);
void lsp_ui_update(BufManager *bufmgr, float dt);
void Ensure_lsp_init(LangConfig *lang, const char *filepath);
void render_lsp_completion_ui(BufManager *bufmgr, Font font);
void render_signature_help(BufManager *bufmgr, Font font);
void render_hover_ui(BufManager *bufmgr, Font font);
CompletionItem *lsp_get_selected_item(const char *current_word);

#endif
