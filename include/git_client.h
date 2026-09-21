/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef GIT_CLIENT_H
#define GIT_CLIENT_H

#include <raylib.h>

#include "types.h"

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
void draw_gitpopup(BufManager *bufmgr, Font font);
void Git_global_init(void);
void Git_global_shutdown(void);

#endif
