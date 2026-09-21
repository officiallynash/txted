/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H
#include "types.h"

BufManager *BufManager_init(void);
void BufManager_newtab(BufManager *bufmgr, const char *filename);
void BufManager_open(BufManager *bufmgr, const char *filename);
Buffer *BufManager_getactive(BufManager *bufmgr);
void BufManager_switchtab(BufManager *bufmgr, SwitchTab direction);
void BufManager_closetab(BufManager *bufmgr);
void BufManager_destroy(BufManager *bufmgr);
size_t BufManager_checkdirty(BufManager *bufmgr);

#endif
