#include <raylib.h>
#include <stdio.h>

#include "git_client.h"
#include "lsp_ui.h"
#include "theme.h"
#include "ui.h"

/**
 * Fungsi untuk Draw Status Bar
 */
void draw_status(BufManager *bufmgr, Font font) {
    int win_w = GetRenderWidth();
    int win_h = GetRenderHeight();

    // Gambar background status bar di paling bawah layar aktual
    DrawRectangle(0, win_h - STATUS_H, win_w, STATUS_H, g_theme.bg_sidebar);

    Buffer *buf = BufManager_getactive(bufmgr);
    if (!buf) {
        Vector2 pos = {(float)PAD_X, (float)(win_h - STATUS_H + 6)};
        DrawTextEx(font, "No buffer", pos, FONT_SIZE, 1.0f, g_theme.text_normal);
        return;
    }
    Color text_color = buf->is_dirty ? g_theme.cursor : g_theme.text_normal;

    char left[256];
    const char *mark = Git_file_mark(buf->path);  // Git Mark
    if (git.is_repo) {                            // Jika path adalah repo
        if (mark[0]) {                            // Jika ada perubahan di File
            snprintf(left, sizeof(left), "File: %s [%s] | Branch: %s%s",
                     buf->filename ? buf->filename : "Untitled", mark, git.branch,
                     git.has_changes ? "*" : "");
        } else {
            snprintf(left, sizeof(left), "File: %s | Branch: %s%s",
                     buf->filename ? buf->filename : "Untitled", git.branch,
                     git.has_changes ? "*" : "");
        }
    } else {  // Kalau bukan repo render biasa
        snprintf(left, sizeof(left), "File: %s", buf->filename ? buf->filename : "Untitled");
    }

    Vector2 left_pos = {(float)PAD_X, (float)(win_h - STATUS_H + 6)};
    DrawTextEx(font, left, left_pos, FONT_SIZE, 1.0f, text_color);

    char right[128];

    const char *lsp_status = "Inactive";
    if (buf->language_id != NULL) {
        // Jika UI popup lagi aktif -> "Active", kalau standby -> "Ready" / "Idle"
        lsp_status = g_lsp_ui.enabled ? "Active" : "Ready";
    }

    snprintf(right, sizeof(right), "LSP: %s (%s) | Ln %zu | Col %zu", lsp_status,
             buf->language_id ? buf->language_id : "none", buf->cursor.y + 1, buf->cursor.x + 1);

    Vector2 rsize = MeasureTextEx(font, right, FONT_SIZE, 1.0f);
    Vector2 right_pos = {(float)(win_w - (int)rsize.x - PAD_X), (float)(win_h - STATUS_H + 6)};
    DrawTextEx(font, right, right_pos, FONT_SIZE, 1.0f, g_theme.text_normal);
}
