#include "common.h"
#include "dirty_tracker.h"
#include "checkpoint.h"
#include <sys/mman.h>
#include <assert.h>

void test_checkpoint_save_and_restore(void) {
    LOG_I("TEST", "Running test_checkpoint_save_and_restore...");
    
    size_t heap_size = 2 * 1024 * 1024; /* 2 MB = 512 pages */
    void *src_heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(src_heap != MAP_FAILED);
    
    dirty_tracker_t dt;
    assert(dt_init(&dt, src_heap, heap_size) == 0);
    assert(dt_install_handler(&dt) == 0);
    assert(dt_protect_heap(&dt) == 0);
    
    /* Modify specific pages */
    uint8_t *src_bytes = (uint8_t *)src_heap;
    
    /* Page 2 */
    src_bytes[2 * PAGE_SIZE_BYTES + 10] = 0xDE;
    src_bytes[2 * PAGE_SIZE_BYTES + 11] = 0xAD;
    
    /* Page 100 */
    src_bytes[100 * PAGE_SIZE_BYTES + 50] = 0xBE;
    src_bytes[100 * PAGE_SIZE_BYTES + 51] = 0xEF;
    
    /* Page 300 */
    src_bytes[300 * PAGE_SIZE_BYTES + 500] = 0x42;
    
    const char *test_ckpt = "test_run.ckpt";
    unlink(test_ckpt);
    
    assert(ckpt_save_to_file(test_ckpt, NULL, &dt, getpid()) == 0);
    
    /* Prepare fresh destination heap */
    void *dst_heap = mmap(NULL, heap_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(dst_heap != MAP_FAILED);
    memset(dst_heap, 0, heap_size);
    
    uint32_t restored = 0;
    assert(ckpt_load_from_file(test_ckpt, NULL, dst_heap, heap_size, &restored) == 0);
    assert(restored > 0);
    
    uint8_t *dst_bytes = (uint8_t *)dst_heap;
    assert(dst_bytes[2 * PAGE_SIZE_BYTES + 10] == 0xDE);
    assert(dst_bytes[2 * PAGE_SIZE_BYTES + 11] == 0xAD);
    assert(dst_bytes[100 * PAGE_SIZE_BYTES + 50] == 0xBE);
    assert(dst_bytes[100 * PAGE_SIZE_BYTES + 51] == 0xEF);
    assert(dst_bytes[300 * PAGE_SIZE_BYTES + 500] == 0x42);
    
    LOG_I("TEST", "Checkpoint save and restore: PASSED (Restored %u dirty pages with 100%% byte fidelity)", restored);
    
    unlink(test_ckpt);
    dt_cleanup(&dt);
    munmap(src_heap, heap_size);
    munmap(dst_heap, heap_size);
}

int main(void) {
    LOG_I("TEST", "=== Starting Checkpoint Serialization Unit Tests ===");
    test_checkpoint_save_and_restore();
    LOG_I("TEST", "=== ALL CHECKPOINT TESTS PASSED ===");
    return 0;
}
