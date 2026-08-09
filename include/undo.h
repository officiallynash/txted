#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>
#include <stdbool.h>

#define UNDO_MAX_ACTIONS 1000
#define UNDO_TIMEOUT 500

typedef enum {
   UNDO_INSERT,
   UNDO_DELETE
} UndoType;

typedef struct {
    UndoType type;
    size_t offset;
    size_t len;
    char *text;

    long timestamp_ms;
}  UndoAction;

typedef struct {
    UndoAction *actions;
    size_t count;
    size_t capacity;
    size_t current;
    bool is_undoing;
} UndoStack;

void Undo_init(UndoStack *us);
void Undo_free(UndoStack *us);

void Undo_push(UndoStack *us, UndoType type, size_t offset, const char *text, size_t len);

bool Undo_pop(UndoStack *us, UndoAction *out);
bool Redo_pop(UndoStack *us, UndoAction *out);

void Undo_clear(UndoStack *us);
#endif // !UNDO_H

