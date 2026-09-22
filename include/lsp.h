/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef LSP_CONFIG_H
#define LSP_CONFIG_H
#include "types.h"

extern LspUiState g_lsp_ui;

// Debounce timer for LSP requests
extern float lsp_debounce_timer;

LangConfig *LspConfig_detail(const char *filepath);
void LangConfig_free(LangConfig *config);

// Mulai clangd + handshake
bool lsp_start(const char *lsp_path, char **argv, const char *workspace_root);
void lsp_did_open(const char *uri, const char *language_id, const char *text);
void lsp_did_change(const char *uri, const char *text, int version);
void lsp_did_close(const char *uri);
void lsp_stop(void);

// Completion
CompletionList lsp_completion(const char *uri, int line, int character, char trigger_char);
void lsp_free_completion(CompletionList *list);

// Diagnostic
DiagnosticList *lsp_get_diagnostics(const char *uri);
void lsp_free_diagnostics(DiagnosticList *list);

// Signature Help
SignatureHelp lsp_signature_help(const char *uri, int line, int character);
void lsp_free_signature_help(SignatureHelp *help);

// Auto format
TextEditList lsp_format(const char *uri, int tab_size, bool insert_spaces);
void lsp_free_text_edits(TextEditList *list);

// Hover
HoverInfo lsp_hover(const char *uri, int line, int character);
void lsp_free_hover(HoverInfo *hover);

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
