/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>

#include "buffer_manager.h"
#include "ui.h"

/**
 * Helper untuk setting layout
 */
EditorLayout get_editor_layout(BufManager *bufmgr) {
    EditorLayout L = {0};

    int win_w = GetRenderWidth();
    int win_h = GetRenderHeight();

    L.win_h = win_h;  // Height
    L.win_w = win_w;  // Width

    L.editor_y = TAB_H;
    L.editor_h = win_h - TAB_H - STATUS_H - DIAG_PANEL_H;

    if (bufmgr->show_fm) {
        L.fm_w = (int)(win_w * bufmgr->fm_width_ratio);
        if (L.fm_w < 160) L.fm_w = 160;
        if (L.fm_w > win_w / 2) L.fm_w = win_w / 2;

        L.fm_x = 0;
        L.fm_y = TAB_H;
        L.fm_h = win_h - TAB_H - STATUS_H;

        L.editor_x = L.fm_w;
        L.editor_w = win_w - L.fm_w;
    } else {
        L.fm_w = 0;
        L.fm_x = 0;
        L.fm_y = TAB_H;
        L.fm_h = 0;

        L.editor_x = 0;
        L.editor_w = win_w;
    }

    L.gutter_screen_x = L.editor_x;
    L.text_screen_x = L.editor_x + PAD_X + GUTTER_W;

    return L;
}
