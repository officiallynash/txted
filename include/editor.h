/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef EDITOR_H
#define EDITOR_H
#include <stddef.h>

/**
 * Struct untuk membawa data dari Prompt
 */
typedef struct {
    char label[128];
    char subtext[128];
    int icon_id;
} PromptItem;

/**
 * Struct untuk konfigurasi FloatPrompt
 */
typedef struct FloatPrompt {
    char label[64];
    char input_buf[256];
    int icon_id;

    PromptItem *items;
    size_t item_count;
    int selected_idx;
    int scroll_offset;
    bool is_active;
    bool edit_mode;
} FloatPrompt;

#endif