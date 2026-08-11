#include <raylib.h>
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
    char author[64];
} GitStatus;

/**
 * Struct untuk GitPopup
 */
typedef struct {
    bool open;
    char message[256];
    bool edit_message;
    int selected;
    char last_error[256];
    float list_scroll;
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
void Git_fetch_file_diff(const char *repo_path, const char *file_path, Buffer *buf);
void Git_fetch_file_blame(const char *repo_path, const char *file_path, Buffer *buf);

void GitPopup_open(void);
void GitPopup_close(void);
void GitPopup_render(BufManager *bufmgr, Font font);

#endif
