/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "fs.h"

#include <dirent.h>
#include <libgen.h>
#include <raygui.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "result.h"

/**
 * Fungsi untuk membuat Folder Recursive [PRIVATE API]
 */
static int Ensure_dir_exists(const char *file_path) {
    char path_copy[1024] = {0};
    snprintf(path_copy, sizeof(path_copy), "%s", file_path);

    char *dir_path = dirname(path_copy);
    if (strcmp(dir_path, ".") == 0 || strcmp(dir_path, "/") == 0) return 0;

    char tmp[1024] = {0};
    char *p = nullptr;

    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    // Bikin folder-folder secara recursive
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }

    return mkdir(tmp, 0755);
}

/**
 * Fungsi untuk memperpendek path /home/user/... jadi ~/ ...
 */
char *format_pretty_path(const char *path) {
    if (!path) return strdup("Untilted");

    // Ambil HOME directory dari Environment Variable sistem ($HOME)
    const char *home = getenv("HOME");

    if (home) {
        size_t home_len = strlen(home);

        // Cek apakah path diawali dengan string $HOME
        if (strncmp(path, home, home_len) == 0) {
            size_t path_len = strlen(path);
            size_t new_len = 1 + (path_len - home_len) + 1;  // 1 untuk '~' + sisa path + '\0'

            char *pretty = calloc(new_len, sizeof(char));
            if (pretty) {
                snprintf(pretty, new_len, "~%s", path + home_len);
                return pretty;
            }
        }
    }

    // Jika tidak di dalam folder $HOME, kembalikan copy path aslinya
    return strdup(path);
}

/**
 * Fungsi untuk menambah item ke FileList [PRIVATE API]
 */
static void FileList_add(FileList *list, const char *path) {
    if (list->item_count >= list->capacity) {
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity * sizeof(PromptItem));
        if (!list->items) return;
    }

    snprintf(list->items[list->item_count].label, sizeof(list->items[list->item_count].label), "%s",
             path);
    snprintf(list->items[list->item_count].subtext, sizeof(list->items[list->item_count].subtext),
             "File");
    list->items[list->item_count].icon_id = ICON_FILETYPE_TEXT;
    list->item_count++;
}

/**
 * Helper internal untuk cek keberadaan file/folder [PRIVATE API]
 */
static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/**
 * Fungsi untuk mengambil Directory name
 */
char *Fs_dirname(const char *path) {
    if (!path) return nullptr;
    char *copy = strdup(path);
    if (!copy) return nullptr;

    char *slash = strrchr(copy, '/');
    if (!slash) {
        free(copy);
        return strdup(".");
    }
    if (slash == copy) {
        // root "/"
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return copy;  // caller free
}

/**
 * Fungsi untuk mengambil Full path [PUBLIC API]
 */
char *Get_full_path(const char *filename) {
    if (!filename || filename[0] == '\0') return nullptr;

    char resolved[1024] = {0};
    // Coba dapatkan realpath langsung
    if (realpath(filename, resolved) != nullptr) {
        return strdup(resolved);
    }

    // Jika sudah absolute path tapi file belum ada di disk
    if (filename[0] == '/') {
        return strdup(filename);
    }

    // Jika relatif dan belum ada di disk (fallback gabung CWD)
    char cwd[1024] = {0};
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        char full[2048] = {0};
        snprintf(full, sizeof(full), "%s/%s", cwd, filename);

        // Coba realpath sekali lagi setelah digabung CWD
        if (realpath(full, resolved) != nullptr) {
            return strdup(resolved);
        }
        return strdup(full);
    }

    return strdup(filename);
}

/**
 * Fungsi untuk membuka file [PUBLIC API]
 */
Result Fs_open(const char *filename) {
    char *full_path = Get_full_path(filename);  // Ambil fullpath dari file
    if (!full_path) {
        return Err("Path tidak di temukan!");
    }

    FILE *file = fopen(full_path, "r");  // Buka file
    // Jika gagal maka keluarkan error sementara, kalau udah ada gui akan todo()
    if (!file) {
        free(full_path);
        return Err("File tidak ditemukan!");
    }

    // hitung fsz atau file size
    fseek(file, 0, SEEK_END);
    long fsz = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Sebenarnya ada cara yang lebih cepat yaitu
    // Menggunakan temp buffer lalu pakai while fgets
    // ya tapi juga ada resiko. maka akan lebih bijak pakai calloc
    // dengan kombinasi Result atau error handling
    // Buat buffer file disini pakai u8, kalau di Rust itu Vec<u8>

    unsigned char *buffer = calloc(fsz + 1, sizeof(unsigned char));
    if (!buffer) [[clang::unlikely]] {
        return Err("Gagal alokasi Buffer!");
    }

    size_t got = fread(buffer, 1, fsz, file);  // Baca file ke Buffer
    buffer[got] = '\0';                        // Null terminator

    fclose(file);                                  // Tutup file agar tidak corrupt
    FileData *data = calloc(1, sizeof(FileData));  // Persiapan struct untuk penampung hasil

    data->data = buffer;
    data->full_path = full_path;  // Transfer ownership, free nanti di buffer
    data->size = got;
    data->is_success = true;

    return Ok(data);
}

/**
 * Fungsi untuk save file dari Buffer [PUBLIC API]
 */
Result Fs_savefile(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "w");
    if (!f) return Err("Gagal menyimpan File!");
    fwrite(data, 1, len, f);
    fclose(f);

    const char *msg = "Berhasil disimpan!";
    return Ok((void *)msg);
}

/**
 * Fungsi untuk membuat file baru [PUBLIC API]
 */
Result Fs_create(const char *filename) {
    Ensure_dir_exists(filename);
    FILE *f = fopen(filename, "w");
    if (!f) return Err("Gagal membuat File");
    fclose(f);

    char *full_path = Get_full_path(filename);
    file_manager_refresh();
    return Ok(full_path);
}

/**
 * Fungsi untuk menghapus metadata [PUBLIC API]
 */
void Fs_metadata_free(FileData *fm) {
    if (!fm) return;
    if (fm->data != nullptr) free(fm->data);

    fm->size = 0;
    if (fm != nullptr) free(fm);
}

/**
 * Fungsi untuk Membuat FIleList [PUBLIC API]
 */
FileList *FileList_init(size_t capacity) {
    FileList *list = calloc(1, sizeof(FileList));
    if (!list) return nullptr;

    list->items = calloc(capacity, sizeof(PromptItem));
    if (!list->items) {
        free(list);
        return nullptr;
    }

    list->item_count = 0;
    list->capacity = (capacity > 0) ? capacity : 128;
    return list;
}

/**
 * Fungsi untuk Free FileList [PUBLIC API]
 */
void FileList_free(FileList *list) {
    if (!list) return;
    if (list->items != nullptr) free(list->items);
    free(list);
}

/**
 * Fungsi untuk memindai file dalam project [PUBLIC API]
 */
void Scan_project_files(const char *base_path, FileList *list) {
    char path[1024] = {0};
    struct dirent *dp;
    DIR *dir = opendir(base_path);

    if (!dir) return;

    while ((dp = readdir(dir)) != nullptr) {
        // Abaikan "." (current dir) dan ".." (parent dir)
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        // Filter folder tersembunyi / build artifacts biar gak memenuhi RAM
        if (dp->d_name[0] == '.' || strcmp(dp->d_name, "build") == 0 ||
            strcmp(dp->d_name, "node_modules") == 0) {
            continue;
        }

        // Gabungkan Path (Tanpa 'cd'!)
        if (strcmp(base_path, ".") == 0) {
            snprintf(path, sizeof(path), "%s", dp->d_name);
        } else {
            snprintf(path, sizeof(path), "%s/%s", base_path, dp->d_name);
        }

        // Cek jenis (Folder vs File)
        struct stat statbuf;
        if (stat(path, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                // Rekursi masuk ke sub-folder
                Scan_project_files(path, list);
            } else {
                // Tambahkan file ke FileList
                FileList_add(list, path);
            }
        }
    }

    closedir(dir);  // Tutup direktori setelah selesai
}

/**
 * Mencari Root Directory dari path file yang sedang dibuka [PUBLIC API]
 */
char *Fs_find_project_root(const char *filepath) {
    if (!filepath) {
        // Fallback jika filepath NULL, ambil CWD (Current Working Directory)
        char *cwd = getcwd(nullptr, 0);
        return cwd ? cwd : strdup("/");
    }

    // Dapatkan Full Absolute Path terlebih dahulu
    char *full_path = Get_full_path(filepath);
    if (!full_path) {
        char *cwd = getcwd(nullptr, 0);
        return cwd ? cwd : strdup("/");
    }

    char current_dir[1024] = {0};

    // Cek apakah ini file atau folder
    struct stat st;
    if (stat(full_path, &st) == 0 && !S_ISDIR(st.st_mode)) {
        // Kalau file, ambil folder induknya
        char *path_copy = strdup(full_path);
        char *dir = dirname(path_copy);
        snprintf(current_dir, sizeof(current_dir), "%s", dir);
        free(path_copy);
    } else {
        snprintf(current_dir, sizeof(current_dir), "%s", full_path);
    }
    free(full_path);  // Free memory temporary full_path

    // Marker penanda root project
    const char *markers[] = {"Makefile",   ".git",         "CMakeLists.txt",
                             "Cargo.toml", "package.json", "compile_commands.json",
                             "build.zig",  "build.zig.zon"};
    size_t num_markers = sizeof(markers) / sizeof(markers[0]);

    char check_path[1024] = {0};
    char temp_dir[1024] = {0};
    snprintf(temp_dir, sizeof(temp_dir), "%s", current_dir);

    // Panjat direktori ke atas
    while (strlen(temp_dir) > 1 && strcmp(temp_dir, "/") != 0) {
        // Cek marker
        for (size_t i = 0; i < num_markers; i++) {
            snprintf(check_path, sizeof(check_path), "%s/%s", temp_dir, markers[i]);
            if (file_exists(check_path)) {
                return strdup(temp_dir);  // Ketemu Root!
            }
        }

        // Naik satu tingkat ke folder parent
        char parent[1024] = {0};
        snprintf(parent, sizeof(parent), "%s/..", temp_dir);

        char resolved[1024] = {0};
        if (realpath(parent, resolved) != nullptr) {
            if (strcmp(temp_dir, resolved) == 0) break;  // Sampai di paling atas (/)
            snprintf(temp_dir, sizeof(temp_dir), "%s", resolved);
        } else {
            break;
        }
    }

    // Fallback: Jika tidak ada marker project, gunakan folder file itu sendiri
    return strdup(current_dir);
}
