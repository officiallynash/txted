#ifndef RESULT_H
#define RESULT_H

typedef enum {
    RESULT_OK, RESULT_ERR
} ResultType;

typedef struct {
    ResultType type;
    void *data;
} Result;

Result Ok(void *data);
Result Err(const char *msg);
void Result_free(Result *result);
#endif
