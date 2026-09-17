/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef TXTED_LSP_UI_H
#define TXTED_LSP_UI_H
#include <stddef.h>
#include <stdint.h>

#include "buffer_manager.h"
#include "lsp_config.h"
#include "lsp_server.h"
#include "raylib.h"

// Deskripsi Bitwise operation untuk pengganti
// Flag yang memakai bool dari stdbool
// Walaupun agak "ribet" tapi ini untuk menghemat memori
constexpr uint8_t LSP_ENABLE = 1 << 0;
constexpr uint8_t LSP_VISIBLE = 1 << 1;
constexpr uint8_t LSP_REQUEST_PENDING = 1 << 2;
constexpr uint8_t LSP_HAS_COMP = 1 << 3;
constexpr uint8_t LSP_HAS_SIG = 1 << 4;
constexpr uint8_t LSP_SIG_PENDING = 1 << 5;
constexpr uint8_t LSP_HAS_HOVE = 1 << 6;
constexpr uint8_t LSP_HOV_PENDING = 1 << 7;

/**
 * Struct penampung hasil dari Completion yang sudah di filter
 */
typedef struct {
    CompletionItem *item;
    int score;
} FilteredItem;

/**
 * Enum penanda Completion dan Signature
 * digunakan untuk auto deteksi ketika rendering
 * agar tidak tunmpang tindih
 */
typedef enum { POPUP_BELOW, POPUP_ABOVE } PopupSide;

/**
 * Struct utama untuk mengatur LSP state
 */
typedef struct {
    char *root_uri;
    char uri[512];
    char language_id[32];
    char current_text[8192];

    int selected_index;
    int last_line;
    int last_character;
    uint8_t lsp_flag;

    // Completion
    CompletionList completion;
    PopupSide completion_side;
    PopupSide signature_side;

    // Signature
    SignatureHelp signature_help;
    size_t sig_y;

    // Hover
    HoverInfo hover;
    float hover_scroll;

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
