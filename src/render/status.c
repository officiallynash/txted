/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdio.h>

#include "buffer.h"
#include "git_client.h"
#include "lsp_ui.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

/**
 * Fungsi untuk Draw Status Bar
 */
void draw_status(BufManager *bufmgr, Font font) {
    EditorLayout layout = get_editor_layout(bufmgr);

    int win_w = layout.win_w;
    int win_h = layout.win_h;

    // Ambil ukuran font dinamis & hitung offset Y agar teks tepat di tengah (vertically
    // centered)
    float current_font_size = (float)font.baseSize;
    float text_y = (float)(win_h - STATUS_H) + ((float)STATUS_H - current_font_size) / 2.0f;

    // Gambar background status bar
    DrawRectangle(0, win_h - STATUS_H, win_w, STATUS_H, g_theme.bg_sidebar);

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) {
        Vector2 pos = {(float)PAD_X, text_y};
        DrawTextEx(font, "No buffer", pos, current_font_size, 1.0f, g_theme.text_normal);
        return;
    }

    const char *text = HAS_FLAG(buf->buf_flags, BUF_IS_DIRTY) ? "[*]" : "";

    // Format Teks Kiri
    char left[256] = {0};
    const char *mark = Git_file_mark(buf->path);
    if (git.is_repo) {
        if (mark[0]) {
            snprintf(left, sizeof(left), "File: %s %s[%s] | Branch: %s%s ",
                     buf->filename ? buf->filename : "Untitled", text, mark, git.branch,
                     git.has_changes ? "*" : "");
        } else {
            snprintf(left, sizeof(left), "File: %s %s | Branch: %s%s ",
                     buf->filename ? buf->filename : "Untitled", text, git.branch,
                     git.has_changes ? "*" : "");
        }
    } else {
        snprintf(left, sizeof(left), "File: %s %s ", buf->filename ? buf->filename : "Untitled",
                 text);
    }

    // Render Teks Kiri (Posisi Y Dinamis)
    Vector2 left_pos = {(float)PAD_X, text_y};
    DrawTextEx(font, left, left_pos, current_font_size, 1.0f, g_theme.text_normal);

    // Format Teks Kanan
    char right[128] = {0};
    const char *lsp_status = "Inactive";
    if (buf->language_id != nullptr) {
        lsp_status = HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE) ? "Active" : "Ready";
    }

    snprintf(right, sizeof(right), "LSP: %s (%s) | Ln: %zu | Col: %zu", lsp_status,
             buf->language_id ? buf->language_id : "none", buf->cursor.y + 1, buf->cursor.x + 1);

    // Render Teks Kanan (Ukur Lebar Pakai current_font_size & Posisi Y Dinamis)
    Vector2 rsize = MeasureTextEx(font, right, current_font_size, 1.0f);
    Vector2 right_pos = {(float)(win_w - (int)rsize.x - PAD_X), text_y};
    DrawTextEx(font, right, right_pos, current_font_size, 1.0f, g_theme.text_normal);
}
