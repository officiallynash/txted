/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H
#define MAX_TABS 7

#include <stddef.h>

#include "buffer.h"

/**
 * Enum untuk Switch Tab
 */
typedef enum SwitchTab { PREV, NEXT } SwitchTab;

/**
 * Enum untuk Focus Mode
 */
typedef enum ViewFocus { WRITE, FILE_MANAGER } ViewFocus;

/**
 * Struct pembungkus untuk Buffer, ini jantungnya Multi tab
 */
typedef struct BufManager {
    Buffer *buf[MAX_TABS];  // Array buffer
    size_t num_tabs;
    int active_idx;
    Clipboard *clp;  // Clipboard

    bool show_help;        // Menu help (agar ga bentrok dengan Main Ui)
    bool show_fm;          // Flag penanda File Manager
    float fm_width_ratio;  // Ratio untuk File Manager
    ViewFocus focus_mode;  // Focus mode

    char *path_root;
} BufManager;

BufManager *BufManager_init(void);
void BufManager_newtab(BufManager *bufmgr, const char *filename);
void BufManager_open(BufManager *bufmgr, const char *filename);
Buffer *BufManager_getactive(BufManager *bufmgr);
void BufManager_switchtab(BufManager *bufmgr, SwitchTab direction);
void BufManager_closetab(BufManager *bufmgr);
void BufManager_destroy(BufManager *bufmgr);
size_t BufManager_checkdirty(BufManager *bufmgr);

#endif
