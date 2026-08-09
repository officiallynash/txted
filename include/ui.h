#ifndef UI_H
#define UI_H

#include <raylib.h>
#include <stdbool.h>

#include "buffer_manager.h"

#define TAB_H 36
#define STATUS_H 26
#define DIAG_PANEL_H 26
#define PAD_X 16
#define PAD_Y 10
#define FONT_SIZE 18
#define LINE_H 24
#define GUTTER_W 50

typedef struct {
    int win_h, win_w;
    int fm_x, fm_y, fm_w, fm_h;       // area file manager
    int editor_x, editor_y;           // origin editor
    int editor_w, editor_h;           // ukuran editor
    int gutter_screen_x;              // X gutter di layar
    int text_screen_x;                // X awal teks di layar
} EditorLayout;

typedef struct {
    char label[128];
    char subtext[128];
    int icon_id;
} PromptItem;

typedef struct {
    bool is_active;
    bool edit_mode;
    char label[64];
    char input_buf[256];
    int icon_id;

    PromptItem *items;
    size_t item_count;
    int selected_idx;
    int scroll_offset;
} FloatPrompt;

extern FloatPrompt g_prompt;

EditorLayout get_editor_layout(BufManager *bufmgr);

void draw_tabs(BufManager *bufmgr, Font font);
void draw_status(BufManager *bufmgr, Font font);
void draw_editor(BufManager *bufmgr, Font font);
void draw_diagnostic_bar(BufManager *bufmgr, Font font);
void draw_dialog_modal(BufManager *bufmgr, Font font);
void handle_input(BufManager *bufmgr, Font font);

// Pop up
char *FloatPrompt_ask(FloatPrompt *fp, const char *msg, const char *default_val, int icon_id,
                      Font font, BufManager *bufmgr);

// Pop up dengan box suggestion
char *FloatPrompt_ask_with_items(FloatPrompt *fp, const char *msg, const char *default_val,
                                 int icon_id, Font font, BufManager *bufmgr, PromptItem *items,
                                 size_t item_count);

#endif
