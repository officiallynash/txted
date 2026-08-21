/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef ROPE_H
#define ROPE_H

#include <stddef.h>
#include <stdint.h>

typedef struct String String;

/**
 * Struct untuk membungkus ketika String_get
 */
typedef struct {
    unsigned char *data;
    size_t len;
} Bytes;

String *String_new();
void String_insert(String **str, size_t index, const char *text, size_t len);
void String_delete(String **str, size_t pos_idx, size_t len);
size_t String_len(String *str);
void String_release(String *str);
Bytes String_get(String *str, size_t index, size_t len);
void Bytes_free(Bytes *bytes);

#endif
