#include "buffer_manager.h"
/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raygui.h"
#include "raylib.h"
#include "settings_txted.h"
#include "ui.h"

extern void Theme_init(const char *filename);  // Extern Theme init (theme.c)

/**
 * Init default Values
 */
Settings default_settings = {
    .font_size = 18, .theme = "default", .font = "JetBrainsMono-Regular.ttf", .fm_pos = FM_LEFT};

/**
 * Fungsi untuk Load Settings
 */
void Settings_load(void) {
    char setting_path[128];
    char *home = getenv("HOME");
    snprintf(setting_path, sizeof(setting_path), "%s/.config/txted/settings/settings.ini", home);

    FILE *fp = fopen(setting_path, "r");
    if (!fp) return;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[128];

        if (sscanf(line, " %63[^ =] = %127s", key, val) == 2) {
            val[strcspn(val, "\r\n")] = 0;

            // Font Size
            if (strcmp(key, "font_size") == 0) {
                default_settings.font_size = atoi(val);
            }
            // Theme
            else if (strcmp(key, "theme") == 0) {
                strncpy(default_settings.theme, val, sizeof(default_settings.theme));
            }
            // Font
            else if (strcmp(key, "font") == 0) {
                strncpy(default_settings.font, val, sizeof(default_settings.font));
            }
            // Posisi File Manager
            else if (strcmp(key, "fm_pos") == 0) {
                // Jika kiri
                if (strcmp(val, "left") == 0) {
                    default_settings.fm_pos = FM_LEFT;
                } else if (strcmp(val, "right") == 0) {  // jika kanan
                    default_settings.fm_pos = FM_RIGHT;
                } else {
                    default_settings.fm_pos = FM_LEFT;  // kasih default kiri aja
                }
            }
        }
    }
    fclose(fp);
}

/**
 * Fungsi untuk Apply setting
 */
void Settings_apply(BufManager *bufmgr, Font *font) {
    EditorLayout layout = get_editor_layout(bufmgr);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);

    // Init window dan dynamic title
    InitWindow(layout.win_w, layout.win_h, "TxtEd - Simple Text Editor");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    // Load font
    char font_path[512];
    char *home = getenv("HOME");
    snprintf(font_path, sizeof(font_path), "%s/.config/txted/settings/fonts/%s", home,
             default_settings.font);
    *font = LoadFontEx(font_path, FONT_SIZE, NULL, 0);

    Theme_init(default_settings.theme);  // Init Theme
    SetTextureFilter(font->texture, TEXTURE_FILTER_POINT);
    GuiSetFont(*font);
    GuiSetStyle(DEFAULT, TEXT_SIZE, FONT_SIZE);
}
