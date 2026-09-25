/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "matches.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "types.h"

#define XOR_CHECK(src, val) ((src ^ val) == 0)
/**
 * Fungsi pengganti strcasestr
 */
const char *my_strcasestr(const char *haystack, const char *needle, size_t needle_len) {
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) return haystack;
    }

    return nullptr;
}

/**
 * Fungsi untuk membandingkan score untuk qsort [PRIVATE API]
 */
int compare_scores(const void *a, const void *b) {
    FilteredItem *itemA = (FilteredItem *)a;
    FilteredItem *itemB = (FilteredItem *)b;
    return itemB->score - itemA->score;  // Descending (tertinggi di atas)
}

/**
 * Fungsi Scoring Pintar (Exact Case Bonus + Fuzzy)
 */
int calculate_score(const char *query, const char *label) {
    if (!query || !label) return -1;
    if (XOR_CHECK(query[0], '\0')) return 0;

    size_t q_len = strlen(query);
    size_t l_len = strlen(label);

    if (q_len > l_len) return -1;

    //  Dapatkan nama file saja (tanpa folder path)
    const char *filename = strrchr(label, '/');
    if (filename)
        filename++;  // Skip slash '/'
    else
        filename = label;

    int extra_filename_bonus = 0;

    // Exact / Prefix Match pada Filename (Dapat prioritas tertinggi!)
    if (strncasecmp(filename, query, q_len) == 0) {
        extra_filename_bonus += 2000;
    }

    // Exact Substring Match
    const char *found = my_strcasestr(label, query, q_len);
    if (found != nullptr) {
        int base_score = 1000 + extra_filename_bonus;
        if (strncmp(found, query, q_len) == 0) base_score += 500;  // Case exact
        base_score -= (int)(found - label) * 5;                    // Penalty jarak dari awal
        base_score -= (int)(l_len - q_len);
        return base_score;
    }

    // Fuzzy Matching dengan Consecutive Bonus
    int score = extra_filename_bonus;
    const char *q = query;
    const char *l = label;
    int consecutive = 0;

    while (*q && *l) {
        bool match = false;

        if (*q == *l) {
            score += 30 + (consecutive * 15);
            match = true;
        } else if (tolower((unsigned char)*q) == tolower((unsigned char)*l)) {
            score += 15 + (consecutive * 10);
            match = true;
        }

        if (match) {
            // Word boundary bonus (setelah '/', '_', atau huruf kapital)
            if (l == label || XOR_CHECK(*(l - 1), '_') || XOR_CHECK(*(l - 1), '/') ||
                isupper((unsigned char)*l)) {
                score += 50;
            }
            consecutive++;
            q++;
        } else {
            consecutive = 0;  // Reset bonus berurutan jika ada karakter beda
        }
        l++;
    }

    if (!XOR_CHECK(*q, '\0')) return -1;  // Query tidak habis ter-match

    score -= (int)(l_len - q_len);
    return score;
}

/**
 * Fungsi untuk memfilter dan mengurutkan completion [PRIVATE API]
 */
size_t filter_and_sort_completion(const char *query, FilteredItem *filtered, size_t count) {
    if (!filtered || count == 0) return 0;

    size_t filtered_count = 0;
    for (size_t i = 0; i < count; i++) {
        int score = (query[0] == '\0') ? 1 : calculate_score(query, filtered[i].label);

        if (score >= 0) {
            filtered[filtered_count].original_idx = i;
            filtered[filtered_count].score = score;
            filtered_count++;
        }
    }

    // Sort daftar pilihan berdasarkan score tertinggi!
    if (query && query[0] != '\0' && filtered_count > 0) {
        qsort(filtered, filtered_count, sizeof(FilteredItem), compare_scores);
    }
    return filtered_count;
}
