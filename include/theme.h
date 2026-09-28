/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef THEME_H
#define THEME_H
#include "types.h"

// Global theme instance
extern UITheme g_theme;

// Directives API
void Theme_init(const char *filename);  // Set tema bawaan (Misal: Dark Modern)
void Theme_apply_raygui(void);          // Biar RayGUI otomatis ikut tema!

Settings Settings_load(void);
void Settings_apply(const Settings *st, BufManager *bufmgr, Font *font);

#endif  // THEME_H
