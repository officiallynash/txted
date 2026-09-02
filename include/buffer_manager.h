/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "buffer.h"

// define max tab
constexpr int MAX_TABS = 7;

// Daftar semua Flag di Struct Buffer Manager
// Kita gunakan Bitwise untuk menghemat memory
// Selain itu untuk mempercepat Toggle ketika perpindahan Flag
constexpr uint8_t TXTED_REQ_EXIT = (1 << 0);
constexpr uint8_t TXTED_EXIT = (1 << 1);
constexpr uint8_t TXTED_SHOW_FM = (1 << 2);
constexpr uint8_t TXTED_SHOW_HELP = (1 << 3);
constexpr uint8_t TXTED_WRITE = (1 << 4);
constexpr uint8_t TXTED_FILE_MANAGER = (1 << 5);

/**
 * Enum untuk Switch Tab
 */
typedef enum SwitchTab { PREV, NEXT } SwitchTab;

/**
 * Struct pembungkus untuk Buffer, ini jantungnya Multi tab
 */
typedef struct BufManager {
    Buffer *buf[MAX_TABS];  // Array buffer
    size_t num_tabs;        // Untuk num tabs
    int active_idx;         // Active idx
    Clipboard *clp;         // Clipboard
    float fm_width_ratio;   // Ratio untuk File Manager
    char *path_root;        // Menyimpan path root, untuk kebutuhan workspace
    uint8_t win_flags;      // Flag untuk menampung state window, misal minta exit, dll
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
