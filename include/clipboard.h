/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "rope.h"

typedef struct Clipboard Clipboard;

Clipboard *Clipboard_init();                       // Inisiasi Clipboard
void Clipboard_set(Clipboard *clp, Bytes *bytes);  // Insert ke clipboard dari Buffer
void Clipboard_free(Clipboard *clp);               // Membersihkan memori
const char *Clipboard_get_text(Clipboard *clp);

#endif
