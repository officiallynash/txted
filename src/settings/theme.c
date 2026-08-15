/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "theme.h"

#include <raylib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raygui.h"

UITheme g_theme = {0};

typedef struct {
    const char *key;
    Color *ptr;
} ThemeMap;

/**
 * Fungsi helper atau internal untuk Loading Theme File [PRIVATE API]
 */
static void Theme_loader(const char *filename) {
    char theme_path[128];
    char *home = getenv("HOME");
    snprintf(theme_path, sizeof(theme_path), "%s/.config/txted/settings/theme/%s.ini", home,
             filename);

    FILE *fp = fopen(theme_path, "r");
    if (!fp) return;

    ThemeMap map[] = {
        {"bg_main", &g_theme.bg_main},
        {"bg_editor", &g_theme.bg_editor},
        {"bg_sidebar", &g_theme.bg_sidebar},
        {"bg_card", &g_theme.bg_card},
        {"border", &g_theme.border},
        {"active_tab", &g_theme.active_tab},
        {"active_line", &g_theme.active_line},
        {"line_num", &g_theme.line_num},
        {"backdrop", &g_theme.backdrop},
        {"selection", &g_theme.selection},
        {"cursor", &g_theme.cursor},
        {"accent", &g_theme.accent},
        {"text_normal", &g_theme.text_normal},
        {"text_muted", &g_theme.text_muted},
        {"text_highlight", &g_theme.text_highlight},
        {"success", &g_theme.success},
        {"warning", &g_theme.warning},
        {"error", &g_theme.error},
        {"info", &g_theme.info},
        {"keyword", &g_theme.keyword},
        {"type", &g_theme.type},
        {"comment", &g_theme.comment},
        {"string", &g_theme.string},
        {"function", &g_theme.function},
        {"number", &g_theme.number},
        {"method", &g_theme.method},
        {"operator", &g_theme.operator},
        {"constant", &g_theme.constant},
        {"bracket_match", &g_theme.bracket_match},
        {"bracket_match_bg", &g_theme.bracket_match_bg},
    };

    size_t map_size = sizeof(map) / sizeof(map[0]);

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64];
        int r, g, b, a;

        if (sscanf(line, "%63[^ =] = %d, %d, %d, %d", key, &r, &g, &b, &a) == 5) {
            for (size_t i = 0; i < map_size; i++) {
                if (strcmp(key, map[i].key) == 0) {
                    *map[i].ptr = (Color){r, g, b, a};
                    break;
                }
            }
        }
    }
    fclose(fp);
}

/**
 * Apply Theme
 * ke RayGUI
 */
void Theme_apply_raygui(void) {
    // Window /
    // Main
    // Background
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(g_theme.bg_main));

    // Tab Bar &
    // Status
    // Bar Area
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, ColorToInt(g_theme.bg_sidebar));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, ColorToInt(g_theme.active_tab));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, ColorToInt(g_theme.accent));

    // Pop-up /
    // Modal
    // Card Area
    GuiSetStyle(LABEL, BACKGROUND_COLOR, ColorToInt(g_theme.bg_card));

    // Borders
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(g_theme.border));
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(g_theme.accent));

    // Text
    // Colors
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, ColorToInt(g_theme.text_normal));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, ColorToInt(g_theme.text_highlight));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, ColorToInt(g_theme.text_highlight));
}

/**
 * Init Theme
 * [PUBLIC API]
 */
void Theme_init(const char *filename) {
    Theme_loader(filename);
    Theme_apply_raygui();
}
