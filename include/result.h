/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef RESULT_H
#define RESULT_H

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
