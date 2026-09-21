/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <dirent.h>
#include <raylib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "buffer_manager.h"
#include "fs.h"
#include "git_client.h"
#include "result.h"
#include "theme.h"
#include "ui.h"

extern bool Is_active_menu(void);  // Cek apakah active menu aktif. (tab.c)

/**
 * Struct untuk menampung File List
 */
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

// Internal state
static FileNode *g_fm_root = nullptr;
static float g_fm_scroll_y = 0.0f;
static char g_active_file_path[512] = {0};
static char g_loaded_root_path[512] = "";

// State untuk Navigasi Keyboard
// Karena ini lokal lebih baik terpisah dari BufManager
static int g_fm_selected_index = 0;
static FileNode *g_visible_nodes[1024];  // Max node yang tampil/visible
static int g_visible_count = 0;
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
 * Panggil fungsi ini untuk memaksa File Manager me-load ulang struktur folder dari disk
 */
void file_manager_refresh(void) {
    if (g_fm_root) {
        FileNode_free(g_fm_root);
        g_fm_root = nullptr;
    }
    // Reset path terload agar draw_file_manager otomatis re-build root node
    g_loaded_root_path[0] = '\0';
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
    FileNode *node = calloc(1, sizeof(FileNode));
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
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char child_path[1024] = {0};
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
 * Fungsi untuk Draw File List recursive
 * Disesuaikan agar tinggi item, indentasi, dan posisi vertikal fleksibel terhadap ukuran font
 * dinamis.
 */
static void draw_file_node_recursive(FileNode *node, Font font, int depth, float *current_y,
                                     Vector2 mouse_pos, BufManager *bufmgr) {
    if (!node) return;

    EditorLayout layout = get_editor_layout(bufmgr);
    int fm_y = layout.fm_y;
    int fm_x = layout.fm_x;
    int fm_h = layout.fm_h;
    int fm_w = layout.fm_w;

    float current_font_size = (float)font.baseSize;
    // Item height disesuaikan dengan tinggi font ditambah vertical padding
    float item_h = current_font_size + 6.0f;
    float item_y = *current_y - g_fm_scroll_y;
    float indent = depth * 14.0f + 12.0f;

    Rectangle item_bounds = {(float)fm_x + 4.0f, item_y, (float)fm_w - 8.0f, item_h};

    float visible_top = fm_y + current_font_size + 16.0f;
    float visible_bottom = fm_y + fm_h;

    if (item_y + item_h > visible_top && item_y < visible_bottom) {
        // Tambahan cek apakah Active menu sedang aktif

        bool is_hovered = CheckCollisionPointRec(mouse_pos, item_bounds) && !Is_active_menu();

        // Item terpilih via Keyboard ATAU Active File Path (Mouse)
        bool is_selected = false;
        if (bufmgr->mode == FILE_MANAGER) {
            if (g_fm_selected_index >= 0 && g_fm_selected_index < g_visible_count) {
                is_selected = (g_visible_nodes[g_fm_selected_index] == node);
            }
        } else {
            is_selected = (!node->is_directory && strcmp(g_active_file_path, node->path) == 0 &&
                           !Is_active_menu());
        }

        if (is_selected) {
            DrawRectangleRounded(item_bounds, 0.15f, 4, g_theme.active_line);
        } else if (is_hovered) {
            DrawRectangleRounded(item_bounds, 0.15f, 4, g_theme.border);
        }

        // Logic Click Mouse
        if (is_hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            // Update selected index ke node ini saat di-click
            for (int i = 0; i < g_visible_count; i++) {
                if (g_visible_nodes[i] == node) {
                    g_fm_selected_index = i;
                    break;
                }
            }

            if (node->is_directory) {
                node->is_expanded = !node->is_expanded;
                if (node->is_expanded && !node->is_loaded) {
                    FileNode_load_children(node);
                }
            } else {
                strncpy(g_active_file_path, node->path, sizeof(g_active_file_path));
                Buffer *buf = BufManager_getactive(bufmgr);
                if (buf && HAS_FLAG(buf->buf_flags, BUF_IS_DIRTY)) {
                    BufManager_newtab(bufmgr, node->path);
                } else {
                    BufManager_open(bufmgr, node->path);
                }

                bufmgr->mode = WRITE;
            }
        }

        const char *prefix = node->is_directory ? (node->is_expanded ? "v " : "> ") : "  ";
        Color text_color = node->is_directory
                               ? g_theme.keyword
                               : (is_selected ? g_theme.cursor : g_theme.text_normal);
        // Git Mark (Untuk tracking Changes dan lain2)
        const char *mark = "";
        if (node->is_directory) {
            mark = Git_folder_mark(node->path);
        } else {
            mark = Git_file_mark(node->path);
        }

        char label[300] = {0};
        if (mark[0]) {
            snprintf(label, sizeof(label), "%s%s [%s]", prefix, node->name, mark);
        } else {
            snprintf(label, sizeof(label), "%s%s", prefix, node->name);
        }

        if (mark[0] == '~')
            text_color = g_theme.warning;
        else if (mark[0] == '-')
            text_color = g_theme.error;
        else if (mark[0] == '+' || mark[0] == '?')
            text_color = g_theme.cursor;
        else if (mark[0] == 'x')
            text_color = g_theme.text_muted;

        // Posisi Y diselaraskan secara vertikal tepat di tengah baris item
        float text_y = item_y + (item_h - current_font_size) / 2.0f;
        Vector2 text_pos = {(float)fm_x + indent, text_y};
        DrawTextEx(font, label, text_pos, current_font_size, 1.0f, text_color);
    }

    *current_y += item_h;

    if (node->is_directory && node->is_expanded) {
        for (int i = 0; i < node->child_count; i++) {
            draw_file_node_recursive(node->children[i], font, depth + 1, current_y, mouse_pos,
                                     bufmgr);
        }
    }
}

/**
 * Mengumpulkan semua node yang visible (termasuk child dari folder yang expanded)
 */
static void collect_visible_nodes(FileNode *node) {
    if (!node || g_visible_count >= 1024) return;

    g_visible_nodes[g_visible_count++] = node;

    if (node->is_directory && node->is_expanded) {
        for (int i = 0; i < node->child_count; i++) {
            collect_visible_nodes(node->children[i]);
        }
    }
}

/**
 * Memastikan item yang dipilih via keyboard selalu berada di area pandang (scissor)
 */
static void ensure_node_visible(int index, float header_h, float item_h, float view_h) {
    (void)header_h;
    float item_top = index * item_h;
    float item_bottom = item_top + item_h;

    if (item_top < g_fm_scroll_y) {
        g_fm_scroll_y = item_top;
    } else if (item_bottom > g_fm_scroll_y + view_h) {
        g_fm_scroll_y = item_bottom - view_h;
    }
}

/**
 * Fungsi untuk Draw atau Render utama [PUBLIC API]
 * Layout header, scissor height, dan scroll calculation disinkronkan dengan font dinamis.
 */
void draw_file_manager(BufManager *bufmgr, Font font) {
    if (!bufmgr || !HAS_FLAG(bufmgr->win_flags, TXTED_SHOW_FM)) return;

    // Reset dan Rebuild ulang
    g_visible_count = 0;
    if (g_fm_root) {
        for (int i = 0; i < g_fm_root->child_count; i++) {
            collect_visible_nodes(g_fm_root->children[i]);
        }
    }

    float current_font_size = (float)font.baseSize;
    float item_h = current_font_size + 6.0f;

    /**
     * Navigation, Harusnya memang terpisah, tetapi jika untuk file manager di bentuk dengan struct
     * akan kelihatan ribet, jadi lebih baik kita masukin ke dalam draw saja,
     * terlebih untuk mempersingkat fungsi yang di panggil di main.c
     */
    // Safety clamp untuk selected index
    if (g_fm_selected_index >= g_visible_count) g_fm_selected_index = g_visible_count - 1;
    if (g_fm_selected_index < 0) g_fm_selected_index = 0;

    // Handling keyboard event
    if (bufmgr->mode == FILE_MANAGER && !Is_active_menu() && g_visible_count > 0) {
        int key = GetKeyPressed();

        // Panah Bawah / Down
        if (key == KEY_DOWN || IsKeyPressedRepeat(KEY_DOWN)) {
            if (g_fm_selected_index < g_visible_count - 1) {
                g_fm_selected_index++;
            }
        }
        // Panah Atas / Up
        else if (key == KEY_UP || IsKeyPressedRepeat(KEY_UP)) {
            if (g_fm_selected_index > 0) {
                g_fm_selected_index--;
            }
        }
        // Enter / Space untuk Buka File atau Expand Folder
        else if (key == KEY_ENTER || key == KEY_SPACE) {
            FileNode *sel = g_visible_nodes[g_fm_selected_index];
            if (sel->is_directory) {
                sel->is_expanded = !sel->is_expanded;
                if (sel->is_expanded && !sel->is_loaded) {
                    FileNode_load_children(sel);
                }
            } else {
                strncpy(g_active_file_path, sel->path, sizeof(g_active_file_path));
                Buffer *buf = BufManager_getactive(bufmgr);
                if (buf && HAS_FLAG(buf->buf_flags, BUF_IS_DIRTY)) {
                    BufManager_newtab(bufmgr, sel->path);
                } else {
                    BufManager_open(bufmgr, sel->path);
                }

                // Pindah mode ke WRITE setelah buka file
                bufmgr->mode = WRITE;
            }
        } else if (key == KEY_ESCAPE) {
            // Untuk kembali ke Write dan tutup si FM
            bufmgr->mode = WRITE;
            CLR_FLAG(bufmgr->win_flags, TXTED_SHOW_FM);
        }

        // Ctrl + Panah kiri untuk pindah ke mode Write tanpa menutup FM
        if (IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_CONTROL)) {
            if (default_settings.fm_pos == FM_LEFT && IsKeyPressed(KEY_RIGHT)) {
                bufmgr->mode = WRITE;
            } else if (default_settings.fm_pos == FM_RIGHT && IsKeyPressed(KEY_LEFT)) {
                bufmgr->mode = WRITE;
            }
        }
    }

    // Load Path Root
    const char *wanted = bufmgr->path_root;
    if (wanted && wanted[0] && strcmp(g_loaded_root_path, wanted) != 0) {
        if (g_fm_root) {
            FileNode_free(g_fm_root);
            g_fm_root = nullptr;
        }

        strncpy(g_loaded_root_path, wanted, sizeof(g_loaded_root_path) - 1);
        g_loaded_root_path[sizeof(g_loaded_root_path) - 1] = '\0';

        const char *folder_name = strrchr(wanted, '/');
        folder_name = folder_name ? folder_name + 1 : wanted;

        g_fm_root = FileNode_build(wanted, folder_name);
        g_fm_scroll_y = 0.0f;
        g_fm_selected_index = 0;
    }

    if (!g_fm_root) {
        char cwd[512] = {0};
        if (getcwd(cwd, sizeof(cwd))) {
            strncpy(g_loaded_root_path, cwd, sizeof(g_loaded_root_path) - 1);
            const char *folder_name = strrchr(cwd, '/');
            folder_name = folder_name ? folder_name + 1 : cwd;
            g_fm_root = FileNode_build(cwd, folder_name);
        }
    }

    /**
     * Draw dan Mouse Configuration
     */
    EditorLayout layout = get_editor_layout(bufmgr);
    Vector2 mouse_pos = GetMousePosition();

    int fm_w = layout.fm_w;
    int fm_h = layout.fm_h;
    int fm_x = layout.fm_x;
    int fm_y = layout.fm_y;

    DrawRectangle(fm_x, fm_y, fm_w, fm_h, g_theme.bg_sidebar);
    DrawLine(fm_x + fm_w - 1, fm_y, fm_x + fm_w - 1, fm_y + fm_h, g_theme.border);

    float header_h = current_font_size + 16.0f;
    float header_text_y = fm_y + (header_h - current_font_size) / 2.0f;
    DrawTextEx(font, "FILE EXPLORER", (Vector2){(float)(fm_x + 12), header_text_y},
               current_font_size, 1.0f, g_theme.cursor);

    Rectangle fm_rect = {(float)fm_x, (float)fm_y, (float)fm_w, (float)(fm_h - DIAG_PANEL_H)};

    float content_start_y = fm_y + header_h;
    float current_y = content_start_y;
    int scissor_h = fm_h - (int)header_h - DIAG_PANEL_H;

    // Update Scroll Position Otomatis Saat Menggunakan Keyboard Up/Down
    if (bufmgr->mode == FILE_MANAGER && scissor_h > 0) {
        ensure_node_visible(g_fm_selected_index, header_h, item_h, (float)scissor_h);
    }

    // Draw item dari File Manager
    if (scissor_h > 0) {
        BeginScissorMode(fm_x, (int)content_start_y, fm_w - 1, scissor_h);

        if (g_fm_root) {
            for (int i = 0; i < g_fm_root->child_count; i++) {
                draw_file_node_recursive(g_fm_root->children[i], font, 0, &current_y, mouse_pos,
                                         bufmgr);
            }
        }

        EndScissorMode();
    }

    // Scroll Bar dan Mouse
    float total_content_h = current_y - content_start_y;
    float view_h = (float)scissor_h;
    float max_scroll = (total_content_h > view_h) ? (total_content_h - view_h) : 0.0f;

    if (g_fm_scroll_y < 0.0f) g_fm_scroll_y = 0.0f;
    if (g_fm_scroll_y > max_scroll) g_fm_scroll_y = max_scroll;

    if (CheckCollisionPointRec(mouse_pos, fm_rect) && !Is_active_menu()) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            g_fm_scroll_y -= wheel * (current_font_size * 1.5f);
            if (g_fm_scroll_y < 0.0f) g_fm_scroll_y = 0.0f;
        }
    }

    // Scroll bar
    if (max_scroll > 0) {
        float thumb_h = (view_h / total_content_h) * view_h;
        if (thumb_h < 14.0f) thumb_h = 14.0f;

        float thumb_y = content_start_y + (g_fm_scroll_y / max_scroll) * (view_h - thumb_h);
        Rectangle scrollbar_rect = {(float)(fm_x + fm_w - 6), thumb_y, 4.0f, thumb_h};

        DrawRectangleRounded(scrollbar_rect, 0.5f, 4, g_theme.border);
    }

    // Kalau File Manager aktif kasih Border menyala
    if (bufmgr->mode == FILE_MANAGER) {
        Rectangle fm_rect = {(float)fm_x, (float)fm_y, (float)fm_w, (float)fm_h - DIAG_PANEL_H};
        DrawRectangleLinesEx(fm_rect, 1.5f, g_theme.cursor);
    }

    // Garis pembatas dengan Diagnostic dan Status Bar
    float ws_y = fm_y + fm_h - DIAG_PANEL_H;
    DrawLine(fm_x, (int)ws_y, fm_x + fm_w - 1, (int)ws_y, g_theme.line_num);
}
