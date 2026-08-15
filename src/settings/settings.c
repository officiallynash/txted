/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings_txted.h"

/**
 * Init default Values
 */
Settings default_settings = {
    .font_size = 18, .theme = "default", .font = "JetBrainsMono-Regular.ttf"};

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
        }
    }
    fclose(fp);
}
