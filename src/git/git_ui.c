/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "buffer_manager.h"
#include "git_client.h"
#include "notification.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

// Extern dari git_client.c
extern bool GitPopup_stage(const char *repo);
extern bool GitPopup_commit(const char *repo, const char *message);
extern void GitPopup_push_async(const char *repo);
extern bool GitPopup_is_pushing(void);

static bool git_popup_just_opened = false;

/**
 * Fungsi untuk membuka GitUi
 */
void GitPopup_open(BufManager *bufmgr) {
    // Set ke flags Show Git
    SET_FLAG(bufmgr->win_flags, TXTED_SHOW_GIT);

    // Focus mode ke Popup
    bufmgr->mode = POPUP;

    git_popup.edit_message = true;
    git_popup.selected = 0;
    git_popup.list_scroll = 0.0f;
    git_popup.last_error[0] = '\0';
    git_popup_just_opened = true;
    GitStatus_force();
}

/**
 * FUngsi untuk GitPopup Close
 */
void GitPopup_close(BufManager *bufmgr) {
    git_popup.edit_message = false;

    // Hapus flag show git
    CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_GIT);
}

/**
 * Helper internal untuk merender Button dengan efek Hover
 */
static bool DrawButton(Font font, const char *text, Rectangle rect, Color base_col, Color text_col,
                       bool disabled) {
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect) && !disabled;
    float current_font_size = (float)font.baseSize;

    // Warna background saat normal vs hover
    Color bg = disabled ? g_theme.bg_editor : (hovered ? g_theme.active_line : base_col);
    Color txt_c = disabled ? g_theme.comment : text_col;

    DrawRectangleRounded(rect, 0.15f, 4, bg);
    DrawRectangleRoundedLines(rect, 0.15f, 4, g_theme.border);

    Vector2 size = MeasureTextEx(font, text, current_font_size, 1.0f);
    Vector2 pos = {rect.x + (rect.width - size.x) / 2.0f, rect.y + (rect.height - size.y) / 2.0f};
    DrawTextEx(font, text, pos, current_font_size, 1.0f, txt_c);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/**
 * Fungsi utama untuk Render GitUi
 */
void draw_gitpopup(BufManager *bufmgr, Font font) {
    if (!HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_GIT)) return;
    EditorLayout Layout = get_editor_layout(bufmgr);

    float current_font_size = (float)font.baseSize;
    const char *repo = bufmgr->path_root;
    int win_w = Layout.win_w;
    int win_h = Layout.win_h;

    // Dim background (overlay)
    DrawRectangle(0, 0, win_w, win_h, (Color){0, 0, 0, 140});

    // Box
    float box_w = 500.0f;
    float box_h = 420.0f;
    Rectangle box = {(win_w - box_w) / 2.0f, (win_h - box_h) / 3.0f, box_w, box_h};

    DrawRectangleRounded(box, 0.04f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.04f, 4, g_theme.border);

    // Header Title
    char title[160] = {0};
    if (git.is_repo) {
        snprintf(title, sizeof(title), "Git  ·  %s%s", git.branch, git.has_changes ? "*" : "");
    } else {
        snprintf(title, sizeof(title), "Git  ·  not a repo");
    }

    DrawTextEx(font, title, (Vector2){box.x + 16, box.y + 14}, current_font_size, 1.0f,
               g_theme.cursor);

    // List Files (Scrollable Area)
    float list_y = box.y + 44;
    float list_h = 170.0f;
    float item_h = current_font_size;
    float content_h = git.file_count * item_h;
    float max_scroll = content_h > list_h ? content_h - list_h : 0.0f;

    if (git_popup.list_scroll < 0) git_popup.list_scroll = 0;
    if (git_popup.list_scroll > max_scroll) git_popup.list_scroll = max_scroll;

    Rectangle list_rect = {box.x + 8, list_y, box_w - 16, list_h};

    BeginScissorMode((int)list_rect.x, (int)list_rect.y, (int)list_rect.width,
                     (int)list_rect.height);

    float y = list_y - git_popup.list_scroll;
    if (git.file_count == 0) {
        DrawTextEx(font, "No changes detected", (Vector2){box.x + 16, y + 8}, current_font_size,
                   1.0f, g_theme.comment);
    } else {
        for (int i = 0; i < git.file_count; i++) {
            if (y + item_h >= list_y && y <= list_y + list_h) {
                char row[300] = {0};
                snprintf(row, sizeof(row), "[%s]  %s", git.files[i].mark, git.files[i].path);

                Color c = g_theme.text_normal;
                if (git.files[i].mark[0] == '~')
                    c = g_theme.warning;
                else if (git.files[i].mark[0] == '?')
                    c = g_theme.comment;
                else if (git.files[i].mark[0] == '+')
                    c = g_theme.function;
                else if (git.files[i].mark[0] == '-')
                    c = g_theme.keyword;

                if (i == git_popup.selected) {
                    DrawRectangleRounded((Rectangle){box.x + 10, y, box_w - 20, item_h}, 0.1f, 4,
                                         g_theme.active_line);
                }

                DrawTextEx(font, row, (Vector2){box.x + 16, y + 2}, current_font_size, 1.0f, c);
            }
            y += item_h;
        }
    }
    EndScissorMode();

    // Scrollbar Indicator
    if (max_scroll > 0) {
        float track_h = list_h;
        float thumb_h = (list_h / content_h) * track_h;
        if (thumb_h < 12) thumb_h = 12;
        float thumb_y = list_y + (git_popup.list_scroll / max_scroll) * (track_h - thumb_h);
        DrawRectangleRounded((Rectangle){box.x + box_w - 12, thumb_y, 4, thumb_h}, 0.5f, 4,
                             g_theme.border);
    }

    // =========================================================================
    // Multi line input untuk Commit
    // =========================================================================
    float msg_y = box.y + 226;
    DrawTextEx(font, "Commit Message:", (Vector2){box.x + 16, msg_y}, current_font_size, 1.0f,
               g_theme.comment);

    float line_step = current_font_size + 4.0f;
    float input_h = (line_step * 3.0f) + 12.0f;  // Tinggi cukup untuk 3 baris + padding
    Rectangle input = {box.x + 16, msg_y + 22, box_w - 32, input_h};

    DrawRectangleRounded(input, 0.06f, 4, g_theme.bg_editor);
    DrawRectangleRoundedLines(input, 0.06f, 4,
                              git_popup.edit_message ? g_theme.cursor : g_theme.border);

    BeginScissorMode((int)input.x + 2, (int)input.y + 2, (int)input.width - 4,
                     (int)input.height - 4);

    float draw_x = input.x + 8.0f;
    float draw_y = input.y + 6.0f;
    float usable_w = input.width - 16.0f;

    // Buffer sementara untuk menghitung kata per baris
    char current_wrap[512] = {0};
    char *temp_msg = strdup(git_popup.message);
    char *word = strtok(temp_msg, " ");

    float cursor_draw_x = draw_x;
    float cursor_draw_y = draw_y;

    if (git_popup.message[0] == '\0') {
        // Jika teks masih kosong, kursor digambar di awal baris pertama
        cursor_draw_x = draw_x;
        cursor_draw_y = draw_y;
    } else {
        while (word) {
            char test_buf[512] = {0};
            if (strlen(current_wrap) > 0) {
                snprintf(test_buf, sizeof(test_buf), "%s %s", current_wrap, word);
            } else {
                snprintf(test_buf, sizeof(test_buf), "%s", word);
            }

            // Jika kata melebihi lebar input box, turunkan ke baris baru
            if (MeasureTextEx(font, test_buf, current_font_size, 1.0f).x > usable_w) {
                if (strlen(current_wrap) > 0) {
                    DrawTextEx(font, current_wrap, (Vector2){draw_x, draw_y}, current_font_size,
                               1.0f, g_theme.text_normal);
                    draw_y += line_step;
                    snprintf(current_wrap, sizeof(current_wrap), "%s", word);
                } else {
                    DrawTextEx(font, word, (Vector2){draw_x, draw_y}, current_font_size, 1.0f,
                               g_theme.text_normal);
                    draw_y += line_step;
                    current_wrap[0] = '\0';
                }
            } else {
                snprintf(current_wrap, sizeof(current_wrap), "%s", test_buf);
            }
            word = strtok(nullptr, " ");
        }

        // Render sisa potongan teks di baris terakhir
        if (strlen(current_wrap) > 0) {
            DrawTextEx(font, current_wrap, (Vector2){draw_x, draw_y}, current_font_size, 1.0f,
                       g_theme.text_normal);
            float wrap_w = MeasureTextEx(font, current_wrap, current_font_size, 1.0f).x;

            // Hitung posisi kursor aktif di ekor teks
            cursor_draw_x = draw_x + wrap_w;
            if (git_popup.message[strlen(git_popup.message) - 1] == ' ') {
                cursor_draw_x += MeasureTextEx(font, " ", current_font_size, 1.0f).x;
            }
            cursor_draw_y = draw_y;
        }
    }

    free(temp_msg);

    // Gambarkan Kursor Berkedip (Blinking Cursor)
    if (git_popup.edit_message && ((int)(GetTime() * 2) % 2) == 0) {
        DrawRectangle((int)cursor_draw_x, (int)cursor_draw_y + 1, 2, (int)current_font_size - 1,
                      g_theme.cursor);
    }

    EndScissorMode();

    // Buttons Layout
    float by = box.y + box_h - 44;
    Rectangle b_stage = {box.x + 16, by, 85, 28};
    Rectangle b_commit = {box.x + 109, by, 85, 28};
    Rectangle b_push = {box.x + 202, by, 95, 28};
    Rectangle b_close = {box.x + box_w - 96, by, 80, 28};

    bool is_pushing = GitPopup_is_pushing();
    const char *push_label = is_pushing ? "Pushing" : "Push";

    // Handling Button Clicks via Helper
    bool click_stage =
        DrawButton(font, "Stage", b_stage, g_theme.border, g_theme.text_normal, false);
    bool click_commit =
        DrawButton(font, "Commit", b_commit, g_theme.border, g_theme.text_normal, false);
    bool click_push =
        DrawButton(font, push_label, b_push, g_theme.border, g_theme.text_normal, is_pushing);
    bool click_close =
        DrawButton(font, "Close", b_close, g_theme.border, g_theme.text_normal, false);

    Vector2 m = GetMousePosition();

    // Mouse Input Events
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (git_popup_just_opened) {
            git_popup_just_opened = false;
        } else {
            if (click_stage) {
                if (GitPopup_stage(repo)) {
                    Notif_show("Berhasil stage semua file!", NOTIF_SUCCESS, 2.0f);
                } else {
                    Notif_show(git_popup.last_error[0] ? git_popup.last_error : "Stage gagal!",
                               NOTIF_ERROR, 3.0f);
                }
            } else if (click_commit) {
                if (GitPopup_commit(repo, git_popup.message)) {
                    Notif_show("Commit berhasil disimpan!", NOTIF_SUCCESS, 2.0f);
                } else {
                    Notif_show(git_popup.last_error[0] ? git_popup.last_error : "Commit gagal!",
                               NOTIF_ERROR, 3.0f);
                }
            } else if (click_push) {
                Notif_show("Push sedang diproses...", NOTIF_INFO, 3.0f);
                GitPopup_push_async(repo);
            } else if (click_close || !CheckCollisionPointRec(m, box)) {
                bufmgr->mode = WRITE;
                GitPopup_close(bufmgr);
            } else if (CheckCollisionPointRec(m, input)) {
                git_popup.edit_message = true;
            }
        }
    }

    // Keyboard Shortcuts & Typing
    if (IsKeyPressed(KEY_ESCAPE)) {
        GitPopup_close(bufmgr);
        bufmgr->mode = WRITE;
        return;
    }

    if (git_popup.edit_message) {
        int k = GetCharPressed();
        while (k > 0) {
            size_t len = strlen(git_popup.message);
            if (k >= 32 && k < 127 && len + 1 < sizeof(git_popup.message)) {
                git_popup.message[len] = (char)k;
                git_popup.message[len + 1] = '\0';
            }
            k = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t len = strlen(git_popup.message);
            if (len > 0) git_popup.message[len - 1] = '\0';
        }
    }

    // List Navigation via Keyboard
    if (IsKeyPressed(KEY_DOWN) && git_popup.selected + 1 < git.file_count) {
        git_popup.selected++;
        float sel_y = git_popup.selected * item_h;
        if (sel_y + item_h > git_popup.list_scroll + list_h)
            git_popup.list_scroll = sel_y + item_h - list_h;
    }
    if (IsKeyPressed(KEY_UP) && git_popup.selected > 0) {
        git_popup.selected--;
        float sel_y = git_popup.selected * item_h;
        if (sel_y < git_popup.list_scroll) git_popup.list_scroll = sel_y;
    }

    // Mouse Wheel Scroll
    if (CheckCollisionPointRec(m, list_rect)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            git_popup.list_scroll -= wheel * item_h * 2;
            if (git_popup.list_scroll < 0) git_popup.list_scroll = 0;
            if (git_popup.list_scroll > max_scroll) git_popup.list_scroll = max_scroll;
        }
    }
}
