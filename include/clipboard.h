#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stddef.h>
#include <stdint.h>

#include "rope.h"

/**
 * Struct untuk data Clipboard baik untuk Text Editor maupun OS
 */
typedef struct {
    unsigned char *data;
    size_t len;
} Clipboard;

Clipboard *Clipboard_init();                       // Inisiasi Clipboard
void Clipboard_set(Clipboard *clp, Bytes *bytes);  // Insert ke clipboard dari Buffer
void Clipboard_free(Clipboard *clp);               // Membersihkan memori
const char *Clipboard_get_text(Clipboard *clp);

#endif
