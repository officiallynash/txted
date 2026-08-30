/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef RESULT_H
#define RESULT_H

// Helper untuk bitwise operation di Flag
#define HAS_FLAG(flag, val) ((flag & val) != 0)
#define SET_FLAG(flag, val) (flag |= val)
#define CLR_FLAG(flag, val) (flag &= ~val)

// Enum untuk type Result
typedef enum { RESULT_OK, RESULT_ERR } ResultType;

// Struct untuk Generic Result
typedef struct {
    ResultType type;
    void *data;
} Result;

Result Ok(void *data);
Result Err(const char *msg);
void Result_free(Result *result);
#endif
