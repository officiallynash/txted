#ifndef FS_H
#define FS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "result.h"
#include "ui.h"

/**
 * Struct untuk File List (Fuzzi search)
 */
typedef struct {
    PromptItem *items;
    size_t item_count;
    size_t capacity;
} FileList;

/**
 * Struct untuk membawa data ke Buffer
 */
typedef struct {
    char *full_path;
    uint8_t *data;
    size_t size;
    bool is_success;
} FileData;

Result Fs_open(const char *filename);
Result Fs_savefile(const char *path, const char *data, size_t len);
Result Fs_create(const char *filename);
void Fs_metadata_free(FileData *fm);
char *Fs_find_project_root(const char *filepath);
void draw_file_manager(BufManager *bufmgr, Font font);
char *Fs_dirname(const char *path);
// Initialize a FileList
FileList *FileList_init(size_t capacity);
void FileList_free(FileList *list);
void Scan_project_files(const char *path, FileList *list);
#endif  // !FS_H
