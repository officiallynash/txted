/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef UI_H
#define UI_H

#include <raylib.h>
#include <stddef.h>

#include "types.h"

EditorLayout get_editor_layout(BufManager *bufmgr);  // Layout manager

void draw_status(BufManager *bufmgr, Font font);
void draw_editor(BufManager *bufmgr, Font font);
void draw_diagnostic_bar(BufManager *bufmgr, Font font);
void draw_dialog_modal(BufManager *bufmgr, Font font);
void handle_input(BufManager *bufmgr, Font font);
void handle_mouse_input(BufManager *bufmgr, Font font);
void Draw_confirm_exit(BufManager *bufmgr, Font font);
void draw_all_top_bar(BufManager *bufmgr, Font font);
void draw_prompt_ui(BufManager *bufmgr, Font font);

/*
 * Karena PromptBuffer ini juga untuk Git UI akan lebih bijak di taruh disini
 * Kalau di taruh di editor.h nanti kena cross include dengan BufManager
 */
PromptBuffer *PromptBuffer_init(void);
void PromptBuffer_insert(PromptBuffer *prb, size_t pos_idx, const char *ch);
void PromptBuffer_delete(PromptBuffer *prb, size_t pos_idx);
char *PromptBuffer_get(PromptBuffer *prb);
void PromptBuffer_destroy(BufManager *bufmgr);

#endif
