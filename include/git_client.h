/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef GIT_CLIENT_H
#define GIT_CLIENT_H

#include <raylib.h>
#include <stdbool.h>

#include "buffer_manager.h"

constexpr int MAX_FILE_GIT = 256;

/**
 * Struct untuk menampung Status dari File
 */
typedef struct {
    char path[512];
    char mark[4];
} GitFileStatus;

/**
 * Struct untuk menampung Git
 */
typedef struct {
    char author[64];
    char branch[128];
    bool is_repo;
    bool has_changes;
    int modified;
    int untracked;

    // File status
    GitFileStatus files[MAX_FILE_GIT];
    int file_count;
} GitStatus;

/**
 * Struct untuk GitPopup
 */
typedef struct {
    char message[256];
    char last_error[256];
    int selected;
    float list_scroll;
    bool edit_message;
} GitPopup;

// Extern Git Status, GitPopup dan Timer
extern GitStatus git;
extern float GitStatus_timer;
extern GitPopup git_popup;

bool GitStatus_refresh(const char *repo, GitStatus *git);
const char *Git_file_mark(const char *abs_path);
const char *Git_folder_mark(const char *dir_path);
void GitStatus_update(BufManager *bufmgr, float dt);
void GitStatus_force(void);
void format_time_ago(const char *author, double last_edited, char *out_str, size_t max_len);
void Git_fetch_file_async(const char *repo_path, const char *file_path, Buffer *buf);

void GitPopup_open(BufManager *bufmgr);
void GitPopup_close(BufManager *bufmgr);
void GitPopup_render(BufManager *bufmgr, Font font);
void Git_global_init(void);
void Git_global_shutdown(void);

#endif
