#include <raylib.h>
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
bool GitPopup_is_pushing(void);

/**
 * Fungsi untuk membuka GitPopup [PUBLIC API]
 */
void GitPopup_open(void) {
    git_popup.open = true;
    git_popup.edit_message = true;
    git_popup.selected = 0;
    git_popup.list_scroll = 0.0f;
    git_popup.last_error[0] = '\0';
    GitStatus_force();
}

/**
 * Fungsi untuk menutup GitPopup [PUBLIC API]
 */
void GitPopup_close(void) {
    git_popup.open = false;
    git_popup.edit_message = false;
}

/**
 * Fungsi untuk render GitPopup
 */
void GitPopup_render(BufManager *bufmgr, Font font) {
    if (!git_popup.open) return;
    EditorLayout Layout = get_editor_layout(bufmgr);

    const char *repo = bufmgr->path_root;
    int win_w = Layout.win_w;
    int win_h = Layout.win_h;

    // dim background
    DrawRectangle(0, 0, win_w, win_h, (Color){0, 0, 0, 120});

    float box_w = 480.0f;
    float box_h = 360.0f;
    Rectangle box = {(win_w - box_w) / 2.0f, (win_h - box_h) / 3.0f, box_w, box_h};

    DrawRectangleRounded(box, 0.06f, 4, g_theme.bg_card);
    DrawRectangleRoundedLines(box, 0.06f, 4, g_theme.border);

    // title
    char title[160];
    if (git.is_repo)
        snprintf(title, sizeof(title), "Git  ·  %s%s", git.branch, git.has_changes ? "*" : "");
    else
        snprintf(title, sizeof(title), "Git  ·  not a repo");
    DrawTextEx(font, title, (Vector2){box.x + 16, box.y + 12}, FONT_SIZE, 1.0f, g_theme.cursor);

    // list files
    float list_y = box.y + 44;
    float list_h = 180.0f;
    float item_h = 22.0f;
    float content_h = git.file_count * item_h;
    float max_scroll = content_h > list_h ? content_h - list_h : 0.0f;

    // clamp
    if (git_popup.list_scroll < 0) git_popup.list_scroll = 0;
    if (git_popup.list_scroll > max_scroll) git_popup.list_scroll = max_scroll;

    // List Area (Scrollable)
    Rectangle list_rect = {box.x + 8, list_y, box_w - 16, list_h};

    BeginScissorMode((int)list_rect.x, (int)list_rect.y, (int)list_rect.width,
                     (int)list_rect.height);

    float y = list_y - git_popup.list_scroll;
    if (git.file_count == 0) {
        DrawTextEx(font, "No changes", (Vector2){box.x + 16, y}, FONT_SIZE, 1.0f, g_theme.comment);
    } else {
        for (int i = 0; i < git.file_count; i++) {
            if (y + item_h < list_y || y > list_y + list_h) {
                y += item_h;
                continue;
            }
            char row[300];
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

            if (i == git_popup.selected)
                DrawRectangleRounded((Rectangle){box.x + 10, y - 2, box_w - 20, 22}, 0.1f, 4,
                                     g_theme.active_line);

            DrawTextEx(font, row, (Vector2){box.x + 16, y}, FONT_SIZE, 1.0f, c);
            y += item_h;
        }
    }
    EndScissorMode();

    // message input
    float msg_y = box.y + 240;
    DrawTextEx(font, "Message:", (Vector2){box.x + 16, msg_y}, FONT_SIZE, 1.0f, g_theme.comment);
    Rectangle input = {box.x + 16, msg_y + 22, box_w - 32, 32};
    DrawRectangleRounded(input, 0.1f, 4, g_theme.bg_editor);
    DrawRectangleRoundedLines(input, 0.1f, 4, g_theme.border);
    DrawTextEx(font, git_popup.message, (Vector2){input.x + 8, input.y + 6}, FONT_SIZE, 1.0f,
               g_theme.text_normal);

    // caret
    if (git_popup.edit_message && ((int)(GetTime() * 2) % 2) == 0) {
        float cx = input.x + 8 + MeasureTextEx(font, git_popup.message, FONT_SIZE, 1.0f).x;
        DrawRectangle((int)cx, (int)input.y + 6, 2, FONT_SIZE, g_theme.cursor);
    }

    // buttons
    float by = box.y + box_h - 48;
    Rectangle b_stage = {box.x + 16, by, 90, 28};
    Rectangle b_commit = {box.x + 116, by, 90, 28};
    Rectangle b_push = {box.x + 216, by, 90, 28};
    Rectangle b_close = {box.x + box_w - 100, by, 80, 28};

    // simple hit + label (atau pakai GuiButton)
    DrawRectangleRounded(b_stage, 0.1f, 4, g_theme.border);
    DrawRectangleRounded(b_commit, 0.1f, 4, g_theme.border);
    DrawRectangleRounded(b_push, 0.1f, 4, g_theme.border);
    DrawRectangleRounded(b_close, 0.1f, 4, g_theme.border);
    DrawTextEx(font, "Stage", (Vector2){b_stage.x + 20, b_stage.y + 5}, FONT_SIZE, 1.0f, WHITE);
    DrawTextEx(font, "Commit", (Vector2){b_commit.x + 14, b_commit.y + 5}, FONT_SIZE, 1.0f, WHITE);
    DrawTextEx(font, "Push", (Vector2){b_push.x + 26, b_push.y + 5}, FONT_SIZE, 1.0f, WHITE);
    DrawTextEx(font, "Close", (Vector2){b_close.x + 18, b_close.y + 5}, FONT_SIZE, 1.0f, WHITE);

    if (git_popup.last_error[0]) {
        Notif_show(git_popup.last_error, NOTIF_ERROR, 3.0f);
    }

    // --- input ---
    if (IsKeyPressed(KEY_ESCAPE)) {
        GitPopup_close();
        return;
    }

    // message typing
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

    // list nav
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
    // Scroll bar
    if (max_scroll > 0) {
        float track_h = list_h;
        float thumb_h = (list_h / content_h) * track_h;
        if (thumb_h < 12) thumb_h = 12;
        float thumb_y = list_y + (git_popup.list_scroll / max_scroll) * (track_h - thumb_h);
        DrawRectangleRounded((Rectangle){box.x + box_w - 14, thumb_y, 4, thumb_h}, 0.5f, 4,
                             g_theme.border);
    }

    // Handling Mouse
    Vector2 m = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(m, b_stage)) {
            GitPopup_stage(repo);
        } else if (CheckCollisionPointRec(m, b_commit)) {
            GitPopup_commit(repo, git_popup.message);
        } else if (CheckCollisionPointRec(m, b_push)) {
            if (!GitPopup_is_pushing()) {
                Notif_show("Push sedang di proses!", NOTIF_INFO, 3.0f);
                GitPopup_push_async(repo);
            }
        } else if (CheckCollisionPointRec(m, b_close) || !CheckCollisionPointRec(m, box)) {
            GitPopup_close();
        } else if (CheckCollisionPointRec(m, input)) {
            git_popup.edit_message = true;
        }
    }

    // Scroll Mouse
    if (CheckCollisionPointRec(m, list_rect)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            git_popup.list_scroll -= wheel * item_h * 2;
            if (git_popup.list_scroll < 0) git_popup.list_scroll = 0;
            if (git_popup.list_scroll > max_scroll) git_popup.list_scroll = max_scroll;
        }
    }
}