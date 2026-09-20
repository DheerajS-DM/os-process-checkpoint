#include "workload.h"
#include <math.h>

#define MATRIX_DIM 512 /* 512x512 doubles */

typedef struct {
    size_t dim;
    double *mat_a;
    double *mat_b;
    double *mat_c;
    size_t current_row;
} matrix_workload_state_t;

static void rebind_matrix_pointers(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    matrix_workload_state_t *st = (matrix_workload_state_t *)ctx->heap_base;
    size_t matrix_elements = (size_t)MATRIX_DIM * MATRIX_DIM;
    double *base = (double *)ctx->heap_base;
    size_t offset_doubles = (sizeof(matrix_workload_state_t) + sizeof(double) - 1) / sizeof(double);
    
    st->dim = MATRIX_DIM;
    st->mat_a = base + offset_doubles;
    st->mat_b = st->mat_a + matrix_elements;
    st->mat_c = st->mat_b + matrix_elements;
}

void workload_matrix_init(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    
    rebind_matrix_pointers(ctx);
    matrix_workload_state_t *st = (matrix_workload_state_t *)ctx->heap_base;
    size_t matrix_elements = (size_t)MATRIX_DIM * MATRIX_DIM;
    st->current_row = 0;
    
    for (size_t i = 0; i < matrix_elements; i++) {
        st->mat_a[i] = (double)(i % 100) * 0.01;
        st->mat_b[i] = (double)((i * 7) % 100) * 0.01;
        st->mat_c[i] = 0.0;
    }
    
    LOG_I("WORKLOAD", "Matrix Cascade initialized (dim=%dx%d)", MATRIX_DIM, MATRIX_DIM);
}

void workload_matrix_step(workload_ctx_t *ctx) {
    if (!ctx || !ctx->heap_base) return;
    
    rebind_matrix_pointers(ctx);
    matrix_workload_state_t *st = (matrix_workload_state_t *)ctx->heap_base;
    size_t n = st->dim;
    if (n == 0) n = MATRIX_DIM;
    
    size_t rows_per_step = 8;
    
    for (size_t step = 0; step < rows_per_step; step++) {
        size_t r = st->current_row % n;
        
        for (size_t c = 0; c < n; c++) {
            double sum = 0.0;
            for (size_t k = 0; k < n; k++) {
                sum += st->mat_a[r * n + k] * st->mat_b[k * n + c];
            }
            st->mat_c[r * n + c] = sum;
        }
        
        st->current_row = (st->current_row + 1) % n;
        if (st->current_row == 0) {
            for (size_t i = 0; i < n; i++) {
                for (size_t j = 0; j < n; j++) {
                    st->mat_a[j * n + i] = st->mat_c[i * n + j] * 0.999 + 0.001;
                }
            }
            ctx->iterations++;
        }
    }
}
