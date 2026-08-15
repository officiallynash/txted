/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#include "lsp_server.h"
#include "lsp_ui.h"

extern int calculate_score(const char *query, const char *label);  // calculate_score (lsp_client.c)

/* =================================
 * PRIVATE API
 * ================================= */

/**
 * Fungsi untuk membandingkan score untuk qsort [PRIVATE API]
 */
int compare_scores(const void *a, const void *b) {
    FilteredItem *itemA = (FilteredItem *)a;
    FilteredItem *itemB = (FilteredItem *)b;
    return itemB->score - itemA->score;  // Descending (tertinggi di atas)
}

/**
 * Fungsi untuk memfilter dan mengurutkan completion [PRIVATE API]
 */
void filter_and_sort_completion(CompletionList *list, const char *query) {
    FilteredItem filtered[256];
    int filtered_count = 0;

    for (size_t i = 0; i < list->count && filtered_count < 256; i++) {
        int score = calculate_score(query, list->items[i].label);
        if (score >= 0) {
            filtered[filtered_count].item = &list->items[i];
            filtered[filtered_count].score = score;
            filtered_count++;
        }
    }

    // Sort daftar pilihan berdasarkan score tertinggi!
    qsort(filtered, filtered_count, sizeof(FilteredItem), compare_scores);
}

/* =========================================
 * PUBLIC API
 * ========================================= */

/**
 * Mengambil item completion aktif sesuai urutan hasil Filter & Sort [PUBLIC API]
 */
CompletionItem *lsp_get_selected_item(const char *current_word) {
    if (!g_lsp_ui.has_completion || g_lsp_ui.completion.count == 0) return NULL;

    FilteredItem filtered[256];
    int total_items = 0;

    for (size_t i = 0; i < g_lsp_ui.completion.count && total_items < 256; i++) {
        const char *label = g_lsp_ui.completion.items[i].label;
        if (!label) continue;

        int score = calculate_score(current_word, label);
        if (score >= 0) {
            filtered[total_items].item = &g_lsp_ui.completion.items[i];
            filtered[total_items].score = score;
            total_items++;
        }
    }

    if (total_items == 0) return NULL;

    if (current_word[0] != '\0') {
        qsort(filtered, total_items, sizeof(FilteredItem), compare_scores);
    }

    if (g_lsp_ui.selected_index < 0 || g_lsp_ui.selected_index >= total_items) {
        return NULL;
    }

    return filtered[g_lsp_ui.selected_index].item;
}
