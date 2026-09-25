/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "rope.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint8_t u8;
typedef uint32_t u32;

// Balik ke constexpr, menurutku lebih aman daripada #define walau di cast ke u32
constexpr u32 MAX_SIZE_LEAF = 1024;
constexpr u32 ROPE_IS_LEAF = 1u << 0;

String *String_new();            // Register awal
size_t String_len(String *str);  // Register Awal

/**
 * Struct pembungkus untuk Rope (String)
 * Sengaja di Private karena ini inti dari Manipulasi teks di Buffer
 */
struct String {
    struct String *left;   // 8 byte
    struct String *right;  // 8 byte
    u8 *str;               // 8 byte
    u32 len;               // 4 byte
    u32 weight;            // 4 byte
    u32 ref_count;         // 4 byte
    u32 flags;             // 4 byte
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
    // Menggunakan Malloc karena Malloc lebih efektif untuk String atau Rope logic
    // Jika calloc lebih efektif jika hanya sekali deklarasi
    String *new = malloc(sizeof(String));
    new->str = malloc(len + 1);
    if (!new->str) {
        free(new);
        return nullptr;
    }

    memcpy(new->str, text, len);
    new->str[len] = '\0';  // Null terminator
    new->len = len;
    new->weight = len;
    new->ref_count = 1;
    new->left = nullptr;
    new->right = nullptr;

    // Kasih flags
    new->flags = ROPE_IS_LEAF;

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
    String *parent = calloc(1, sizeof(String));
    if (!parent) return nullptr;

    parent->str = nullptr;
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
 * Helper Internal untuk build Balance si Rope
 */
static String *String_build_balanced(String **leaves, size_t start, size_t end) {
    if (start >= end) return nullptr;
    if (end - start == 1) return leaves[start];

    size_t mid = (start + end) >> 1;

    String *left = String_build_balanced(leaves, start, mid);
    String *right = String_build_balanced(leaves, mid, end);

    return String_concat(left, right);
}

/**
 * Fungsi untuk String Split [PRIVATE API]
 */
static void String_split(String *root, size_t index, String **left, String **right) {
    // Guard clause kalau Root itu Kosong
    if (!root) {
        *left = *right = nullptr;
        return;
    }

    if (root->flags & ROPE_IS_LEAF) {
        // jika index kurang dari nol maka Kiri kosong
        if (index <= 0) {
            *left = nullptr;
            *right = root;
            String_retain(root);
        }
        // Jika index lebih dari root len, maka kanan kosong
        else if (index >= root->len) {
            *left = root;
            *right = nullptr;
            String_retain(root);
        }
        // Kalau tidak maka buat baru
        else {
            *left = String_make_leaf((const char *)root->str, index);
            *right = String_make_leaf((const char *)root->str + index, root->len - index);
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
        if (lr) String_release(lr);  // safety free
    } else {
        String *rl, *rr;
        String_split(root->right, index - root->weight, &rl, &rr);

        *left = String_concat(root->left, rl);
        *right = rr;
        if (rl) String_release(rl);  // safety free
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
    if (str->flags & ROPE_IS_LEAF) {
        // Proses hanya jika start kurang dari panjang
        if (start < str->len) {
            size_t bytes_to_copy = str->len - start;
            if (bytes_to_copy > len) bytes_to_copy = len;

            // Copy memory zero cost
            memcpy(buffer + *offset, str->str + start, bytes_to_copy);
            *offset += bytes_to_copy;
        }

        return;
    }

    // Jika ada left dan right
    if (start < str->weight) {
        size_t left_len = str->weight - start;
        size_t copy_from_left = (left_len < len) ? left_len : len;

        // Recursive
        String_collect(str->left, start, copy_from_left, buffer, offset);
        if (len > copy_from_left) {
            String_collect(str->right, 0, len - copy_from_left, buffer, offset);
        }

    } else {
        String_collect(str->right, start - str->weight, len, buffer, offset);
    }
}

/**
 * Helper untuk menghitung kedalaman (height) dari Rope tree [PRIVATE API]
 */
static size_t String_height(String *str) {
    if (!str || (str->flags & ROPE_IS_LEAF)) return 1;  // Leaf bernilai height 1

    size_t hl = String_height(str->left);
    size_t hr = String_height(str->right);
    return 1 + (hl > hr ? hl : hr);
}

/**
 * Helper untuk mengumpulkan seluruh leaf node ke dalam array [PRIVATE API]
 */
static void String_collect_leaves(String *str, String **leaves, size_t *count) {
    if (!str) return;

    // Ga perlu di kasih ref_count karena operasi balancing melibatkan split
    if (str->flags & ROPE_IS_LEAF) {  // Jika ini leaf node
        leaves[*count] = str;
        (*count)++;
        return;
    }

    String_collect_leaves(str->left, leaves, count);
    String_collect_leaves(str->right, leaves, count);
}

/**
 * Helper untuk hitung total leaf di dalam Rope [PRIVATE API]
 */
static size_t String_count_leaves(String *str) {
    if (!str) return 0;
    if (str->flags & ROPE_IS_LEAF) return 1;
    return String_count_leaves(str->left) + String_count_leaves(str->right);
}

/**
 * Fungsi Rebalance Utama [PUBLIC API]
 */
void String_rebalance(String **root) {
    if (!root || !*root) return;

    size_t height = String_height(*root);
    size_t leaf_count = String_count_leaves(*root);

    // Pohon kecil tidak perlu di-rebalance
    if (leaf_count <= 2) return;

    // Hitung ideal height (~ log2(leaf_count))
    size_t temp = leaf_count;
    size_t log2_leaves = 0;
    while (temp >>= 1) log2_leaves++;

    // Rebalance HANYA jika tinggi pohon melebihi 2x lipat tinggi ideal
    // ATAU sudah menyentuh batas kritis kedalaman rekursi (misal 28)
    size_t threshold = (log2_leaves + 1) << 1;  // Setara dengan (log2_leaves * 2) + 2
    if (height <= threshold && height < 28) return;

    // Alokasi temporary array
    String **leaves = malloc(leaf_count * sizeof(String *));
    if (!leaves) return;

    size_t count = 0;
    // Collect tanpa merusak / menambah ref_count
    String_collect_leaves(*root, leaves, &count);

    // Bikin struktur internal node baru yang seimbang.
    // PENTING: String_build_balanced memanggil String_concat yang melakukan String_retain
    // pada tiap element di array leaves. Ini BENAR karena leaves akan punya parent baru!
    String *new_root = String_build_balanced(leaves, 0, count);
    free(leaves);

    // Release struktur tree LAMA (ini akan me-release internal node lama
    // DAN menguraikan 1 ref_count lama dari masing-masing leaf)
    String_release(*root);

    // Tetapkan root baru
    *root = new_root;
}

/**
 * Fungsi untuk mengetahui panjang String [PUBLIC API]
 */
size_t String_len(String *str) { return str ? str->len : 0; }

/**
 * Fungsi untuk deklarasi String [PUBLIC API]
 */
String *String_new() {
    String *new = calloc(1, sizeof(String));
    // Karena pakai calloc jadi ga perlu pasang flag lain kali ya HAHHA
    return new;
}

/**
 * Fungsi untuk reference counting [PUBLIC API]
 */
void String_release(String *str) {
    if (!str) return;

    str->ref_count--;
    if (str->ref_count > 0) return;

    // Rekursif release
    String_release(str->left);
    String_release(str->right);

    // Jika str tidak kosong maka hapus
    if (str->flags & ROPE_IS_LEAF) {
        free(str->str);
    }
    // Hapus root atau String
    free(str);
}

/**
 * Fungsi untuk insert String [PUBLIC API]
 */
void String_insert(String **str, size_t index, const char *text, size_t len) {
    if (!text || len == 0) return;

    String *inserted = nullptr;
    // Jika teks melebihi 1024 maka bagi menjadi 2
    if (len > MAX_SIZE_LEAF) {
        // Hitung berapa banyak leaf yang dibutuhkan
        size_t num_leaves = (len + 1023) >> 10;
        String **leaves = malloc(num_leaves * sizeof(String *));

        size_t offset = 0;
        for (size_t i = 0; i < num_leaves; i++) {
            size_t remaining = len - offset;
            size_t chunk_len = (remaining > MAX_SIZE_LEAF) ? MAX_SIZE_LEAF : remaining;
            leaves[i] = String_make_leaf(text + offset, chunk_len);
            offset += chunk_len;
        }

        // Buat Tree Seimbang secara instan (Zero Deep-Recursion)
        inserted = String_build_balanced(leaves, 0, num_leaves);
        free(leaves);
    } else {
        // Jika masih di bawah 1024, maka langsung buat baru saja
        inserted = String_make_leaf(text, len);
    }

    // Jika bukan left dan right langsung assign ke Root
    if (*str && (*str)->len == 0 && (*str)->str == nullptr && (*str)->left == nullptr &&
        (*str)->right == nullptr) {
        free(*str);
        *str = inserted;
        return;
    }

    // Inisiasi left dan right untuk split jika String tidak kosong dalam hal ini
    // ada left dan right
    String *left = nullptr;
    String *right = nullptr;

    String_split(*str, index, &left, &right);  // Split
    *str = String_concat(String_concat(left, inserted), right);
    String_rebalance(str);

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

    String *left = nullptr;
    String *mid_and_right = nullptr;
    String *middle = nullptr;
    String *right = nullptr;

    // Potong bagian kiri [0 ... pos_idx]
    String_split(*str, pos_idx, &left, &mid_and_right);

    // Potong bagian middle [pos_idx ... pos_idx + len] dari sisa kanan
    String_split(mid_and_right, len, &middle, &right);

    // Gabungkan bagian left + right (Abaikan 'middle' karena mau dihapus)
    *str = String_concat(left, right);

    // Cleanup & Safety Release
    String_release(middle);         // Hapus teks yang dibuang dari memory
    String_release(mid_and_right);  // Safety free temp node split
    String_release(left);           // Safety free ref count split
    String_release(right);          // Safety free ref count split

    // Rebalance
    String_rebalance(str);
}

/**
 * Fungsi untuk Get Public API
 */
Bytes String_get(String *str, size_t index, size_t len) {
    // Result default itu nullptr dan len 0
    Bytes result = {.data = nullptr, .len = 0};

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
