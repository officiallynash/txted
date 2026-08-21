/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef THEME_H
#define THEME_H

#include "raylib.h"

/**
 * Struct untuk konfigurasi theme
 */
typedef struct {
    // Canvas / Window
    Color bg_main;     // Background editor utama
    Color bg_sidebar;  // Sidebar / Panel
    Color bg_editor;
    Color bg_card;  // Modal / Pop-up card (LSP, Prompt)
    Color border;   // Line/Border komponen UI
    Color active_tab;
    Color active_line;
    Color line_active;
    Color line_num;
    Color backdrop;  // Transparan

    // Status & Feedback
    Color selection;  // Highlight teks / item terpilih
    Color cursor;     // Warna kursor
    Color accent;     // Warna aksen/fokus utama

    // Typography (Teks)
    Color text_normal;     // Teks biasa
    Color text_muted;      // Teks redup (sub-label, placeholder)
    Color text_highlight;  // Teks saat di-hover/select

    // Syntax
    Color keyword;
    Color type;
    Color string;
    Color function;
    Color number;
    Color comment;
    Color method;
    Color operator;
    Color constant;

    Color bracket_match;
    Color bracket_match_bg;
    // Notification / Status Colors
    Color success;
    Color warning;
    Color error;
    Color info;
} UITheme;

// Global theme instance
extern UITheme g_theme;

// Directives API
void Theme_init(const char *filename);  // Set tema bawaan (Misal: Dark Modern)
void Theme_apply_raygui(void);          // Biar RayGUI otomatis ikut tema!

#endif  // THEME_H
