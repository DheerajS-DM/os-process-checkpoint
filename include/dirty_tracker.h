#ifndef DIRTY_TRACKER_H
#define DIRTY_TRACKER_H

#include "common.h"
#include <signal.h>
#include <ucontext.h>

typedef struct {
    uint64_t *words;
    size_t num_words;
    size_t total_pages;
} dirty_bitmap_t;

typedef struct {
    void *heap_base;
    size_t heap_size;
    size_t total_pages;
    dirty_bitmap_t bitmap;
    volatile uint64_t total_faults;
    volatile uint64_t last_fault_time_ns;
} dirty_tracker_t;

int dt_init(dirty_tracker_t *dt, void *heap_base, size_t heap_size);
void dt_cleanup(dirty_tracker_t *dt);

void dt_mark_page(dirty_tracker_t *dt, size_t page_idx);
bool dt_is_dirty(const dirty_tracker_t *dt, size_t page_idx);
size_t dt_count_dirty(const dirty_tracker_t *dt);
void dt_clear_all(dirty_tracker_t *dt);

int dt_protect_heap(dirty_tracker_t *dt);
int dt_unprotect_heap(dirty_tracker_t *dt);
int dt_reset(dirty_tracker_t *dt);

int dt_install_handler(dirty_tracker_t *dt);

/* Global tracker instance pointer for SIGSEGV handler */
extern dirty_tracker_t *g_active_tracker;

#endif /* DIRTY_TRACKER_H */
