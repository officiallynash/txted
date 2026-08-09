#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H
#define MAX_TABS 7

#include <stddef.h>

#include "buffer.h"
#include "clipboard.h"

/**
 * Enum untuk Switch Tab
 */
typedef enum { PREV, NEXT } SwitchTab;

/**
 * Enum untuk Focus Mode
 */
typedef enum { WRITE, FILE_MANAGER } ViewFocus;

/**
 * Struct pembungkus untuk Buffer, ini jantungnya Multi tab
 */
typedef struct {
    Buffer *buf[MAX_TABS];
    size_t num_tabs;
    size_t active_idx;
    Clipboard *clp;

    bool show_help;
    bool show_fm;
    float fm_width_ratio;
    ViewFocus focus_mode;

    char *path_root;
} BufManager;

void BufManager_init(BufManager *bufmgr);
void BufManager_newtab(BufManager *bufmgr, const char *filename);
void BufManager_open(BufManager *bufmgr, const char *filename);
Buffer *BufManager_getactive(BufManager *bufmgr);
void BufManager_switchtab(BufManager *bufmgr, SwitchTab direction);
void BufManager_closetab(BufManager *bufmgr);
void BufManager_destroy(BufManager *bufmgr);
size_t BufManager_checkdirty(BufManager *bufmgr);

#endif
