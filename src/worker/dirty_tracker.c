#include "dirty_tracker.h"
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

dirty_tracker_t *g_active_tracker = NULL;

static void sigsegv_trap_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    (void)sig;
    (void)ucontext_raw;
    
    if (!g_active_tracker) {
        struct sigaction sa_dfl;
        memset(&sa_dfl, 0, sizeof(sa_dfl));
        sa_dfl.sa_handler = SIG_DFL;
        sigaction(SIGSEGV, &sa_dfl, NULL);
        raise(SIGSEGV);
        return;
    }
    
    void *fault_addr = info->si_addr;
    uintptr_t base = (uintptr_t)g_active_tracker->heap_base;
    uintptr_t limit = base + g_active_tracker->heap_size;
    uintptr_t addr = (uintptr_t)fault_addr;
    
    /* If fault is outside tracked heap, pass it to OS / default handler */
    if (addr < base || addr >= limit) {
        struct sigaction sa_dfl;
        memset(&sa_dfl, 0, sizeof(sa_dfl));
        sa_dfl.sa_handler = SIG_DFL;
        sigaction(SIGSEGV, &sa_dfl, NULL);
        raise(SIGSEGV);
        return;
    }
    
    size_t offset = addr - base;
    size_t page_idx = offset / PAGE_SIZE_BYTES;
    
    /* Mark dirty in bitmap */
    dt_mark_page(g_active_tracker, page_idx);
    
    /* Unlock contiguous block of pages (e.g. 16 pages = 64 KB) to minimize trap overhead */
    size_t block_start_page = (page_idx / UNLOCK_BLOCK_PAGES) * UNLOCK_BLOCK_PAGES;
    size_t block_page_count = UNLOCK_BLOCK_PAGES;
    if (block_start_page + block_page_count > g_active_tracker->total_pages) {
        block_page_count = g_active_tracker->total_pages - block_start_page;
    }
    
    void *block_addr = (void *)(base + block_start_page * PAGE_SIZE_BYTES);
    size_t block_bytes = block_page_count * PAGE_SIZE_BYTES;
    
    if (mprotect(block_addr, block_bytes, PROT_READ | PROT_WRITE) < 0) {
        /* If mprotect failed in signal handler, fatal */
        ssize_t ret = write(STDERR_FILENO, "FATAL: mprotect failed in SIGSEGV handler\n", 42);
        (void)ret;
        _exit(EXIT_FAILURE);
    }
    
    /* Mark all pages in this block as dirty */
    for (size_t i = 0; i < block_page_count; i++) {
        dt_mark_page(g_active_tracker, block_start_page + i);
    }
    
    __atomic_fetch_add(&g_active_tracker->total_faults, 1, __ATOMIC_RELAXED);
}

int dt_init(dirty_tracker_t *dt, void *heap_base, size_t heap_size) {
    if (!dt || !heap_base || heap_size == 0) return -1;
    
    dt->heap_base = heap_base;
    dt->heap_size = heap_size;
    dt->total_pages = (heap_size + PAGE_SIZE_BYTES - 1) / PAGE_SIZE_BYTES;
    
    dt->bitmap.total_pages = dt->total_pages;
    dt->bitmap.num_words = (dt->total_pages + 63) / 64;
    dt->bitmap.words = (uint64_t *)calloc(dt->bitmap.num_words, sizeof(uint64_t));
    if (!dt->bitmap.words) {
        LOG_E("TRACKER", "Failed to allocate dirty bitmap words");
        return -1;
    }
    
    dt->total_faults = 0;
    dt->last_fault_time_ns = 0;
    
    g_active_tracker = dt;
    return 0;
}

void dt_cleanup(dirty_tracker_t *dt) {
    if (!dt) return;
    if (dt->bitmap.words) {
        free(dt->bitmap.words);
        dt->bitmap.words = NULL;
    }
    if (g_active_tracker == dt) {
        g_active_tracker = NULL;
    }
}

void dt_mark_page(dirty_tracker_t *dt, size_t page_idx) {
    if (!dt || page_idx >= dt->total_pages) return;
    size_t word_idx = page_idx / 64;
    size_t bit_idx = page_idx % 64;
    __atomic_fetch_or(&dt->bitmap.words[word_idx], (1ULL << bit_idx), __ATOMIC_RELAXED);
}

bool dt_is_dirty(const dirty_tracker_t *dt, size_t page_idx) {
    if (!dt || page_idx >= dt->total_pages) return false;
    size_t word_idx = page_idx / 64;
    size_t bit_idx = page_idx % 64;
    return (dt->bitmap.words[word_idx] & (1ULL << bit_idx)) != 0;
}

size_t dt_count_dirty(const dirty_tracker_t *dt) {
    if (!dt || !dt->bitmap.words) return 0;
    size_t count = 0;
    for (size_t i = 0; i < dt->bitmap.num_words; i++) {
        count += __builtin_popcountll(dt->bitmap.words[i]);
    }
    return count;
}

void dt_clear_all(dirty_tracker_t *dt) {
    if (!dt || !dt->bitmap.words) return;
    memset(dt->bitmap.words, 0, dt->bitmap.num_words * sizeof(uint64_t));
}

int dt_protect_heap(dirty_tracker_t *dt) {
    if (!dt || !dt->heap_base || dt->heap_size == 0) return -1;
    if (mprotect(dt->heap_base, dt->heap_size, PROT_READ) < 0) {
        LOG_E("TRACKER", "mprotect PROT_READ failed on %p (size=%zu): %s",
              dt->heap_base, dt->heap_size, strerror(errno));
        return -1;
    }
    return 0;
}

int dt_unprotect_heap(dirty_tracker_t *dt) {
    if (!dt || !dt->heap_base || dt->heap_size == 0) return -1;
    if (mprotect(dt->heap_base, dt->heap_size, PROT_READ | PROT_WRITE) < 0) {
        LOG_E("TRACKER", "mprotect PROT_READ|PROT_WRITE failed on %p: %s",
              dt->heap_base, strerror(errno));
        return -1;
    }
    return 0;
}

int dt_reset(dirty_tracker_t *dt) {
    if (!dt) return -1;
    dt_clear_all(dt);
    return dt_protect_heap(dt);
}

int dt_install_handler(dirty_tracker_t *dt) {
    g_active_tracker = dt;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_trap_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        LOG_E("TRACKER", "Failed to install SIGSEGV handler: %s", strerror(errno));
        return -1;
    }
    return 0;
}
