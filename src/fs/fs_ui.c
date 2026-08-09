#include <dirent.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer.h"
#include "buffer_manager.h"
#include "fs.h"
#include "theme.h"
#include "ui.h"

typedef struct FileNode {
    char name[256];
    char path[512];
    bool is_directory;
    bool is_expanded;
    bool is_loaded;
    struct FileNode **children;
    int child_count;
    int child_capacity;
} FileNode;

static FileNode *g_fm_root = NULL;
static float g_fm_scroll_y = 0.0f;
static char g_active_file_path[512] = {0};
static char g_loaded_root_path[512] = "";
extern char *format_pretty_path(const char *path);  // (fs.c)

/**
 * Fungsi internal untuk sorting Folder dan Files
 */
static int file_node_cmp(const void *a, const void *b) {
    const FileNode *na = *(const FileNode *const *)a;
    const FileNode *nb = *(const FileNode *const *)b;

    int a_hidden = (na->name[0] == '.');
    int b_hidden = (nb->name[0] == '.');
    int a_dir = na->is_directory;
    int b_dir = nb->is_directory;

    // Group priority: hidden-dir < normal-dir < normal-file < hidden-file
    int group_a, group_b;

    if (a_dir && a_hidden)
        group_a = 0;
    else if (a_dir && !a_hidden)
        group_a = 1;
    else if (!a_dir && !a_hidden)
        group_a = 2;
    else
        group_a = 3;  // hidden file

    if (b_dir && b_hidden)
        group_b = 0;
    else if (b_dir && !b_hidden)
        group_b = 1;
    else if (!b_dir && !b_hidden)
        group_b = 2;
    else
        group_b = 3;

    if (group_a != group_b) return group_a - group_b;

    // Dalam group yang sama → abjad (case-insensitive lebih enak)
    return strcasecmp(na->name, nb->name);
}

/**
 * Fungsi internal untuk mengecek Is_Dir
 */
static bool is_directory_native(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return false;
}

/**
 * Fungsi untuk Membuat File list
 */
FileNode *FileNode_create(const char *path, const char *name, bool is_dir) {
    FileNode *node = (FileNode *)calloc(1, sizeof(FileNode));
    if (node) {
        strncpy(node->name, name, sizeof(node->name) - 1);
        strncpy(node->path, path, sizeof(node->path) - 1);
        node->is_directory = is_dir;
    }
    return node;
}

/**
 * Fungsi untuk membuat Child dari Parent Folder
 */
static void FileNode_add_child(FileNode *parent, FileNode *child) {
    if (!parent || !child) return;

    if (parent->child_count >= parent->child_capacity) {
        int new_cap = (parent->child_capacity == 0) ? 8 : parent->child_capacity * 2;
        FileNode **new_children =
            (FileNode **)realloc(parent->children, sizeof(FileNode *) * new_cap);
        if (!new_children) return;

        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
}

/**
 * Fungsi untuk me-load Folder di Child
 */
void FileNode_load_children(FileNode *node) {
    if (!node || !node->is_directory || node->is_loaded) return;

    DIR *dir = opendir(node->path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char child_path[1024];
            if (strcmp(node->path, ".") == 0) {
                snprintf(child_path, sizeof(child_path), "%s", entry->d_name);
            } else {
                snprintf(child_path, sizeof(child_path), "%s/%s", node->path, entry->d_name);
            }

            bool is_child_dir = is_directory_native(child_path);
            FileNode *child = FileNode_create(child_path, entry->d_name, is_child_dir);
            FileNode_add_child(node, child);
        }
        closedir(dir);
    }

    // Sort setelah semua child masuk
    if (node->child_count > 1) {
        qsort(node->children, (size_t)node->child_count, sizeof(FileNode *), file_node_cmp);
    }

    node->is_loaded = true;
}

/**
 * Fungsi untuk build File List
 */
FileNode *FileNode_build(const char *rootpath, const char *name) {
    bool is_dir = is_directory_native(rootpath);
    FileNode *node = FileNode_create(rootpath, name, is_dir);

    if (is_dir) {
        FileNode_load_children(node);
    }

    return node;
}

/**
 * Fungsi untuk Menghapus File List
 */
void FileNode_free(FileNode *node) {
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        FileNode_free(node->children[i]);
    }
    free(node->children);
    free(node);
}

/**
 * Fungsi untuk Draw File List recursive
 */
static void draw_file_node_recursive(FileNode *node, Font font, EditorLayout L, int depth,
                                     float *current_y, Vector2 mouse_pos, BufManager *bufmgr) {
    if (!node) return;

    float item_h = 22.0f;
    float item_y = *current_y - g_fm_scroll_y;
    float indent = depth * 14.0f + 12.0f;

    Rectangle item_bounds = {(float)L.fm_x + 4, item_y, (float)L.fm_w - 8, item_h};

    float visible_top = L.fm_y + 32.0f;
    float visible_bottom = L.fm_y + L.fm_h;

    if (item_y + item_h > visible_top && item_y < visible_bottom) {
        bool is_hovered = CheckCollisionPointRec(mouse_pos, item_bounds);
        bool is_selected = (!node->is_directory && strcmp(g_active_file_path, node->path) == 0);

        if (is_selected) {
            DrawRectangleRounded(item_bounds, 0.15f, 4, g_theme.active_line);
        } else if (is_hovered) {
            DrawRectangleRounded(item_bounds, 0.15f, 4, g_theme.border);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (node->is_directory) {
                    node->is_expanded = !node->is_expanded;
                    if (node->is_expanded && !node->is_loaded) {
                        FileNode_load_children(node);
                    }
                } else {
                    strncpy(g_active_file_path, node->path, sizeof(g_active_file_path));
                    Buffer *buf = BufManager_getactive(bufmgr);

                    if (buf && buf->is_dirty) {
                        BufManager_newtab(bufmgr, node->path);
                    } else {
                        BufManager_open(bufmgr, node->path);
                    }
                }
            }
        }

        const char *prefix = node->is_directory ? (node->is_expanded ? "v " : "> ") : "  ";
        Color text_color = node->is_directory
                               ? g_theme.keyword
                               : (is_selected ? g_theme.cursor : g_theme.text_normal);

        char label[300];
        snprintf(label, sizeof(label), "%s%s", prefix, node->name);

        Vector2 text_pos = {(float)L.fm_x + indent, item_y + 2.0f};
        DrawTextEx(font, label, text_pos, FONT_SIZE, 1.0f, text_color);
    }

    *current_y += item_h;

    if (node->is_directory && node->is_expanded) {
        for (int i = 0; i < node->child_count; i++) {
            draw_file_node_recursive(node->children[i], font, L, depth + 1, current_y, mouse_pos,
                                     bufmgr);
        }
    }
}

/**
 * Fungsi untuk Draw atau Render utama [PUBLIC API]
 */
void draw_file_manager(BufManager *bufmgr, Font font) {
    if (!bufmgr || !bufmgr->show_fm) return;

    const char *wanted = bufmgr->path_root;
    if (wanted && wanted[0] && strcmp(g_loaded_root_path, wanted) != 0) {
        if (g_fm_root) {
            FileNode_free(g_fm_root);
            g_fm_root = NULL;
        }

        strncpy(g_loaded_root_path, wanted, sizeof(g_loaded_root_path) - 1);
        g_loaded_root_path[sizeof(g_loaded_root_path) - 1] = '\0';

        // Ambil nama folder terakhir untuk display
        const char *folder_name = strrchr(wanted, '/');
        folder_name = folder_name ? folder_name + 1 : wanted;

        // PENTING: path absolut, bukan "."
        g_fm_root = FileNode_build(wanted, folder_name);
        g_fm_scroll_y = 0.0f;
    }

    if (!g_fm_root) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) {
            strncpy(g_loaded_root_path, cwd, sizeof(g_loaded_root_path) - 1);
            const char *folder_name = strrchr(cwd, '/');
            folder_name = folder_name ? folder_name + 1 : cwd;
            g_fm_root = FileNode_build(cwd, folder_name);  // absolut
        }
    }

    EditorLayout L = get_editor_layout(bufmgr);
    Vector2 mouse_pos = GetMousePosition();

    DrawRectangle(L.fm_x, L.fm_y, L.fm_w, L.fm_h, g_theme.bg_sidebar);
    DrawLine(L.fm_x + L.fm_w - 1, L.fm_y, L.fm_x + L.fm_w - 1, L.fm_y + L.fm_h, g_theme.border);

    DrawTextEx(font, "EXPLORER", (Vector2){(float)(L.fm_x + 12), (float)(L.fm_y + 8)}, FONT_SIZE,
               1.0f, g_theme.cursor);

    Rectangle fm_rect = {(float)L.fm_x, (float)L.fm_y, (float)L.fm_w,
                         (float)(L.fm_h - DIAG_PANEL_H)};

    if (CheckCollisionPointRec(mouse_pos, fm_rect)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            g_fm_scroll_y -= wheel * 22.0f;
            if (g_fm_scroll_y < 0.0f) g_fm_scroll_y = 0.0f;
        }
    }

    float content_start_y = L.fm_y + 32.0f;
    float current_y = content_start_y;

    // Batasi ScissorMode agar tree view tidak menimpa panel workspace info di bawah
    int scissor_h = L.fm_h - 32 - DIAG_PANEL_H;
    if (scissor_h > 0) {
        BeginScissorMode(L.fm_x, (int)content_start_y, L.fm_w - 1, scissor_h);

        if (g_fm_root) {
            for (int i = 0; i < g_fm_root->child_count; i++) {
                draw_file_node_recursive(g_fm_root->children[i], font, L, 0, &current_y, mouse_pos,
                                         bufmgr);
            }
        }

        EndScissorMode();
    }

    // --- PANEL WORKSPACE INFO (Bagian Bawah File Manager) ---
    float ws_y = L.fm_y + L.fm_h - DIAG_PANEL_H;
    DrawLine(L.fm_x, (int)ws_y, L.fm_x + L.fm_w - 1, (int)ws_y, g_theme.border);

    const char *ws_path =
        bufmgr->path_root ? format_pretty_path(bufmgr->path_root) : g_loaded_root_path;

    char ws_label[256];
    snprintf(ws_label, sizeof(ws_label), "ROOT: %s", ws_path[0] ? ws_path : "No Workspace");

    // Clipping teks workspace jika terlalu panjang
    BeginScissorMode(L.fm_x + 4, (int)ws_y, L.fm_w - 8, DIAG_PANEL_H);
    DrawTextEx(font, ws_label, (Vector2){(float)(L.fm_x + 8), ws_y + 4.0f}, FONT_SIZE * 0.85f, 1.0f,
               g_theme.text_normal);
    EndScissorMode();
}
