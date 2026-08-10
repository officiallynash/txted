#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer_manager.h"
#include "git_client.h"
#include "raygui.h"
#include "theme.h"
#include "ui.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static float dialog_scroll_y = 0.0f;  // Menyimpan offset scroll dialog

/* =============================
 * Internal State
 * ============================= */
static int active_menu = -1;
typedef void (*MenuAction)(BufManager *bufmgr, Font font);
typedef struct {
    const char *label;
    const char *shortcut;
    MenuAction action;
} MenuItem;

bool Is_active_menu(void) { return active_menu != -1; }
typedef enum { DIALOG_NONE = 0, DIALOG_HELP, DIALOG_ABOUT } DialogState;

static DialogState current_dialog = DIALOG_NONE;

// Setter helper
static void UI_open_dialog(BufManager *bufmgr, DialogState state) {
    current_dialog = state;
    dialog_scroll_y = 0.0f;
    bufmgr->show_help = true;
}

/* ------------------------------- *
 * Extern Function
 * ------------------------------- */
extern void Nav_create_folder(BufManager *bufmgr,
                              Font font);  // Extern Fungsi create Folder (Nav_utils.c)
extern void Nav_open_file(BufManager *bufmgr, Font font);  // Open File (nav_utils.c)
extern void Nav_exit(BufManager *bufmgr, Font font);       // Exit (nav_utils.c)
extern void Nav_save_as(BufManager *bufmgr, Font font);    // Nav_save_as (nav_utils.c)
extern void Nav_save(BufManager *bufmgr, Font font);       // Nav_save (nav_utils.c)
extern void Nav_create_new_file(BufManager *bufmgr,
                                Font font);                // Nav_create_new_file (nav_utils.c)
extern void Nav_close_tab(BufManager *bufmgr, Font font);  // Nav_close_tab (nav_utils.c)
extern void Nav_copy(BufManager *bufmgr, Font font);       // Nav_copy (nav_utils.c)
extern void Nav_cut(BufManager *bufmgr, Font font);        // Nav_cut (nav_utils.c)
extern void Nav_paste(BufManager *bufmgr, Font font);      // Nav_paste (nav_utils.c)
extern void Nav_redo(BufManager *bufmgr, Font);            // Nav_redo (nav_utils.c)
extern void Nav_undo(BufManager *bufmgr, Font font);       // Nav_undo (nav_utils.c)

/**
 * Fungsi untuk membuka Help
 */
void Nav_show_help(BufManager *bufmgr, Font font) {
    (void)bufmgr;
    (void)font;
    UI_open_dialog(bufmgr, DIALOG_HELP);
}

/**
 * Fungsi untuk membuka About Page
 */
void Nav_show_about(BufManager *bufmgr, Font font) {
    (void)bufmgr;
    (void)font;
    UI_open_dialog(bufmgr, DIALOG_ABOUT);
}

/**
 * Git Panel
 */
void Nav_open_git(BufManager *bufmgr, Font font) {
    (void)bufmgr;
    (void)font;
    GitPopup_open();
}
/**
 * Fungsi untuk membuka FM UI
 */
void Nav_show_fm(BufManager *bufmgr, Font font) {
    (void)font;
    bufmgr->show_fm = !bufmgr->show_fm;
}

/**
 * Menu items
 */
static MenuItem file_items[] = {
    {"New File", "Ctrl+N", Nav_create_new_file},
    {"Open File", "Ctrl+O", Nav_open_file},
    {"Save File", "Ctrl+S", Nav_save},
    {"Save As", "Ctrl+Shift+S", Nav_save_as},
    {"Create Folder", "Ctrl+P", Nav_create_folder},
    {"File Manager", "Ctrl + F", Nav_show_fm},
    {"Git Panel", "Ctrl + G", Nav_open_git},
    {"Exit", "Ctrl+Q", Nav_exit},
};

static MenuItem edit_items[] = {{"Undo", "Ctrl+Z", Nav_undo},
                                {"Redo", "Ctrl+R", Nav_redo},
                                {"Cut", "Ctrl+X", Nav_cut},
                                {"Copy", "Ctrl+C", Nav_copy},
                                {"Paste", "Ctrl+V", Nav_paste}};

static MenuItem help_items[] = {{"Help", "", Nav_show_help}, {"About", "", Nav_show_about}};

/**
 * Render Dropdown Items
 */
static void draw_dropdown_items(int menu_idx, float x, float y, BufManager *bufmgr, Font font) {
    MenuItem *items = NULL;
    int count = 0;

    if (menu_idx == 0) {
        items = file_items;
        count = ARRAY_SIZE(file_items);
    } else if (menu_idx == 1) {
        items = edit_items;
        count = ARRAY_SIZE(edit_items);
    } else if (menu_idx == 2) {
        items = help_items;
        count = ARRAY_SIZE(help_items);
    }

    if (!items || count == 0) return;

    float item_h = 26.0f;
    float dropdown_w = 250.0f;
    float dropdown_h = count * item_h + 8.0f;

    Rectangle dropdown_rect = {x, y, dropdown_w, dropdown_h};
    Vector2 mouse_pos = GetMousePosition();

    // Render Box Dropdown & Border
    DrawRectangleRec(dropdown_rect, g_theme.bg_sidebar);
    DrawRectangleLinesEx(dropdown_rect, 1.0f, g_theme.active_tab);

    bool item_clicked = false;

    for (int i = 0; i < count; i++) {
        Rectangle item_rect = {x + 2.0f, y + 4.0f + (i * item_h), dropdown_w - 4.0f, item_h - 2.0f};
        bool is_hovered = CheckCollisionPointRec(mouse_pos, item_rect);

        if (is_hovered) {
            DrawRectangleRec(item_rect, g_theme.active_tab);

            // Jika item diklik
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                // Simpan action, tutup menu DULUAN
                MenuAction act = items[i].action;
                active_menu = -1;
                item_clicked = true;

                if (act != NULL) {
                    act(bufmgr, font);
                }
                break;  // Keluar dari loop item agar tidak memproses event lain
            }
        }

        // Render Label Text
        Color text_color = is_hovered ? g_theme.cursor : g_theme.text_normal;
        Vector2 text_pos = {item_rect.x + 8.0f, item_rect.y + 4.0f};
        DrawTextEx(font, items[i].label, text_pos, FONT_SIZE, 1.0f, text_color);

        // Render Shortcut Text
        if (items[i].shortcut[0] != '\0') {
            Vector2 sc_size = MeasureTextEx(font, items[i].shortcut, FONT_SIZE - 2, 1.0f);
            Vector2 sc_pos = {item_rect.x + dropdown_w - sc_size.x - 12.0f, item_rect.y + 5.0f};
            DrawTextEx(font, items[i].shortcut, sc_pos, FONT_SIZE, 1.0f, g_theme.text_muted);
        }
    }

    // Hanya tutup jika klik benar-benar di LUAR dropdown AND tidak mengeksekusi item
    if (!item_clicked && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (!CheckCollisionPointRec(mouse_pos, dropdown_rect) && mouse_pos.y > TAB_H) {
            active_menu = -1;
        }
    }
}

/**
 * Fungsi untuk Draw Menu
 */
int draw_menu(BufManager *bufmgr, Font font, Vector2 mouse_pos) {
    const char *menus[] = {"File", "Edit", "Help"};
    int num_menus = 3;
    int x = 12;
    float active_menu_x = 0;

    for (int m = 0; m < num_menus; m++) {
        Vector2 msize = MeasureTextEx(font, menus[m], FONT_SIZE, 1.0f);
        int menu_w = (int)msize.x + 16;
        Rectangle menu_rect = {(float)x, 4.0f, (float)menu_w, (float)(TAB_H - 8)};

        bool is_hovered = CheckCollisionPointRec(mouse_pos, menu_rect);
        bool is_active = (active_menu == m);

        if (is_active) active_menu_x = (float)x;

        // Hover effect saat menu lain sedang terbuka
        if (is_hovered && active_menu != -1 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            active_menu = m;
        }

        Color menu_text_color = (is_hovered || is_active) ? g_theme.cursor : g_theme.text_normal;

        if (is_hovered || is_active) {
            DrawRectangleRec(menu_rect, g_theme.active_tab);

            // Buka/Tutup menu header HANYA jika kursor di atas menu header (y <= TAB_H)
            if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && active_menu == -1) {
                active_menu = m;
            } else if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && is_active) {
                // Jika diklik lagi header yang sama
                active_menu = -1;
            }
        }

        Vector2 menu_pos = {(float)(x + 8), 10.0f};
        DrawTextEx(font, menus[m], menu_pos, FONT_SIZE, 1.0f, menu_text_color);

        x += menu_w + 4;
    }

    if (active_menu != -1) {
        draw_dropdown_items(active_menu, active_menu_x, (float)TAB_H, bufmgr, font);
    }

    return x + 12;
}

/**
 * Fungsi untuk Draw Tabs
 */
void draw_tabs(BufManager *bufmgr, Font font) {
    int win_w = GetRenderWidth();
    int win_h = GetRenderHeight();

    DrawRectangle(0, win_h - TAB_H, win_w, TAB_H, g_theme.bg_sidebar);

    Vector2 mouse_pos = GetMousePosition();
    int x = draw_menu(bufmgr, font, mouse_pos);

    // Separator tipis antara Menu Bar dan Tab List
    DrawLine(x + 2, 8, x + 2, TAB_H - 8, g_theme.text_muted);
    x += 12;  // Kasih jarak buat mulai ngerender Tab!

    /* ============================================================
     * RENDER TABS
     * ============================================================ */
    for (size_t i = 0; i < bufmgr->num_tabs; i++) {
        Buffer *buf = bufmgr->buf[i];
        if (!buf) continue;

        const char *name = buf->filename ? buf->filename : "Untitled";
        Color text_color = buf->is_dirty ? g_theme.cursor : g_theme.text_normal;

        Vector2 nsize = MeasureTextEx(font, name, FONT_SIZE, 1.0f);
        int tab_w = (int)nsize.x + 48;

        Color bg = (i == bufmgr->active_idx) ? g_theme.active_tab : g_theme.bg_sidebar;
        DrawRectangle(x, 4, tab_w, TAB_H - 8, bg);

        // Render Teks Nama File
        Vector2 text_pos = {(float)(x + 12), 10.0f};
        DrawTextEx(font, name, text_pos, FONT_SIZE, 1.0f, text_color);

        // Render Tombol "x"
        int x_btn_x = x + 12 + (int)nsize.x + 8;
        Rectangle x_rect = {(float)x_btn_x, 8.0f, 16.0f, 18.0f};

        bool is_x_hovered = CheckCollisionPointRec(mouse_pos, x_rect);
        Color x_color = is_x_hovered ? g_theme.selection : g_theme.text_muted;

        Vector2 x_pos = {(float)x_btn_x + 3, 9.0f};
        DrawTextEx(font, "x", x_pos, FONT_SIZE, 1.0f, x_color);

        // Handling Klik Tab & 'x'
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (CheckCollisionPointRec(mouse_pos, x_rect)) {
                bufmgr->active_idx = (int)i;
                Nav_close_tab(bufmgr, font);
                buf->scroll_y = 0;
                break;
            } else {
                Rectangle tab_rect = {(float)x, 4.0f, (float)tab_w, (float)(TAB_H - 8)};
                if (CheckCollisionPointRec(mouse_pos, tab_rect)) {
                    bufmgr->active_idx = (int)i;
                    buf->scroll_y = 0;
                }
            }
        }

        x += tab_w + 4;
    }

    if (bufmgr->num_tabs == 0) {
        BufManager_newtab(bufmgr, NULL);
    }

    /* Tombol '+' New Tab (Hanya render jika belum melewati limit MAX_TABS) */
    if (bufmgr->num_tabs < MAX_TABS) {
        Rectangle plus = {(float)x + 4, 4, 28, (float)(TAB_H - 8)};
        DrawRectangleRec(plus, g_theme.active_tab);

        Vector2 plus_pos = {(float)(x + 12), 8.0f};
        DrawTextEx(font, "+", plus_pos, 20, 1.0f, g_theme.text_normal);

        if (CheckCollisionPointRec(mouse_pos, plus) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            BufManager_newtab(bufmgr, NULL);
        }
    }
}

/**
 * Fungsi untuk render Help
 */
void draw_dialog_modal(BufManager *bufmgr, Font font) {
    if (current_dialog == DIALOG_NONE) return;

    static bool just_opened = true;
    if (just_opened) {
        just_opened = false;
        return;
    }

    Vector2 mouse_pos = GetMousePosition();
    float screen_w = (float)GetScreenWidth();
    float screen_h = (float)GetScreenHeight();

    // Render Backdrop Gelap Transparan
    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, g_theme.backdrop);

    // Dimensi Main Card Pop-up
    float card_w = 460.0f;
    float card_h = 360.0f;
    float card_x = (screen_w - card_w) / 2.0f;
    float card_y = (screen_h - card_h) / 2.0f;
    Rectangle card_rect = {card_x, card_y, card_w, card_h};

    // Render Container Card & Border
    DrawRectangleRec(card_rect, g_theme.bg_card);
    DrawRectangleLinesEx(card_rect, 1.0f, g_theme.border);

    // Header Title & Tombol Close [X]
    const char *title = (current_dialog == DIALOG_HELP) ? "Help / Shortcuts" : "About Editor";
    DrawTextEx(font, title, (Vector2){card_x + 20, card_y + 16}, FONT_SIZE + 2, 1.0f,
               g_theme.cursor);

    Rectangle close_rect = {card_x + card_w - 32, card_y + 12, 24, 24};
    bool close_hovered = CheckCollisionPointRec(mouse_pos, close_rect);
    Color close_color = close_hovered ? g_theme.error : g_theme.text_muted;
    DrawTextEx(font, "X", (Vector2){close_rect.x + 6, close_rect.y + 2}, FONT_SIZE, 1.0f,
               close_color);

    // Separator Line bawah Header
    DrawLine((int)card_x + 16, (int)card_y + 45, (int)(card_x + card_w - 16), (int)card_y + 45,
             g_theme.border);

    // Content Area dengan Scissor Clipping (Scroll Zone)
    Rectangle content_area = {card_x + 16, card_y + 52, card_w - 32, card_h - 68};

    if (current_dialog == DIALOG_HELP) {
        const char *shortcuts[] = {"--- File Operations ---",
                                   "Ctrl + N : New File",
                                   "Ctrl + O : Open File",
                                   "Ctrl + S : Save File",
                                   "Ctrl + Shift + S : Save As",
                                   "Ctrl + P : Create Folder",
                                   "Ctrl + F : File Manager",
                                   "CTRL + G : Git Panel",
                                   "",
                                   "--- Tab Management ---",
                                   "Ctrl + T : New Tab",
                                   "Ctrl + W : Close Tab",
                                   "Ctrl + Tab : Next Tab",
                                   "Ctrl + Shift + Tab : Prev Tab",
                                   "",
                                   "--- Editing ---",
                                   "Ctrl + Z : Undo",
                                   "Ctrl + R : Redo",
                                   "Ctrl + C : Copy",
                                   "Ctrl + X : Cut",
                                   "Ctrl + V : Paste",
                                   "",
                                   "--- LSP ---",
                                   "Ctrl + K : Show Hover Doc",
                                   "Escape / Esc : Hide LSP, Signature\nand Hover",
                                   "",
                                   "",
                                   "--- Navigation ---",
                                   "Ctrl + U : Jump up 5 lines",
                                   "Ctrl + D : Jump down 5 lines",
                                   "Ctrl + H : Move start of line",
                                   "Ctrl + L : Move end of line",
                                   "",
                                   "--- Application ---",
                                   "Ctrl + Q : Exit",
                                   "Ctrl + Shift + Q : Force Close"};
        int num_items = sizeof(shortcuts) / sizeof(shortcuts[0]);
        float line_height = 24.0f;
        float total_content_h = num_items * line_height;

        // Handling Mouse Wheel Scroll (Hanya scroll jika kursor berada di dalam area modal)
        if (CheckCollisionPointRec(mouse_pos, card_rect)) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0) {
                dialog_scroll_y -= wheel * 28.0f;  // Kecepatan scroll
            }
        }

        // Clamp Scroll Bounds (Supaya scroll tidak kebablasan ke atas/bawah)
        float max_scroll = total_content_h - content_area.height;
        if (max_scroll < 0) max_scroll = 0;
        if (dialog_scroll_y < 0) dialog_scroll_y = 0;
        if (dialog_scroll_y > max_scroll) dialog_scroll_y = max_scroll;

        // Potong Render Area (Clipping)
        BeginScissorMode((int)content_area.x, (int)content_area.y, (int)content_area.width,
                         (int)content_area.height);

        float line_y = content_area.y - dialog_scroll_y;
        for (int i = 0; i < num_items; i++) {
            // Bedakan warna header section dan item shortcut
            if (shortcuts[i][0] == '-') {
                DrawTextEx(font, shortcuts[i], (Vector2){content_area.x + 4, line_y}, FONT_SIZE,
                           1.0f, g_theme.cursor);
            } else {
                DrawTextEx(font, shortcuts[i], (Vector2){content_area.x + 12, line_y}, FONT_SIZE,
                           1.0f, g_theme.text_normal);
            }
            line_y += line_height;
        }

        // Render Custom Scrollbar Indicator (Visual Bar Samping)
        if (max_scroll > 0) {
            float scrollbar_h = (content_area.height / total_content_h) * content_area.height;
            float scrollbar_y = content_area.y + (dialog_scroll_y / max_scroll) *
                                                     (content_area.height - scrollbar_h);
            Rectangle scrollbar_rect = {card_x + card_w - 12, scrollbar_y, 4, scrollbar_h};
            DrawRectangleRounded(scrollbar_rect, 0.5f, 4, g_theme.text_muted);
        }

    } else if (current_dialog == DIALOG_ABOUT) {
        const char *help_msg = "TxtEd created and Maintanined by Nash.";

        DrawTextEx(font, "TxtEd v1.0 - Simple Text Editor",
                   (Vector2){content_area.x + 8, content_area.y + 20}, FONT_SIZE, 1.0f,
                   g_theme.text_highlight);
        DrawTextEx(font, help_msg, (Vector2){content_area.x + 8, content_area.y + 50}, FONT_SIZE,
                   1.0f, g_theme.text_normal);
    }

    EndScissorMode();

    // Handling Event Close
    bool clicked_close =
        CheckCollisionPointRec(mouse_pos, (Rectangle){card_x + card_w - 32, card_y + 12, 24, 24}) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    bool clicked_outside =
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse_pos, card_rect);
    bool pressed_esc = IsKeyPressed(KEY_ESCAPE);

    if (clicked_close || clicked_outside || pressed_esc) {
        current_dialog = DIALOG_NONE;
        just_opened = true;
        bufmgr->show_help = false;
    }
}
