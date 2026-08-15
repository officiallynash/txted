/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "buffer_manager.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "clipboard.h"
#include "fs.h"
#include "git_client.h"
#include "notification.h"

/**
 * Fungsi internal untuk Pindah Folder ke Workspace [PRIVATE API]
 */
void BufManager_set_workspace(BufManager *bufmgr, const char *any_path) {
    if (!bufmgr || !any_path) return;

    char *root = Fs_find_project_root(any_path);
    if (!root) {
        root = Fs_dirname(any_path);
    }
    if (!root) return;

    if (bufmgr->path_root && strcmp(bufmgr->path_root, root) == 0) {
        free(root);
        return;
    }

    free(bufmgr->path_root);
    bufmgr->path_root = root;

    if (chdir(bufmgr->path_root) != 0) {
        Notif_show("Tidak bisa ke Workspace", NOTIF_WARNING, 3.0f);
    }

    GitStatus_refresh(bufmgr->path_root, &git);

    // Ambil active buffer & validasi path sebelum memanggil git fetch
    // Jika Path adalah Repo
    Buffer *active = BufManager_getactive(bufmgr);
    if (git.is_repo && active && active->path) {
        Git_fetch_file_blame(bufmgr->path_root, active->path, active);
        Git_fetch_file_diff(bufmgr->path_root, active->path, active);
    }
}

/**
 * Fungsi untuk inisiasi Buffer Manager [PUBLIC API]
 */
void BufManager_init(BufManager *bufmgr) {
    if (!bufmgr) return;
    bufmgr->active_idx = -1;
    bufmgr->num_tabs = 0;
    bufmgr->clp = Clipboard_init();
    bufmgr->show_help = false;
    bufmgr->show_fm = false;
    bufmgr->fm_width_ratio = 0.25f;
    bufmgr->focus_mode = WRITE;
    bufmgr->path_root = NULL;

    for (size_t i = 0; i < MAX_TABS; i++) {
        bufmgr->buf[i] = NULL;
    }
}

/**
 * Fungsi untuk mengambil Buffer yang aktif [PUBLIC API]
 */
Buffer *BufManager_getactive(BufManager *bufmgr) {
    if (!bufmgr || bufmgr->active_idx < 0 || (size_t)bufmgr->active_idx >= bufmgr->num_tabs) {
        return NULL;
    }

    return bufmgr->buf[bufmgr->active_idx];
}

/**
 * Fungsi untuk membuat Tab Baru [PUBLIC API]
 */
void BufManager_newtab(BufManager *bufmgr, const char *filename) {
    if (!bufmgr) return;

    if (bufmgr->num_tabs >= MAX_TABS) {
        Notif_show("Maksimum tab adalah 7", NOTIF_INFO, 3.0f);
        return;
    }

    Buffer *buf = filename ? Buffer_open(filename) : Buffer_new();
    if (!buf) return;

    bufmgr->buf[bufmgr->num_tabs] = buf;
    bufmgr->active_idx = (int)bufmgr->num_tabs;
    bufmgr->num_tabs++;

    if (!bufmgr->path_root && filename) {
        BufManager_set_workspace(bufmgr, filename);
    }

    // Sinkronasi dengan Git Blame dan Diff, jika Path itu Repo
    if (git.is_repo && buf && buf->path) {
        Git_fetch_file_blame(bufmgr->path_root, buf->path, buf);
        Git_fetch_file_diff(bufmgr->path_root, buf->path, buf);
    }
}

/**
 * Fungsi untuk membuka Buffer baru di Tab yang sama
 */
void BufManager_open(BufManager *bufmgr, const char *filename) {
    if (!bufmgr || !filename) return;

    int idx = bufmgr->active_idx;
    if (idx < 0 || (size_t)idx >= bufmgr->num_tabs) {
        // Kalau belum ada tab sama sekali, buat tab baru
        BufManager_newtab(bufmgr, filename);
        return;
    }

    // Free buffer lama di tab aktif
    if (bufmgr->buf[idx]) {
        Buffer_free(bufmgr->buf[idx]);
    }

    // Ganti dengan buffer baru
    bufmgr->buf[idx] = Buffer_open(filename);
    if (!bufmgr->path_root && filename) {
        BufManager_set_workspace(bufmgr, filename);
    }

    Buffer *active = BufManager_getactive(bufmgr);
    // Hanya ambil Blame dan Diff bila Path adalah Repo
    if (git.is_repo && active && active->path) {
        Git_fetch_file_blame(bufmgr->path_root, active->path, active);
        Git_fetch_file_diff(bufmgr->path_root, active->path, active);
    }
}

/**
 * Fungsi untuk pindah Tab [PUBLIC API]
 */
void BufManager_switchtab(BufManager *bufmgr, SwitchTab direction) {
    if (!bufmgr || bufmgr->num_tabs <= 1) return;

    switch (direction) {
        case PREV: {
            if (bufmgr->active_idx > 0) {
                bufmgr->active_idx--;
            }
            break;
        }
        case NEXT: {
            if ((size_t)bufmgr->active_idx + 1 < bufmgr->num_tabs) {
                bufmgr->active_idx++;
            }
            break;
        }
    }
}

/**
 * Fungsi untuk menutup Tab [PUBLIC API]
 */
void BufManager_closetab(BufManager *bufmgr) {
    if (!bufmgr || bufmgr->num_tabs == 0) return;

    int idx = bufmgr->active_idx;
    if (idx < 0 || (size_t)idx >= bufmgr->num_tabs) return;

    Buffer *active = bufmgr->buf[idx];
    if (!active) return;

    // Free buffer target & set NULL
    Buffer_free(active);
    bufmgr->buf[idx] = NULL;

    // ika ini satu-satunya tab tersisa, buat buffer baru
    if (bufmgr->num_tabs == 1) {
        bufmgr->buf[0] = Buffer_new();
        bufmgr->active_idx = 0;
        bufmgr->num_tabs = 1;
        return;
    }

    // Geser elemen array ke kiri
    for (size_t i = (size_t)idx; i < bufmgr->num_tabs - 1; i++) {
        bufmgr->buf[i] = bufmgr->buf[i + 1];
    }

    bufmgr->buf[bufmgr->num_tabs - 1] = NULL;
    bufmgr->num_tabs--;

    // Update active_idx secara pasti dan aman
    if (bufmgr->active_idx >= (int)bufmgr->num_tabs) {
        bufmgr->active_idx = (int)bufmgr->num_tabs - 1;
    }
}

/**
 * Fungsi untuk menutup atau menghapus Buffer Manager [PUBLIC API]
 */
void BufManager_destroy(BufManager *bufmgr) {
    if (!bufmgr) return;

    for (size_t i = 0; i < bufmgr->num_tabs; i++) {
        if (bufmgr->buf[i]) {
            Buffer_free(bufmgr->buf[i]);
            bufmgr->buf[i] = NULL;  // Safety nullify
        }
    }

    if (bufmgr->clp) {
        Clipboard_free(bufmgr->clp);
        bufmgr->clp = NULL;
    }

    if (bufmgr->path_root != NULL) free(bufmgr->path_root);
    bufmgr->active_idx = -1;
    bufmgr->num_tabs = 0;
}

/**
 * Check Is dirty [PUBLIC API]
 */
size_t BufManager_checkdirty(BufManager *bufmgr) {
    if (!bufmgr) return 0;

    size_t num = 0;
    for (size_t i = 0; i < bufmgr->num_tabs; i++) {
        // TAMBAHKAN NULL-CHECK DULU!
        if (bufmgr->buf[i] && bufmgr->buf[i]->is_dirty) {
            num++;
        }
    }

    return num;
}
