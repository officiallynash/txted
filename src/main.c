#include <stdbool.h>
#include <stdio.h>
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

    if (g_lsp_ui.enabled) {
        render_lsp_completion_ui(bufmgr, font);
        render_signature_help(bufmgr, font);
        render_hover_ui(bufmgr, font);
    }
    Notif_draw(font);
}

int main(int argc, char *argv[]) {
    // State agar auto keluar
#if defined(__linux__)
    if (fork() > 0) exit(0);
    setsid();
#endif

    // Inisasi Buffer Manager
    BufManager bufmgr = {0};
    BufManager_init(&bufmgr);

    // Inisiasi Notify
    Notif_init();

    // Set Log level
    SetTraceLogLevel(LOG_NONE);

    // Jika dibuka dengan txted filename
    if (argc == 2) {
        // Argumen sebagai Filename
        BufManager_newtab(&bufmgr, argv[1]);
    } else {
        BufManager_newtab(&bufmgr, NULL);                           /* tab awal */
        Notif_show("Buka File untuk fitur LSP", NOTIF_INFO, 3.0f);  // Kasih notif LSP
    }

    // Inisiasi Window
    int win_h = GetRenderHeight();
    int win_w = GetRenderWidth();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(win_w, win_h, "TxtEd - Simple Text Editor");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    char font_path[512];
    snprintf(font_path, sizeof(font_path), "%sSFMono-Regular.otf", GetApplicationDirectory());
    Font font = LoadFontEx(font_path, FONT_SIZE, NULL, 0);

    Theme_init(TOKYO_NIGHT);  // Init Theme
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    GuiSetFont(font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, FONT_SIZE);

    while (!ExitWindow && !WindowShouldClose()) {
        float dt = GetFrameTime();
        Notif_update(dt);
        GitStatus_update(&bufmgr, dt);

        // Handle Input biasa hanya jika TIDAK sedang minta exit
        if (!ExitWindowRequested) {
            lsp_ui_update(&bufmgr, dt);

            if (IsKeyPressed(KEY_SPACE) && IsKeyDown(KEY_LEFT_CONTROL)) {
                lsp_ui_toggle();
            }
            if (!git_popup.open) {
                handle_input(&bufmgr, font);
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
        render_all_ui(&bufmgr, font);

        // Render Modal Confirm Exit di LAYER PALING ATAS
        if (ExitWindowRequested) {
            int win_w = GetRenderWidth();
            int win_h = GetRenderHeight();

            // Backdrop Gelap Transparan
            DrawRectangle(0, 0, win_w, win_h, g_theme.backdrop);

            // Dimensi Modal Dialog (Center)
            float box_w = 380.0f;
            float box_h = 130.0f;
            Rectangle modal_rect = {(win_w - box_w) / 2.0f, (win_h - box_h) / 2.0f, box_w, box_h};

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

            Rectangle btn_yes = {modal_rect.x + modal_rect.width - (btn_w * 2) - 28, btn_y, btn_w,
                                 btn_h};
            Rectangle btn_no = {modal_rect.x + modal_rect.width - btn_w - 16, btn_y, btn_w, btn_h};

            // Tombol YES & NO Raygui
            if (GuiButton(btn_yes, GuiIconText(ICON_EXIT, "Keluar"))) {
                ExitWindow = true;
            }

            if (GuiButton(btn_no, GuiIconText(ICON_CROSS_SMALL, "Batal"))) {
                ExitWindowRequested = false;
            }
        }

        EndDrawing();
    }

    lsp_ui_shutdown();

    if (IsWindowReady()) {
        UnloadFont(font);  // Safe free font
    }

    BufManager_destroy(&bufmgr);  // Free semua buffer
    CloseWindow();                // Close window dan Context openGl
    return 0;
}
