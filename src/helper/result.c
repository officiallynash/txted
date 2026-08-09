#include "result.h"

#include <unistd.h>

/**
 * Fungsi untuk membuat Result OK
 */
Result Ok(void *data) { return (Result){.type = RESULT_OK, .data = data}; }

/**
 * Fungsi untuk membuat Result ERR
 */
Result Err(const char *msg) { return (Result){.type = RESULT_ERR, .data = (void *)msg}; }

/**
 * Fungsi untuk membersihkan Result
 */
void Result_free(Result *result) {
    if (result->data) result->data = NULL;
}
