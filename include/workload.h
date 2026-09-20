#ifndef WORKLOAD_H
#define WORKLOAD_H

#include "common.h"

typedef enum {
    WORKLOAD_MATRIX = 0,
    WORKLOAD_HASH_RING,
    WORKLOAD_UNKNOWN
} workload_kind_t;

typedef struct {
    workload_kind_t kind;
    void *heap_base;
    size_t heap_size;
    volatile bool *should_stop;
    uint64_t iterations;
} workload_ctx_t;

workload_kind_t workload_parse_kind(const char *str);
const char *workload_kind_name(workload_kind_t kind);

void workload_matrix_init(workload_ctx_t *ctx);
void workload_matrix_step(workload_ctx_t *ctx);

void workload_hash_init(workload_ctx_t *ctx);
void workload_hash_step(workload_ctx_t *ctx);

#endif /* WORKLOAD_H */
