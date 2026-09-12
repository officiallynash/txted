/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#define RAY_IMPLEMENTATION
#define RAY_STATIC

#include <raygui.h>
#include <stdbool.h>
#include <unistd.h>

#include "buffer_manager.h"
#include "fs.h"
#include "lsp_ui.h"
#include "notification.h"
#include "raylib.h"
#include "result.h"
#include "settings_txted.h"
#include "theme.h"
#include "ui.h"

/* ---------------------- *
 * Render semua UI
 * ---------------------- */
void render_all_ui(BufManager *bufmgr, Font font) {
    draw_file_manager(bufmgr, font);
    draw_editor(bufmgr, font);
    draw_tabs(bufmgr, font);
    draw_diagnostic_bar(bufmgr, font);
    draw_status(bufmgr, font);
    draw_dialog_modal(bufmgr, font);

    // Jika LSP aktif, Kita tampilkan lsp
    if (HAS_FLAG(g_lsp_ui.lsp_flag, LSP_ENABLE)) {
        render_lsp_completion_ui(bufmgr, font);
        render_signature_help(bufmgr, font);
        render_hover_ui(bufmgr, font);
    }
    Notif_draw(bufmgr, font);
}

// Pintu masuk aplikasi
int main(int argc, char *argv[]) {
    // State agar auto keluar ketika launch dari terminal
#if defined(__linux__)
    if (fork() > 0) exit(0);
    setsid();
#endif

    // Inisasi Buffer Manager dan Load setting dari File settings.ini
    Settings_load();
    BufManager *bufmgr = BufManager_init();

    // Inisiasi Notify
    Notif_init();

    // Set Log level
    SetTraceLogLevel(LOG_NONE);

    // Jika dibuka dengan txted filename
    if (argc == 2) {
        // Argumen sebagai Filename
        BufManager_newtab(bufmgr, argv[1]);
    } else {
        BufManager_newtab(bufmgr, nullptr); /* tab awal */
    }

    Font font;  // Inisiasi Font, karena font di Apply di Settings
    // Apply settings
    Settings_apply(bufmgr, &font);  // Passing font ke Apply pakai &

    // Loop utama Aplikasi
    while (!HAS_FLAG(bufmgr->win_flags, TXTED_EXIT)) {
        float dt = GetFrameTime();
        Notif_update(dt);

        // Jika dipencet si X
        // Aku assume bahwa Semua buffer telah di save
        // Jadi user secara sadar sudah simpan dan pencet ini tombol
        if (WindowShouldClose()) SET_FLAG(bufmgr->win_flags, TXTED_EXIT);

        // Handle Input biasa hanya jika TIDAK sedang minta exit
        if (!HAS_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT)) {
            lsp_ui_update(bufmgr, dt);

            if (IsKeyPressed(KEY_SPACE) && IsKeyDown(KEY_LEFT_CONTROL)) {
                lsp_ui_toggle();
            }

            handle_input(bufmgr, font);
        } else {
            lsp_ui_hide();
            // Hotkey shortcut keyboard saat modal exit aktif
            if (IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_ENTER))
                SET_FLAG(bufmgr->win_flags, TXTED_EXIT);
            if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE))
                CLR_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT);
        }

        // Gambar UI
        BeginDrawing();
        ClearBackground(g_theme.bg_main);

        // Render Editor UI dulu di layer paling bawah!
        render_all_ui(bufmgr, font);

        // Render Modal Confirm Exit di LAYER PALING ATAS
        if (HAS_FLAG(bufmgr->win_flags, TXTED_REQ_EXIT)) {
            Draw_confirm_exit(bufmgr, font);
        }
        EndDrawing();
    }

    lsp_ui_shutdown();
    BufManager_destroy(bufmgr);  // safety free

    if (IsWindowReady()) {
        UnloadFont(font);  // Safe free font
    }

    CloseWindow();  // Close window dan Context openGl
    return 0;
}
