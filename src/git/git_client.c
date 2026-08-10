#include "git_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GitStatus git = {0};
float GitStatus_timer = 0.0f;

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

static bool path_under_dir(const char *file_path, const char *dir_path) {
    if (!file_path || !dir_path) return false;

    // Jika dir_path persis prefix dari file_path (Absolut vs Absolut)
    size_t dlen = strlen(dir_path);
    if (strncmp(file_path, dir_path, dlen) == 0) {
        if (file_path[dlen] == '/' || file_path[dlen] == '\0') return true;
    }

    // Jika file_path relatif (misal "src/main.c") dan dir_path absolut (misal
    // "/home/user/project/src")
    const char *folder_name = strrchr(dir_path, '/');
    folder_name = folder_name ? folder_name + 1 : dir_path;

    size_t fn_len = strlen(folder_name);
    if (strncmp(file_path, folder_name, fn_len) == 0 && file_path[fn_len] == '/') {
        return true;
    }

    // Cek apakah folder_name ada di dalam baris path relatif git (misal "sub/src/main.c")
    char needle[300];
    snprintf(needle, sizeof(needle), "/%s/", folder_name);
    if (strstr(file_path, needle) != NULL) {
        return true;
    }

    return false;
}

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

const char *Git_folder_mark(const char *dir_path) {
    if (!dir_path || !git.is_repo) return "";

    bool has_mod = false, has_new = false, has_untracked = false;

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
        }
    }

    if (has_mod) return "~";
    if (has_new) return "+";
    if (has_untracked) return "?";
    return "";
}

/**
 * Fungsi untuk Refresh UI Git
 */
void GitStatus_update(BufManager *bufmgr, float dt) {
    GitStatus_timer -= dt;
    if (GitStatus_timer > 0) return;

    GitStatus_timer = 2.0f;
    GitStatus_refresh(bufmgr->path_root, &git);
}

/**
 * Fungsi untuk force Update
 */
void GitStatus_force(void) { GitStatus_timer = 0.0f; }