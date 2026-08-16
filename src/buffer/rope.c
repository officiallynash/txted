/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "rope.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_SIZE_LEAF 1024

String *String_new();            // Register awal
size_t String_len(String *str);  // Register Awal

/* ================================
 * PRIVATE API
 * ================================ */

struct String {
    char *str;
    size_t len;
    size_t weight;
    size_t ref_count;

    struct String *left, *right;
};

/**
 * Fungsi untuk menambahkan ref_count [PRIVATE API]
 */
static void String_retain(String *str) {
    if (!str) return;
    str->ref_count++;
}

/**
 * Fungsi internal untuk membuat new leaf [PRIVATE API]
 */
static String *String_make_leaf(const char *text, size_t len) {
    String *new = malloc(sizeof(String));
    new->str = malloc(len + 1);
    if (!new->str) {
        free(new);
        return NULL;
    }

    memcpy(new->str, text, len);
    new->str[len] = '\0';
    new->len = len;
    new->weight = len;
    new->ref_count = 1;
    new->left = NULL;
    new->right = NULL;

    return new;
}

/**
 * Fungsi untuk menggabungkan 2 String [PRIVATE API]
 */
static String *String_concat(String *left, String *right) {
    if (!left && !right) {
        return String_new();
    }

    if (!left) {
        String_retain(right);
        return right;
    }

    if (!right) {
        String_retain(left);
        return left;
    }

    // Alokasi baru untuk parent
    String *parent = malloc(sizeof(String));
    if (!parent) return NULL;

    parent->str = NULL;
    parent->left = left;
    parent->right = right;

    // Menambahkan Ref count agar tidak terjadi memory leak
    String_retain(left);
    String_retain(right);

    parent->weight = String_len(left);                 // Weight = panjang (len) kiri
    parent->len = parent->weight + String_len(right);  // len = panjang weight + len dari kanan
    parent->ref_count = 1;                             // Default ref_count itu 1

    return parent;
}

/**
 * Fungsi untuk String Split [PRIVATE API]
 */
static void String_split(String *root, size_t index, String **left, String **right) {
    // Guard clause kalau Root itu Kosong
    if (!root) {
        *left = *right = NULL;
        return;
    }

    if (root->str) {
        // jika index kurang dari nol maka Kiri kosong
        if (index <= 0) {
            *left = NULL;
            *right = root;
            String_retain(root);
        }
        // Jika index lebih dari root len, maka kanan kosong
        else if (index >= root->len) {
            *left = root;
            *right = NULL;
            String_retain(root);
        }
        // Kalau tidak maka buat baru
        else {
            *left = String_make_leaf(root->str, index);
            *right = String_make_leaf(root->str + index, root->len - index);
        }
        return;
    }

    // Jika root atau parent itu node maka split lagi
    if (index < root->weight) {
        // Jika index lebih kecil dari weight, sudah pasti left
        String *ll, *lr;
        String_split(root->left, index, &ll, &lr);
        *left = ll;
        *right = String_concat(lr, root->right);
        String_release(lr);  // safety free
    } else {
        String *rl, *rr;
        String_split(root->right, index - root->weight, &rl, &rr);
        *left = String_concat(root->left, rl);
        *right = rr;
        String_release(rl);  // safety free
    }
}

/**
 * Helper untuk Get [PRIVATE API]
 */
static void String_collect(String *str, size_t start, size_t len, unsigned char *buffer,
                           size_t *offset) {
    // Jika str kosong atau len = 0 autp return
    if (!str || len == 0) return;

    // Jika ini leaf
    if (str->str) {
        // Proses hanya jika start kurang dari panjang
        if (start < str->len) {
            size_t bytes_to_copy = str->len - start;
            if (bytes_to_copy > len) bytes_to_copy = len;

            memcpy(buffer + *offset, str->str + start, bytes_to_copy);  // Copy memory zero cost
            *offset += bytes_to_copy;
        }

        return;
    }

    // Jika ada left dan right
    if (start < str->weight) {
        size_t left_len = str->weight - start;

        size_t copy_from_left = (left_len < len) ? left_len : len;

        String_collect(str->left, start, copy_from_left, buffer, offset);  // Recursive

        if (len > copy_from_left) {
            String_collect(str->right, 0, len - copy_from_left, buffer, offset);
        }

    } else {
        String_collect(str->right, start - str->weight, len, buffer, offset);
    }
}

/* ==========================
 * PUBLIC API
 * ========================== */

/**
 * Fungsi untuk mengetahui panjang String [PUBLIC API]
 */
size_t String_len(String *str) { return str ? str->len : 0; }

/**
 * Fungsi untuk deklarasi String [PUBLIC API]
 */
String *String_new() {
    String *new = malloc(sizeof(String));

    new->str = NULL;
    new->len = 0;
    new->weight = 0;
    new->ref_count = 1;
    new->left = new->right = NULL;
    return new;
}

/**
 * Fungsi untuk reference counting [PRIVATE API]
 */
void String_release(String *str) {
    if (!str) return;
    str->ref_count--;

    if (str->ref_count > 0) return;

    String_release(str->left);
    String_release(str->right);

    if (str->str != NULL) {
        free(str->str);
    }
    free(str);
}

/**
 * Fungsi untuk insert String [PUBLIC API]
 */
void String_insert(String **str, size_t index, const char *text, size_t len) {
    if (!text || len == 0) return;

    String *inserted = NULL;
    // Jika teks melebihi 1024 maka bagi menjadi 2
    if (len > MAX_SIZE_LEAF) {
        size_t mid = len / 2;

        // Deklarasi awal agar tidak terjadi data corrupt
        String *left_part = String_new();
        String *right_part = String_new();

        String_insert(&left_part, 0, text, mid);
        String_insert(&right_part, 0, text + mid, len - mid);

        inserted = String_concat(left_part, right_part);

        // Safe free berdasarkan ref count
        String_release(left_part);
        String_release(right_part);

    } else {
        // Jika masih di bawah 1024, maka langsung buat baru saja
        inserted = String_make_leaf(text, len);
    }

    // Jika bukan left dan right langsung assign ke Root
    if (*str && (*str)->len == 0 && (*str)->str == NULL && (*str)->left == NULL &&
        (*str)->right == NULL) {
        free(*str);
        *str = inserted;
        return;
    }

    // Inisiasi left dan right untuk split jika String tidak kosong dalam hal ini ada left dan right
    String *left = NULL;
    String *right = NULL;

    String_split(*str, index, &left, &right);  // Split
    *str = String_concat(String_concat(left, inserted), right);

    // Safety free
    String_release(left);
    String_release(right);
    String_release(inserted);
}

/**
 * Fungsi untuk menghapus teks di Buffer [PUBLIC API]
 */
void String_delete(String **str, size_t pos_idx, size_t len) {
    if (!str || !*str || len == 0) return;
    if (pos_idx >= (*str)->len) return;  // Guard batas indeks

    // Batasi 'len' agar tidak melebihi sisa panjang string
    if (pos_idx + len > (*str)->len) {
        len = (*str)->len - pos_idx;
    }

    String *left = NULL;
    String *mid_and_right = NULL;
    String *middle = NULL;
    String *right = NULL;

    // Potong bagian kiri [0 ... pos_idx]
    String_split(*str, pos_idx, &left, &mid_and_right);

    // Potong bagian middle [pos_idx ... pos_idx + len] dari sisa kanan
    String_split(mid_and_right, len, &middle, &right);

    // 3Gabungkan bagian left + right (Abaikan 'middle' karena mau dihapus)
    String *new_root = String_concat(left, right);

    // Cleanup & Safety Release
    String_release(middle);         // Hapus teks yang dibuang dari memory
    String_release(mid_and_right);  // Safety free temp node split
    String_release(left);           // Safety free ref count split
    String_release(right);          // Safety free ref count split

    // Update pointer root utama
    String_release(*str);
    *str = new_root;
}

/**
 * Fungsi untuk Get Public API
 */
Bytes String_get(String *str, size_t index, size_t len) {
    Bytes result = {.data = NULL, .len = 0};

    if (!str || index >= str->len || len == 0) return result;

    if (index + len > str->len) {
        len = str->len - index;
    }

    unsigned char *buf = malloc(len + 1);
    if (!buf) return result;

    size_t offset = 0;
    String_collect(str, index, len, buf, &offset);
    buf[offset] = '\0';

    result.data = buf;
    result.len = offset;

    return result;
}

/*
 * Fungsi untuk menghapus buffer Get
 */
void Bytes_free(Bytes *bytes) {
    if (bytes->data) free(bytes->data);
}
