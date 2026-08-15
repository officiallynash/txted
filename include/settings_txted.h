/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int font_size;
    char theme[64];
    char font[128];
} Settings;

extern Settings default_settings;
void Settings_load(void);

#endif
