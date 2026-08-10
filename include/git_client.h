#ifndef GIT_CLIENT_H
#define GIT_CLIENT_H

#include <stdbool.h>

#include "buffer_manager.h"

#define MAX_FILE_GIT 256
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
    bool is_repo;
    char branch[128];
    bool has_changes;
    int modified;
    int untracked;

    // File status
    GitFileStatus files[MAX_FILE_GIT];
    int file_count;
} GitStatus;

extern GitStatus git;
extern float GitStatus_timer;

bool GitStatus_refresh(const char *repo, GitStatus *git);
const char *Git_file_mark(const char *abs_path);
const char *Git_folder_mark(const char *dir_path);
void GitStatus_update(BufManager *bufmgr, float dt);
void GitStatus_force(void);

#endif