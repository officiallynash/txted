/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "git_client.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "notification.h"

GitStatus git = {0};
GitPopup git_popup = {0};
float GitStatus_timer = 0.0f;
static bool g_push_in_progress = false;
static bool g_push_success = false;
static bool prev_push_state = false;

/**
 * Fungsi untuk menjalankan git CMD khusus info [PRIVATE API]
 */
static bool run_git(const char *repo, const char *args, char *out, size_t out_sz) {
    char cmd[1024];
    if (repo && repo[0]) {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>/dev/null", repo, args);
    } else {
        snprintf(cmd, sizeof(cmd), "git %s 2>/dev/null", args);
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    size_t n = fread(out, 1, out_sz - 1, fp);
    out[n] = '\0';  // Null Terminator
    int rc = pclose(fp);
    return rc == 0;
}

/**
 * Fungsi untuk mengeksekusi CMD Git (Push, Stage) [PRIVATE API]
 */
static int run_git_rc(const char *repo, const char *args, char *err, size_t err_sz) {
    char cmd[1024];
    if (repo && repo[0]) {
        snprintf(cmd, sizeof(cmd), "git -C \"%s\" %s 2>&1", repo, args);
    } else {
        snprintf(cmd, sizeof(cmd), "git %s 2>&1", args);
    }

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char buf[1024] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';  // Null terminator
    int rc = pclose(fp);

    if (err && err_sz) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(err, buf, err_sz - 1);
    }
    return rc;
}

/**
 * Fungsi untuk Stage [PRIVATE API]
 */
bool GitPopup_stage(const char *repo) {
    char err[256] = {0};
    int rc = run_git_rc(repo, "add -A", err, sizeof(err));
    if (rc != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "%s",
                 err[0] ? err : "Stage failed!");
        return false;
    }
    git_popup.last_error[0] = '\0';
    GitStatus_force();
    return true;
}

/**
 * Fungsi untuk Commit ke Git [PRIVATE API]
 */
bool GitPopup_commit(const char *repo, const char *message) {
    if (!message || !message[0]) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "empty message");
        return false;
    }

    char args[512];
    char msg[240];
    strncpy(msg, message, sizeof(msg) - 1);
    for (char *p = msg; *p; p++)
        if (*p == '"') *p = '\'';

    snprintf(args, sizeof(args), "commit -m \"%s\"", msg);
    char err[256] = {0};
    int rc = run_git_rc(repo, args, err, sizeof(err));
    if (rc != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "%s",
                 err[0] ? err : "Commit failed!");
        return false;
    }
    git_popup.last_error[0] = '\0';
    git_popup.message[0] = '\0';
    GitStatus_force();
    return true;
}

/**
 * Fungsi untuk Push ke Git [PRIVATE API]
 */
bool GitPopup_push(const char *repo) {
    char err[256] = {0};
    int rc = run_git_rc(repo, "push", err, sizeof(err));
    if (rc != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "%s",
                 err[0] ? err : "Push failed!");
        return false;
    }

    git_popup.last_error[0] = '\0';
    return true;
}

/**
 * Fungsi worker untuk Git Push (Multi Thread) [PRIVATE API]
 */
static void *git_push_worker(void *arg) {
    char *repo = (char *)arg;

    // Jalankan push (blocking cuma terjadi di background thread ini)
    bool ok = GitPopup_push(repo);

    g_push_success = ok;
    g_push_in_progress = false;  // Flag penanda selesai
    free(repo);
    return NULL;
}

/**
 * Fungsi untuk memanggil Push secara Async [PRIVATE API]
 */
void GitPopup_push_async(const char *repo) {
    if (g_push_in_progress) return;  // Mencegah spam klik tombol push

    g_push_in_progress = true;

    pthread_t thread;
    char *repo_copy = strdup(repo);
    pthread_create(&thread, NULL, git_push_worker, repo_copy);
    pthread_detach(thread);  // Detach agar memori thread otomatis bersih saat selesai
}

/**
 * Fungsi penanda Push [PRIVATE API]
 */
bool GitPopup_is_pushing(void) { return g_push_in_progress; }

/**
 * Fungsi internal untuk cek file recursive di dalam folder [PRIVATE API]
 */
static bool path_under_dir(const char *file_path, const char *dir_path) {
    if (!file_path || !dir_path) return false;

    size_t dlen = strlen(dir_path);
    size_t flen = strlen(file_path);

    if (dlen > 0 && dir_path[dlen - 1] == '/') dlen--;

    // Jika dir_path relatif (misal dir_path = "src", file_path = "src/main.c")
    if (strncmp(file_path, dir_path, dlen) == 0) {
        if (file_path[dlen] == '/' || file_path[dlen] == '\0') return true;
    }

    // Jika file_path persis sama dengan akhiran dir_path (misal file_path = "include")
    if (dlen > flen && dir_path[dlen - flen - 1] == '/' &&
        strcmp(dir_path + (dlen - flen), file_path) == 0) {
        return true;
    }

    // Cocokkan prefix hirarki folder relatif file_path dengan akhiran dir_path absolut
    for (size_t i = 0; i < flen; i++) {
        if (file_path[i] == '/') {
            if (dlen > i && dir_path[dlen - i - 1] == '/') {
                if (strncmp(dir_path + (dlen - i), file_path, i) == 0) {
                    return true;
                }
            }
        }
    }

    return false;
}

/**
 * Fungsi untuk refresh Git Status [PUBLIC API]
 */
bool GitStatus_refresh(const char *repo, GitStatus *git) {
    if (!git) return false;
    memset(git, 0, sizeof(*git));

    char buf[8192];

    if (!run_git(repo, "rev-parse --is-inside-work-tree", buf, sizeof(buf))) {
        git->is_repo = false;
        return false;
    }
    if (strncmp(buf, "true", 4) != 0) {
        git->is_repo = false;
        return false;
    }
    git->is_repo = true;

    // Branch
    if (run_git(repo, "branch --show-current", buf, sizeof(buf))) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(git->branch, buf, sizeof(git->branch) - 1);
    }
    if (git->branch[0] == '\0') snprintf(git->branch, sizeof(git->branch), "HEAD");

    // Author
    if (run_git(repo, "config user.name", buf, sizeof(buf))) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(git->author, buf, sizeof(git->author) - 1);
    }

    // Porcelain
    if (run_git(repo, "status --porcelain --ignored", buf, sizeof(buf))) {
        char *line = buf;
        while (*line) {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';

            if (strlen(line) >= 3) {
                char x = line[0];
                char y = line[1];
                const char *path = line + 3;

                // rename: "R  old -> new"
                const char *arrow = strstr(path, " -> ");
                if (arrow) path = arrow + 4;

                const char *mark = "";
                if (x == '!' || y == '!') {  // <--- TANDA IGNORED DARI GIT
                    mark = "x";
                } else if (x == '?' && y == '?') {
                    mark = "?";
                    git->untracked++;
                    git->has_changes = true;
                } else if (x == 'D' || y == 'D') {
                    mark = "-";
                    git->modified++;
                    git->has_changes = true;
                } else if (x == 'A' && y == ' ') {
                    mark = "+";
                    git->modified++;
                    git->has_changes = true;
                } else if (x != ' ' || y != ' ') {
                    mark = "~";
                    git->modified++;
                    git->has_changes = true;
                }

                if (mark[0] && git->file_count < MAX_FILE_GIT) {
                    GitFileStatus *f = &git->files[git->file_count++];
                    strncpy(f->path, path, sizeof(f->path) - 1);

                    size_t len = strlen(f->path);
                    if (len > 0 && f->path[len - 1] == '/') {
                        f->path[len - 1] = '\0';
                    }

                    strncpy(f->mark, mark, sizeof(f->mark) - 1);
                }
            }

            if (!end) break;
            line = end + 1;
        }
    }

    return true;
}

/**
 * Fungsi untuk membaca mark per file [PUBLIC API]
 */
const char *Git_file_mark(const char *abs_path) {
    if (!abs_path || !git.is_repo) return "";

    for (int i = 0; i < git.file_count; i++) {
        const char *fp = git.files[i].path;
        size_t fl = strlen(fp);
        size_t al = strlen(abs_path);

        // match exact atau suffix (abs vs relatif)
        if (strcmp(abs_path, fp) == 0) return git.files[i].mark;
        if (al >= fl && abs_path[al - fl - (al > fl ? 1 : 0)] == '/' &&
            strcmp(abs_path + (al - fl), fp) == 0)
            return git.files[i].mark;
        if (al >= fl && strcmp(abs_path + (al - fl), fp) == 0) return git.files[i].mark;
    }
    return "";
}

/**
 * Fungsi untuk membaca Folder mark [PUBLIC API]
 */
const char *Git_folder_mark(const char *dir_path) {
    if (!dir_path || !git.is_repo) return "";

    bool has_mod = false, has_new = false, has_untracked = false, has_ignore = false;

    for (int i = 0; i < git.file_count; i++) {
        const char *fp = git.files[i].path;
        // match relatif / absolut: fp dimulai dengan dir, lalu '/'
        // sederhananya: strstr / suffix logic yang sama dengan Git_file_mark

        // Contoh kasar: path file mengandung nama folder sebagai prefix
        // Lebih aman: bandingkan rel path
        if (path_under_dir(fp, dir_path)) {
            char m = git.files[i].mark[0];
            if (m == '~' || m == '-')
                has_mod = true;
            else if (m == '+')
                has_new = true;
            else if (m == '?')
                has_untracked = true;
            else if (m == 'x')
                has_ignore = true;
        }
    }

    if (has_mod) return "~";
    if (has_new) return "+";
    if (has_untracked) return "?";
    if (has_ignore) return "x";
    return "";
}

/**
 * Fungsi untuk Refresh UI Git
 */
void GitStatus_update(BufManager *bufmgr, float dt) {
    if (prev_push_state && !g_push_in_progress) {
        if (g_push_success) {
            Notif_show("Push ke remote berhasil!", NOTIF_SUCCESS, 3.0f);
        } else {
            const char *err = git_popup.last_error[0] ? git_popup.last_error : "Push gagal!";
            Notif_show(err, NOTIF_ERROR, 4.0f);
        }
        GitStatus_force();  // Langsung trigger refresh status Git seketika
    }
    prev_push_state = g_push_in_progress;

    GitStatus_timer -= dt;
    if (GitStatus_timer > 0) return;

    GitStatus_timer = 2.0f;
    GitStatus_refresh(bufmgr->path_root, &git);
}

/**
 * Helper untuk format waktu (Yang lalu)
 */
void format_time_ago(const char *author, double last_edited, char *out_str, size_t max_len) {
    double diff = difftime(time(NULL), (time_t)last_edited);
    if (diff < 0) diff = 0;

    // Hirarki nama: Param -> Environment OS -> "You"
    const char *name = author;
    if (!name || name[0] == '\0') name = getenv("USER");      // Linux / macOS
    if (!name || name[0] == '\0') name = getenv("USERNAME");  // Windows
    if (!name || name[0] == '\0') name = "You";

    if (diff < 60) {
        snprintf(out_str, max_len, "@%s, just now", name);
    } else if (diff < 3600) {
        snprintf(out_str, max_len, "@%s, %dm ago", name, (int)(diff / 60));
    } else if (diff < 86400) {
        snprintf(out_str, max_len, "@%s, %dh ago", name, (int)(diff / 3600));
    } else {
        snprintf(out_str, max_len, "@%s, %dd ago", name, (int)(diff / 86400));
    }
}

/**
 * Fungsi untuk force Update
 */
void GitStatus_force(void) { GitStatus_timer = 0.0f; }

/**
 * Mengambil status diff per baris dari Git (-U0) dan memperbarui line metadata buffer
 */
void Git_fetch_file_diff(const char *repo_path, const char *file_path, Buffer *buf) {
    if (!repo_path || !file_path || !buf || !buf->line_git) return;

    // Command diff tanpa context line (-U0)
    char args[512];
    snprintf(args, sizeof(args), "diff -U0 -- \"%s\"", file_path);

    char output[16384] = {0};
    if (!run_git(repo_path, args, output, sizeof(output))) return;

    // Reset status gutter lama di buffer
    for (size_t i = 0; i < buf->meta_capacity; i++) {
        buf->line_git[i].status = GUTTER_NONE;
    }

    char *line = output;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        // Cari header diff, contoh: @@ -10,3 +12,5 @@ atau @@ -5 +4,0 @@
        if (strncmp(line, "@@ ", 3) == 0) {
            int old_start = 0, old_count = 1;
            int new_start = 0, new_count = 1;

            char *minus = strchr(line, '-');
            char *plus = strchr(line, '+');

            if (minus) {
                if (sscanf(minus, "-%d,%d", &old_start, &old_count) < 2) {
                    sscanf(minus, "-%d", &old_start);
                }
            }

            if (plus) {
                if (sscanf(plus, "+%d,%d", &new_start, &new_count) < 2) {
                    sscanf(plus, "+%d", &new_start);
                }

                // CASE A: Penambahan (ADDED) atau Perubahan (MODIFIED)
                if (new_count > 0 && new_start > 0) {
                    size_t start_idx = (size_t)new_start - 1;  // Convert ke 0-based index
                    GutterStatus status = (old_count == 0) ? GUTTER_ADDED : GUTTER_MODIFIED;

                    for (int i = 0; i < new_count; i++) {
                        size_t target_line = start_idx + i;

                        if (target_line < buf->lines.line_count &&
                            target_line < buf->meta_capacity) {
                            buf->line_git[target_line].status = status;

                            if (buf->line_git[target_line].last_edited_at == 0) {
                                buf->line_git[target_line].last_edited_at = (double)time(NULL);
                            }
                        }
                    }
                }
                // CASE B: Penghapusan Murni (DELETED) - new_count == 0
                else if (new_count == 0 && old_count > 0) {
                    size_t target_line = (new_start > 0) ? (size_t)(new_start - 1) : 0;

                    if (target_line < buf->lines.line_count && target_line < buf->meta_capacity) {
                        // Hanya tandai GUTTER_DELETED jika belum ada status ADDED/MODIFIED
                        if (buf->line_git[target_line].status == GUTTER_NONE) {
                            buf->line_git[target_line].status = GUTTER_DELETED;
                        }

                        if (buf->line_git[target_line].last_edited_at == 0) {
                            buf->line_git[target_line].last_edited_at = (double)time(NULL);
                        }
                    }
                }
            }
        }

        if (!next) break;
        line = next + 1;
    }
}

/**
 * Mengambil metadata Author & Timestamp commit terakhir per baris via Git Blame
 */
void Git_fetch_file_blame(const char *repo_path, const char *file_path, Buffer *buf) {
    if (!repo_path || !file_path || !buf) return;

    // Command git blame dengan format porcelain (machine-readable)
    char args[512];
    snprintf(args, sizeof(args), "blame --porcelain -- \"%s\"", file_path);

    char output[65536] = {0};  // Penampung output blame
    if (!run_git(repo_path, args, output, sizeof(output))) return;

    char current_author[64] = {0};
    double current_time = 0;
    size_t current_line = 0;

    char *line = output;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        // Baca baris headercommit: <hash> <orig_line> <final_line>
        if (strlen(line) >= 40 && line[40] == ' ') {
            int orig_l, final_l;
            if (sscanf(line + 41, "%d %d", &orig_l, &final_l) >= 2) {
                current_line = (size_t)final_l - 1;  // Convert ke 0-based index
            }
        }
        // Extract nama author
        else if (strncmp(line, "author ", 7) == 0) {
            strncpy(current_author, line + 7, sizeof(current_author) - 1);
        }
        // Extract waktu commit
        else if (strncmp(line, "author-time ", 12) == 0) {
            current_time = (double)atof(line + 12);
        }
        // Baris teks asli diawali karakter TAB -> Tanda akhir blok metadata baris ini
        else if (line[0] == '\t') {
            if (current_line < buf->lines.line_count && current_line < buf->meta_capacity) {
                // Simpan metadata ke line_git/line_meta
                strncpy(buf->line_git[current_line].author, current_author,
                        sizeof(buf->line_git[current_line].author) - 1);

                // Cuma update timestamp jika baris belum diubah lokal di editor
                if (buf->line_git[current_line].last_edited_at == 0) {
                    buf->line_git[current_line].last_edited_at = current_time;
                }
            }
        }

        if (!next) break;
        line = next + 1;
    }
}
