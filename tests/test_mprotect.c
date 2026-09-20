#include "common.h"
#include "dirty_tracker.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <assert.h>

static dirty_tracker_t g_test_dt;

void test_mprotect_trap_and_retry(void) {
    LOG_I("TEST", "Running test_mprotect_trap_and_retry...");
    
    size_t heap_size = 1024 * 1024; /* 1 MB = 256 pages */
    void *heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(heap != MAP_FAILED);
    
    assert(dt_init(&g_test_dt, heap, heap_size) == 0);
    assert(dt_install_handler(&g_test_dt) == 0);
    assert(dt_protect_heap(&g_test_dt) == 0);
    
    /* Pages are read-only right now */
    volatile uint8_t *bytes = (volatile uint8_t *)heap;
    
    /* Read should succeed without fault */
    uint8_t val = bytes[0];
    (void)val;
    assert(g_test_dt.total_faults == 0);
    
    /* Write to page 0 (offset 0) */
    bytes[0] = 0xAA;
    assert(bytes[0] == 0xAA);
    assert(g_test_dt.total_faults == 1);
    assert(dt_is_dirty(&g_test_dt, 0) == true);
    
    /* Because of 16-page block unlock, page 5 should now be writable without another fault */
    bytes[5 * PAGE_SIZE_BYTES] = 0xBB;
    assert(bytes[5 * PAGE_SIZE_BYTES] == 0xBB);
    assert(g_test_dt.total_faults == 1); /* Still 1! Block unlock worked */
    
    /* Write to page 32 (next block, offset 32 * 4096) */
    bytes[32 * PAGE_SIZE_BYTES] = 0xCC;
    assert(g_test_dt.total_faults == 2);
    assert(dt_is_dirty(&g_test_dt, 32) == true);
    
    LOG_I("TEST", "mprotect trap and 64KB block unlock: PASSED (faults=%lu, dirty_pages=%zu)",
          g_test_dt.total_faults, dt_count_dirty(&g_test_dt));
    
    dt_cleanup(&g_test_dt);
    munmap(heap, heap_size);
}

void test_out_of_bounds_segv_passthrough(void) {
    LOG_I("TEST", "Running test_out_of_bounds_segv_passthrough...");
    
    pid_t pid = fork();
    if (pid == 0) {
        /* In child: setup tracker and try to write NULL */
        size_t heap_size = 64 * 1024;
        void *heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        dt_init(&g_test_dt, heap, heap_size);
        dt_install_handler(&g_test_dt);
        dt_protect_heap(&g_test_dt);
        
        /* Dereference NULL — should NOT be caught by dirty tracker */
        volatile int *null_ptr = (volatile int *)0x0;
        *null_ptr = 42;
        _exit(0); /* Should not be reached */
    }
    
    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFSIGNALED(status));
    assert(WTERMSIG(status) == SIGSEGV);
    
    LOG_I("TEST", "Out-of-bounds SIGSEGV pass-through: PASSED (Child properly died of SIGSEGV)");
}

int main(void) {
    LOG_I("TEST", "=== Starting mprotect & Memory Trap Unit Tests ===");
    test_mprotect_trap_and_retry();
    test_out_of_bounds_segv_passthrough();
    LOG_I("TEST", "=== ALL MPROTECT TESTS PASSED ===");
    return 0;
}
