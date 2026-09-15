#include "git2/credential.h"
/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include <git2.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "git2/global.h"
#include "git_client.h"
#include "notification.h"

GitStatus git = {};
GitPopup git_popup = {};
float GitStatus_timer = 0.0f;
static _Atomic bool g_push_in_progress = false;
static _Atomic int g_push_result = 0;

typedef struct {
    char *repo_path;
    char *file_path;
    Buffer *buf;
} GitFetchArgs;

/**
 * Fungsi untuk Stage [PRIVATE API]
 */
bool GitPopup_stage(const char *repo_path) {
    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Gagal membuka repository!");
        return false;
    }

    git_index *index = nullptr;
    if (git_repository_index(&index, repo) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Gagal membaca Git index!");
        git_repository_free(repo);
        return false;
    }

    // Setara dengan git add -A (Stage semua perubahan workdir)
    git_strarray pathspec = {0};
    if (git_index_add_all(index, &pathspec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr) != 0 ||
        git_index_write(index) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Stage failed!");
        git_index_free(index);
        git_repository_free(repo);
        return false;
    }

    git_index_free(index);
    git_repository_free(repo);

    git_popup.last_error[0] = '\0';
    GitStatus_force();
    return true;
}

/**
 * Fungsi untuk Commit ke Git [PRIVATE API]
 */
bool GitPopup_commit(const char *repo_path, const char *message) {
    if (!message || !message[0]) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "empty message");
        return false;
    }

    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Gagal membuka repository!");
        return false;
    }

    // Tulis Index ke Tree
    git_index *index = nullptr;
    git_oid tree_id, commit_id;
    git_tree *tree = nullptr;

    if (git_repository_index(&index, repo) != 0 || git_index_write_tree(&tree_id, index) != 0 ||
        git_tree_lookup(&tree, repo, &tree_id) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error),
                 "Commit failed (Index/Tree error)!");
        if (index) git_index_free(index);
        git_repository_free(repo);
        return false;
    }
    git_index_free(index);

    // Ambil Signature Author & Committer (dari config)
    git_signature *author = nullptr;
    if (git_signature_default(&author, repo) != 0) {
        // Fallback jika user.name / user.email belum di-set di config
        git_signature_now(&author, "TxtEd User", "user@txted.local");
    }

    // Ambil Parent Commit (HEAD) jika ada
    git_commit *parent = nullptr;
    git_oid parent_id;
    int parents_count = 0;
    const git_commit *parents[1];

    if (git_reference_name_to_id(&parent_id, repo, "HEAD") == 0) {
        if (git_commit_lookup(&parent, repo, &parent_id) == 0) {
            parents[0] = parent;
            parents_count = 1;
        }
    }

    // Buat Commit Baru
    int rc = git_commit_create(&commit_id, repo, "HEAD", author, author, nullptr, message, tree,
                               parents_count, parents);

    git_signature_free(author);
    git_tree_free(tree);
    if (parent) git_commit_free(parent);
    git_repository_free(repo);

    if (rc != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Commit failed!");
        return false;
    }

    git_popup.last_error[0] = '\0';
    git_popup.message[0] = '\0';
    GitStatus_force();
    return true;
}

/**
 * Fungsi helper untuk credentiial cb [PRIVATE API]
 */
static int credential_cb(git_credential **out, const char *url, const char *username_from_url,
                         unsigned int allowed_types, void *payload) {
    (void)url, (void)payload;

    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        return git_credential_ssh_key_from_agent(out, username_from_url);
    }

    return git_credential_default_new(out);
}

/**
 * Fungsi untuk Push ke Git [PRIVATE API]
 */
bool GitPopup_push(const char *repo_path) {
    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Gagal membuka repository!");
        return false;
    }

    git_remote *remote = nullptr;
    // Cari remote default ("origin")
    if (git_remote_lookup(&remote, repo, "origin") != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error),
                 "Remote 'origin' tidak ditemukan!");
        git_repository_free(repo);
        return false;
    }

    // Dapatkan nama branch aktif saat ini
    git_reference *head = nullptr;
    char refspec_str[256] = {0};
    if (git_repository_head(&head, repo) == 0 && git_reference_is_branch(head)) {
        const char *branch = nullptr;
        git_branch_name(&branch, head);
        snprintf(refspec_str, sizeof(refspec_str), "refs/heads/%s:refs/heads/%s", branch, branch);
        git_reference_free(head);
    } else {
        snprintf(refspec_str, sizeof(refspec_str), "refs/heads/main:refs/heads/main");
    }

    git_strarray refspecs = {.strings = (char *[]){refspec_str}, .count = 1};

    git_push_options push_opts = GIT_PUSH_OPTIONS_INIT;
    push_opts.callbacks.credentials = credential_cb;

    int rc = git_remote_push(remote, &refspecs, &push_opts);

    git_remote_free(remote);
    git_repository_free(repo);

    if (rc != 0) {
        snprintf(git_popup.last_error, sizeof(git_popup.last_error), "Push failed!");
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

    g_push_result = ok ? 1 : -1;
    g_push_in_progress = false;  // Flag penanda selesai
    free(repo);
    return nullptr;
}

/**
 * Fungsi untuk memanggil Push secara Async [PRIVATE API]
 */
void GitPopup_push_async(const char *repo) {
    if (g_push_in_progress) return;  // Mencegah spam klik tombol push

    g_push_in_progress = true;
    g_push_result = 0;

    pthread_t thread;
    char *repo_copy = strdup(repo);
    pthread_create(&thread, nullptr, git_push_worker, repo_copy);
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

// Callback internal libgit2 saat menemukan Hunk (perubahan baris)
static int diff_hunk_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, void *payload) {
    Buffer *buf = (Buffer *)payload;
    (void)delta;

    // hunk->new_start adalah nomor baris 1-based di file baru (Workdir)
    // hunk->new_lines adalah berapa baris yang ditambahkan/diubah
    // hunk->old_lines adalah berapa baris lama yang dihapus

    if (hunk->new_lines > 0 && hunk->new_start > 0) {
        size_t start_idx = (size_t)hunk->new_start - 1;  // Convert ke 0-based
        GutterStatus status = (hunk->old_lines == 0) ? GUTTER_ADDED : GUTTER_MODIFIED;

        for (int i = 0; i < hunk->new_lines; i++) {
            size_t target_line = start_idx + i;
            if (target_line < buf->lines.line_count && target_line < buf->meta_capacity) {
                buf->line_git[target_line].status = status;
                if (buf->line_git[target_line].last_edited_at == 0) {
                    buf->line_git[target_line].last_edited_at = (double)time(nullptr);
                }
            }
        }
    } else if (hunk->new_lines == 0 && hunk->old_lines > 0) {
        size_t target_line = (hunk->new_start > 0) ? (size_t)(hunk->new_start - 1) : 0;
        if (target_line < buf->lines.line_count && target_line < buf->meta_capacity) {
            if (buf->line_git[target_line].status == GUTTER_NONE) {
                buf->line_git[target_line].status = GUTTER_DELETED;
            }
        }
    }

    return 0;  // Return 0 untuk terus membaca diff berikutnya
}

/**
 * Fungsi untuk refresh Git Status [PUBLIC API]
 */
bool GitStatus_refresh(const char *repo_path, GitStatus *git_out) {
    if (!repo_path || !git_out) return false;
    memset(git_out, 0, sizeof(*git_out));

    git_repository *repo = nullptr;

    // Open Repository (Otomatis nyari folder .git ke atas jika repo_path adalah sub-folder)
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) {
        git_out->is_repo = false;
        return false;
    }
    git_out->is_repo = true;

    // Ambil Nama Branch Aktif
    git_reference *head = nullptr;
    if (git_repository_head(&head, repo) == 0) {
        if (git_reference_is_branch(head)) {
            const char *branch_name = nullptr;
            git_branch_name(&branch_name, head);
            if (branch_name) {
                strncpy(git_out->branch, branch_name, sizeof(git_out->branch) - 1);
            }
        }
        git_reference_free(head);
    }
    if (git_out->branch[0] == '\0') {
        strncpy(git_out->branch, "HEAD", sizeof(git_out->branch) - 1);
    }

    // Ambil Author dari Config (.git/config atau global)
    git_config *cfg = nullptr;
    if (git_repository_config(&cfg, repo) == 0) {
        const char *user_name = nullptr;
        if (git_config_get_string(&user_name, cfg, "user.name") == 0 && user_name) {
            strncpy(git_out->author, user_name, sizeof(git_out->author) - 1);
        }
        git_config_free(cfg);
    }

    // Status File (Porcelain Replacement)
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS |
                 GIT_STATUS_OPT_INCLUDE_IGNORED;

    git_status_list *status_list = nullptr;
    if (git_status_list_new(&status_list, repo, &opts) == 0) {
        size_t count = git_status_list_entrycount(status_list);

        for (size_t i = 0; i < count && git_out->file_count < MAX_FILE_GIT; i++) {
            const git_status_entry *entry = git_status_byindex(status_list, i);
            if (!entry) continue;

            const char *path = nullptr;
            const char *mark = "";
            unsigned int s = entry->status;

            if (s & GIT_STATUS_IGNORED) {
                mark = "x";
                path = entry->index_to_workdir
                           ? entry->index_to_workdir->old_file.path
                           : (entry->head_to_index ? entry->head_to_index->old_file.path : nullptr);
            } else if (s & GIT_STATUS_WT_NEW) {
                mark = "?";
                path = entry->index_to_workdir->old_file.path;
                git_out->untracked++;
                git_out->has_changes = true;
            } else if ((s & GIT_STATUS_WT_DELETED) || (s & GIT_STATUS_INDEX_DELETED)) {
                mark = "-";
                path = entry->index_to_workdir ? entry->index_to_workdir->old_file.path
                                               : entry->head_to_index->old_file.path;
                git_out->modified++;
                git_out->has_changes = true;
            } else if (s & GIT_STATUS_INDEX_NEW) {
                mark = "+";
                path = entry->head_to_index->new_file.path;
                git_out->modified++;
                git_out->has_changes = true;
            } else if ((s & GIT_STATUS_WT_MODIFIED) || (s & GIT_STATUS_INDEX_MODIFIED)) {
                mark = "~";
                path = entry->index_to_workdir ? entry->index_to_workdir->new_file.path
                                               : entry->head_to_index->new_file.path;
                git_out->modified++;
                git_out->has_changes = true;
            }

            if (mark[0] && path) {
                GitFileStatus *f = &git_out->files[git_out->file_count++];
                strncpy(f->path, path, sizeof(f->path) - 1);
                strncpy(f->mark, mark, sizeof(f->mark) - 1);
            }
        }
        git_status_list_free(status_list);
    }

    git_repository_free(repo);
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
    int result = g_push_result;
    if (result != 0) {
        g_push_result = 0;
        if (result == 1) {
            Notif_show("Push ke remote berhasil!", NOTIF_SUCCESS, 3.0f);
        } else {
            const char *err = git_popup.last_error[0] ? git_popup.last_error : "Push gagal!";
            Notif_show(err, NOTIF_ERROR, 4.0f);
        }
        GitStatus_force();  // Langsung trigger refresh status Git seketika
    }

    GitStatus_timer -= dt;
    if (GitStatus_timer > 0) return;

    GitStatus_timer = 2.0f;
    GitStatus_refresh(bufmgr->path_root, &git);
}

/**
 * Helper untuk format waktu (Yang lalu)
 */
void format_time_ago(const char *author, double last_edited, char *out_str, size_t max_len) {
    double diff = difftime(time(nullptr), (time_t)last_edited);
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

    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) return;

    // Reset status gutter
    for (size_t i = 0; i < buf->meta_capacity; i++) {
        buf->line_git[i].status = GUTTER_NONE;
    }

    // Ambil Commit HEAD & Tree untuk file pembanding
    git_object *head_commit = nullptr;
    git_tree *head_tree = nullptr;
    if (git_revparse_single(&head_commit, repo, "HEAD^{commit}") == 0) {
        git_commit_tree(&head_tree, (git_commit *)head_commit);
    }

    // Buat Diff antara Index/HEAD dengan Workdir (file aktif)
    git_diff_options diffopts = GIT_DIFF_OPTIONS_INIT;
    diffopts.pathspec.strings = (char **)&file_path;
    diffopts.pathspec.count = 1;
    diffopts.context_lines = 0;  // Setara dengan -U0 di CLI!

    git_diff *diff = nullptr;
    if (git_diff_tree_to_workdir_with_index(&diff, repo, head_tree, &diffopts) == 0) {
        // Run diff per hunk via callback
        git_diff_foreach(diff, nullptr, nullptr, diff_hunk_cb, nullptr, buf);
        git_diff_free(diff);
    }

    if (head_tree) git_tree_free(head_tree);
    if (head_commit) git_object_free(head_commit);
    git_repository_free(repo);
}

/**
 * Mengambil metadata Author & Timestamp commit terakhir per baris via Git Blame
 */
void Git_fetch_file_blame(const char *repo_path, const char *file_path, Buffer *buf) {
    if (!repo_path || !file_path || !buf || !buf->line_git) return;

    git_repository *repo = nullptr;
    if (git_repository_open_ext(&repo, repo_path, 0, nullptr) != 0) return;

    git_blame_options opts = GIT_BLAME_OPTIONS_INIT;
    git_blame *blame = nullptr;

    if (git_blame_file(&blame, repo, file_path, &opts) == 0) {
        uint32_t hunk_count = git_blame_get_hunk_count(blame);

        for (uint32_t i = 0; i < hunk_count; i++) {
            const git_blame_hunk *hunk = git_blame_get_hunk_byindex(blame, i);
            if (!hunk || !hunk->final_signature) continue;

            size_t start_line = hunk->final_start_line_number - 1;  // 0-based index
            size_t line_count = hunk->lines_in_hunk;

            for (size_t l = 0; l < line_count; l++) {
                size_t current_line = start_line + l;
                if (current_line < buf->lines.line_count && current_line < buf->meta_capacity) {
                    // ALWAYS copy author jika dari commit valid
                    if (hunk->final_signature->name && hunk->final_signature->name[0] != '\0') {
                        strncpy(buf->line_git[current_line].author, hunk->final_signature->name,
                                sizeof(buf->line_git[current_line].author) - 1);
                    }

                    // ALWAYS update timestamp dari commit asli
                    buf->line_git[current_line].last_edited_at =
                        (double)hunk->final_signature->when.time;
                }
            }
        }
        git_blame_free(blame);
    }

    git_repository_free(repo);
}

/**
 * Fungsi wrapper untuk Inisiasi Global init Libgit2
 */
void Git_global_init(void) { git_libgit2_init(); }
void Git_global_shutdown(void) { git_libgit2_shutdown(); }

/*
 * Worker tunggal yang mengeksekusi Diff lalu Blame
 */
static void *git_fetch_worker(void *arg) {
    GitFetchArgs *args = (GitFetchArgs *)arg;
    if (args) {
        Git_fetch_file_diff(args->repo_path, args->file_path, args->buf);
        Git_fetch_file_blame(args->repo_path, args->file_path, args->buf);

        free(args->repo_path);
        free(args->file_path);
        free(args);
    }
    return nullptr;
}

/*
 * Fungsi untuk worker async
 */
void Git_fetch_file_async(const char *repo_path, const char *file_path, Buffer *buf) {
    if (!repo_path || !file_path || !buf) return;

    // Potong repo_path dari file_path agar menjadi relative path
    const char *rel_path = file_path;
    size_t repo_len = strlen(repo_path);
    if (strncmp(file_path, repo_path, repo_len) == 0) {
        rel_path = file_path + repo_len;
        while (*rel_path == '/' || *rel_path == '\\') rel_path++;  // Skip leading slash
    }

    GitFetchArgs *args = malloc(sizeof(GitFetchArgs));
    if (!args) return;

    args->repo_path = strdup(repo_path);
    args->file_path = strdup(rel_path);  // Kirim relative path ke worker
    args->buf = buf;

    pthread_t thread;
    if (pthread_create(&thread, nullptr, git_fetch_worker, args) == 0) {
        pthread_detach(thread);
    } else {
        free(args->repo_path);
        free(args->file_path);
        free(args);
    }
}