/*
 * TxtEd - Simple Text Editor
 * Copyright (c) 2026 Nash
 * SPDX-License-Identifier: MIT
 */
#ifndef UNDO_H
#define UNDO_H
#include "types.h"

void Undo_init(UndoStack *us);
void Undo_free(UndoStack *us);
void Undo_push(UndoStack *us, UndoType type, size_t offset, const char *text, size_t len);
bool Undo_pop(UndoStack *us, UndoAction *out);
bool Redo_pop(UndoStack *us, UndoAction *out);

void Undo_clear(UndoStack *us);
#endif  // !UNDO_H
