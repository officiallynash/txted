/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "clipboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rope.h"

/**
 * Struct untuk data Clipboard baik untuk Text Editor maupun OS
 */
struct Clipboard {
    unsigned char *data;
    size_t len;
};

/**
 * Helper internal: Kirim string ke OS Clipboard [PRIVATE API]
 */
static void os_clipboard_set(const char *text) {
    if (!text) return;
    const char *cmd = NULL;

#if defined(__APPLE__)
    cmd = "pbcopy";
#elif defined(_WIN32)
    cmd = "clip";
#else
    if (getenv("WAYLAND_DISPLAY")) {
        cmd = "wl-copy";
    } else {
        cmd = "xclip -selection clipboard";
    }
#endif

    FILE *pipe = popen(cmd, "w");
    if (!pipe) return;
    fputs(text, pipe);
    pclose(pipe);
}

/**
 * Helper internal: Ambil string dari OS Clipboard [PRIVATE API]
 */
static char *os_clipboard_get(size_t *out_len) {
    const char *cmd = NULL;

#if defined(__APPLE__)
    cmd = "pbpaste";
#elif defined(_WIN32)
    cmd = "powershell -command Get-Clipboard";
#else
    if (getenv("WAYLAND_DISPLAY")) {
        cmd = "wl-paste --no-newline";
    } else {
        cmd = "xclip -selection clipboard -o";
    }
#endif

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return NULL;

    size_t capacity = 1024;
    size_t len = 0;
    char *buffer = malloc(capacity);
    if (!buffer) {
        pclose(pipe);
        return NULL;
    }

    char chunk[256];
    while (fgets(chunk, sizeof(chunk), pipe)) {
        size_t chunk_len = strlen(chunk);
        if (len + chunk_len + 1 > capacity) {
            capacity *= 2;
            char *new_buf = realloc(buffer, capacity);
            if (!new_buf) {
                free(buffer);
                pclose(pipe);
                return NULL;
            }
            buffer = new_buf;
        }
        memcpy(buffer + len, chunk, chunk_len);
        len += chunk_len;
    }
    buffer[len] = '\0';
    pclose(pipe);

    if (out_len) *out_len = len;
    return buffer;
}

/**
 * Fungsi untuk inisiasi Clipboard [PUBLIC API]
 */
Clipboard *Clipboard_init() {
    Clipboard *init = malloc(sizeof(Clipboard));
    init->data = NULL;
    init->len = 0;
    return init;
}

/**
 * Fungsi untuk Free Clipboard [PUBLIC API]
 */
void Clipboard_free(Clipboard *clp) {
    if (!clp) return;
    if (clp->data) free(clp->data);
    clp->len = 0;
    free(clp);
}

/**
 * Fungsi untuk Set Clipboard [PUBLIC API]
 */
void Clipboard_set(Clipboard *clp, Bytes *bytes) {
    if (!clp || !bytes || !bytes->data || bytes->len == 0) return;
    if (clp->data) {
        free(clp->data);
        clp->data = NULL;
    }

    unsigned char *temp = malloc(bytes->len + 1);
    if (!temp) {
        clp->data = NULL;
        clp->len = 0;
        return;
    }

    memcpy(temp, bytes->data, bytes->len);
    temp[bytes->len] = '\0';

    clp->data = temp;
    clp->len = bytes->len;

    // Sinkronkan ke System Clipboard OS
    os_clipboard_set((const char *)clp->data);

    Bytes_free(bytes);
}

/**
 * Ambil teks clipboard terbaru dari OS [PRIVATE API]
 */
const char *Clipboard_get_text(Clipboard *clp) {
    if (!clp) return NULL;

    size_t os_len = 0;
    char *os_data = os_clipboard_get(&os_len);

    if (os_data && os_len > 0) {
        if (clp->data) free(clp->data);
        clp->data = (unsigned char *)os_data;
        clp->len = os_len;
    } else if (os_data) {
        free(os_data);
    }

    return (const char *)clp->data;
}
