/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef THEME_H
#define THEME_H
#include "types.h"

extern Settings default_settings;
#define FONT_SIZE (float)default_settings.font_size

// Global theme instance
extern UITheme g_theme;

// Directives API
void Theme_init(const char *filename);  // Set tema bawaan (Misal: Dark Modern)
void Theme_apply_raygui(void);          // Biar RayGUI otomatis ikut tema!

void Settings_load(void);
void Settings_apply(BufManager *bufmgr, Font *font);

#endif  // THEME_H
