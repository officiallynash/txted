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
#include "theme.h"
#include "ui.h"

// Extern dari git_client.c
extern bool GitPopup_stage(const char *repo);
extern bool GitPopup_commit(const char *repo, const char *message);
extern bool GitPopup_push_async(const char *repo);
extern bool GitPopup_is_pushing(void);

static bool git_popup_just_opened = false;

void GitPopup_open(void) {
    git_popup.open = true;
    git_popup.edit_message = true;
    git_popup.selected = 0;
    git_popup.list_scroll = 0.0f;
    git_popup.last_error[0] = '\0';
    git_popup_just_opened = true;
    GitStatus_force();
}

void GitPopup_close(void) {
    git_popup.open = false;
    git_popup.edit_message = false;
}

/**
 * Helper internal untuk merender Button dengan efek Hover
 */
static bool DrawButton(Font font, const char *text, Rectangle rect, Color base_col, Color text_col,
                       bool disabled) {
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect) && !disabled;

    // Warna background saat normal vs hover
    Color bg = disabled ? g_theme.bg_editor : (hovered ? g_theme.active_line : base_col);
    Color txt_c = disabled ? g_theme.comment : text_col;

    DrawRectangleRounded(rect, 0.15f, 4, bg);
    DrawRectangleRoundedLines(rect, 0.15f, 4, g_theme.border);

    Vector2 size = MeasureTextEx(font, text, (float)font.baseSize, 1.0f);
    Vector2 pos = {rect.x + (rect.width - size.x) / 2.0f, rect.y + (rect.height - size.y) / 2.0f};
    DrawTextEx(font, text, pos, (float)font.baseSize, 1.0f, txt_c);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void GitPopup_render(BufManager *bufmgr, Font font) {
    if (!git_popup.open) return;
    EditorLayout Layout = get_editor_layout(bufmgr);

    float current_font_size = (float)font.baseSize;
    const char *repo = bufmgr->path_root;
    int win_w = Layout.win_w;
    int win_h = Layout.win_h;

    // Dim background (overlay)
    DrawRectangle(0, 0, win_w, win_h, (Color){0, 0, 0, 140});

    float box_w = 480.0f;
    float box_h = 360.0f;
    Rectangle box = {(win_w - box_w) / 2.0f, (win_h - box_h) / 3.0f, box_w, box_h};

    DrawRectangleRounded(box, 0.04f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.04f, 4, g_theme.border);

    // Header Title
    char title[160] = {0};
    if (git.is_repo)
        snprintf(title, sizeof(title), "Git  ·  %s%s", git.branch, git.has_changes ? "*" : "");
    else
        snprintf(title, sizeof(title), "Git  ·  not a repo");

    DrawTextEx(font, title, (Vector2){box.x + 16, box.y + 14}, current_font_size, 1.0f,
               g_theme.cursor);

    // List Files (Scrollable Area)
    float list_y = box.y + 44;
    float list_h = 170.0f;
    float item_h = 22.0f;
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

    // Message Input Box
    float msg_y = box.y + 226;
    DrawTextEx(font, "Commit Message:", (Vector2){box.x + 16, msg_y}, current_font_size, 1.0f,
               g_theme.comment);

    Rectangle input = {box.x + 16, msg_y + 22, box_w - 32, 32};
    DrawRectangleRounded(input, 0.1f, 4, g_theme.bg_editor);
    DrawRectangleRoundedLines(input, 0.1f, 4,
                              git_popup.edit_message ? g_theme.cursor : g_theme.border);

    float text_w = MeasureTextEx(font, git_popup.message, current_font_size, 1.0f).x;
    float max_w = input.width - 16.0f;
    float offset_x = (text_w > max_w) ? (text_w - max_w) : 0.0f;

    BeginScissorMode((int)input.x + 4, (int)input.y, (int)input.width - 8, (int)input.height);
    Vector2 text_pos = {input.x + 8.0f - offset_x, input.y + 7.0f};
    DrawTextEx(font, git_popup.message, text_pos, current_font_size, 1.0f, g_theme.text_normal);

    if (git_popup.edit_message && ((int)(GetTime() * 2) % 2) == 0) {
        float cx = text_pos.x + text_w;
        DrawRectangle((int)cx, (int)input.y + 7, 2, (int)current_font_size - 2, g_theme.cursor);
    }
    EndScissorMode();

    // Buttons Layout
    float by = box.y + box_h - 44;
    Rectangle b_stage = {box.x + 16, by, 85, 28};
    Rectangle b_commit = {box.x + 109, by, 85, 28};
    Rectangle b_push = {box.x + 202, by, 95, 28};
    Rectangle b_close = {box.x + box_w - 96, by, 80, 28};

    bool is_pushing = GitPopup_is_pushing();
    const char *push_label = is_pushing ? "Pushing..." : "Push";

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
                GitPopup_close();
            } else if (CheckCollisionPointRec(m, input)) {
                git_popup.edit_message = true;
            }
        }
    }

    // Keyboard Shortcuts & Typing
    if (IsKeyPressed(KEY_ESCAPE)) {
        GitPopup_close();
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
