#include "git_client.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
 * Fungsi untuk force Update
 */
void GitStatus_force(void) { GitStatus_timer = 0.0f; }
