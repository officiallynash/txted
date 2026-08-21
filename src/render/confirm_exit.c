/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "buffer_manager.h"
#include "raygui.h"
#include "raylib.h"
#include "theme.h"
#include "ui.h"

/**
 * Fungsi untuk draw atau render Confirm Exit
 */
void Draw_confirm_exit(BufManager *bufmgr, Font font) {
    EditorLayout layout = get_editor_layout(bufmgr);

    // Backdrop Gelap Transparan
    DrawRectangle(0, 0, layout.win_w, layout.win_h, g_theme.backdrop);

    // Dimensi Modal Dialog (Center)
    float box_w = 380.0f;
    float box_h = 130.0f;
    Rectangle modal_rect = {(layout.win_w - box_w) / 2.0f, (layout.win_h - box_h) / 2.0f, box_w,
                            box_h};

    // Card Modal Background
    DrawRectangleRounded(modal_rect, 0.12f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(modal_rect, 0.12f, 4, g_theme.border);

    // Text Pesan Konfirmasi
    const char *msg = "Yakin ingin keluar dari TxtEd?";
    DrawTextEx(font, msg, (Vector2){modal_rect.x + 20, modal_rect.y + 18}, FONT_SIZE, 1.0f,
               g_theme.text_normal);

    // Dimensi Tombol
    float btn_w = 120.0f;
    float btn_h = 32.0f;
    float btn_y = modal_rect.y + modal_rect.height - btn_h - 14.0f;

    Rectangle btn_yes = {modal_rect.x + modal_rect.width - (btn_w * 2) - 28, btn_y, btn_w, btn_h};
    Rectangle btn_no = {modal_rect.x + modal_rect.width - btn_w - 16, btn_y, btn_w, btn_h};

    // Tombol YES & NO Raygui
    if (GuiButton(btn_yes, GuiIconText(ICON_EXIT, "Keluar"))) {
        bufmgr->win_flags |= TXTED_EXIT;
    }

    if (GuiButton(btn_no, GuiIconText(ICON_CROSS_SMALL, "Batal"))) {
        bufmgr->win_flags &= ~TXTED_REQ;
    }
}
