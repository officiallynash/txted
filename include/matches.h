/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef MATCHES_H
#define MATCHES_H
#include <stddef.h>

#include "types.h"
int calculate_score(const char *query, const char *label);
size_t filter_and_sort_completion(const char *query, FilteredItem *filtered, size_t count);
#endif
