/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef SETTINGS_H
#define SETTINGS_H
#include "buffer_manager.h"
#include "raylib.h"

/**
 * Enum penanda posisi untuk File Manager
 */
typedef enum { FM_LEFT, FM_RIGHT } Settings_fm;

/**
 * Struct untuk data settings aplikasi
 */
typedef struct {
    int font_size;
    char theme[64];
    char font[128];
    Settings_fm fm_pos;
} Settings;

extern Settings default_settings;
void Settings_load(void);
void Settings_apply(BufManager *bufmgr, Font *font);

#endif
