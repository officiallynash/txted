/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef FS_H
#define FS_H

#include <raylib.h>
#include <stddef.h>

#include "result.h"
#include "types.h"

Result Fs_open(const char *filename);
Result Fs_savefile(const char *path, const char *data, size_t len);
Result Fs_create(const char *filename);
void Fs_metadata_free(FileData *fm);
char *Fs_find_project_root(const char *filepath);
void draw_file_manager(BufManager *bufmgr, Font font);
char *Fs_dirname(const char *path);
void file_manager_refresh(void);

// Initialize a FileList untuk kebutuhan File Manager UI
FileList *FileList_init(size_t capacity);
void FileList_free(FileList *list);
void Scan_project_files(const char *path, FileList *list);

#endif  // !FS_H
