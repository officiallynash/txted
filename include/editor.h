/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef EDITOR_H
#define EDITOR_H
#include <stddef.h>

#include "rope.h"

/**
 * Struct untuk menampung Prompt Buffer
 */
typedef struct {
    String *str;
    size_t len;  // Kasih manual di struct agar lebih efisien
    size_t cursor_pos;
} PromptBuffer;

/**
 * Struct untuk membawa data dari Prompt
 */
typedef struct {
    char label[128];
    char subtext[128];
    int icon_id;
} PromptItem;

/**
 * Enum untuk PromptType, aku rasa lebih efisien memakai Enum daripada Bitwise Flag.
 */
typedef enum {
    PROMPT_TYPE_SAVE,
    PROMPT_SAVE_AS,
    PROMPT_TYPE_OPEN_FILE,
    PROMPT_TYPE_NEW_FILE,
    PROMPT_TYPE_NEW_FOLDER,
    PROMPT_TYPE_SEARCH
} PromptType;

/**
 * Struct untuk konfigurasi FloatPrompt
 */
typedef struct FloatPrompt {
    char label[64];
    int icon_id;

    int selected_idx;
    int scroll_offset;
    bool is_active;
    bool edit_mode;
    PromptType type;
    PromptBuffer *prb;
} FloatPrompt;

#endif
