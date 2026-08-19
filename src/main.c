/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <unistd.h>

#define RAY_IMPLEMENTATION
#define RAY_STATIC

#include <raygui.h>

#include "buffer_manager.h"
#include "fs.h"
#include "git_client.h"
#include "lsp_ui.h"
#include "notification.h"
#include "raylib.h"
#include "settings_txted.h"
#include "theme.h"
#include "ui.h"

// State untuk exit
bool ExitWindowRequested = false;
bool ExitWindow = false;

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
    GitPopup_render(bufmgr, font);

    // Jika LSP aktif, Kita tampilkan lsp
    if (g_lsp_ui.enabled) {
        render_lsp_completion_ui(bufmgr, font);
        render_signature_help(bufmgr, font);
        render_hover_ui(bufmgr, font);
    }
    Notif_draw(font);
}

int main(int argc, char *argv[]) {
    // State agar auto keluar
    // #if defined(__linux__)
    //     if (fork() > 0) exit(0);
    //     setsid();
    // #endif

    // Inisasi Buffer Manager
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
        BufManager_newtab(bufmgr, NULL); /* tab awal */
    }

    Font font;  // Inisiasi Font, karena font di Apply di Settings
    // Apply settings
    Settings_apply(bufmgr, &font);  // Passing font ke Apply pakai &

    // Loop utama Aplikasi
    while (!ExitWindow && !WindowShouldClose()) {
        float dt = GetFrameTime();
        Notif_update(dt);
        GitStatus_update(bufmgr, dt);

        // Handle Input biasa hanya jika TIDAK sedang minta exit
        if (!ExitWindowRequested) {
            lsp_ui_update(bufmgr, dt);

            if (IsKeyPressed(KEY_SPACE) && IsKeyDown(KEY_LEFT_CONTROL)) {
                lsp_ui_toggle();
            }
            if (!git_popup.open) {
                handle_input(bufmgr, font);
            }
        } else {
            lsp_ui_hide();
            // Hotkey shortcut keyboard saat modal exit aktif
            if (IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_ENTER)) ExitWindow = true;
            if (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE)) ExitWindowRequested = false;
        }

        BeginDrawing();
        ClearBackground(g_theme.bg_main);

        // Render Editor UI dulu di layer paling bawah!
        render_all_ui(bufmgr, font);

        // Render Modal Confirm Exit di LAYER PALING ATAS
        if (ExitWindowRequested) {
            Draw_confirm_exit(bufmgr, font);
        }

        EndDrawing();
    }

    lsp_ui_shutdown();

    if (IsWindowReady()) {
        UnloadFont(font);  // Safe free font
    }

    BufManager_destroy(bufmgr);  // Free semua buffer
    CloseWindow();               // Close window dan Context openGl
    return 0;
}
