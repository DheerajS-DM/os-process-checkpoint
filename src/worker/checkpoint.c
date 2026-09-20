#include "checkpoint.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>

int ckpt_save_to_file(const char *filepath, const ucontext_t *uc, const dirty_tracker_t *dt, pid_t pid) {
    if (!filepath || !dt || !dt->heap_base) return -1;
    
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);
    
    int fd = open(tmp_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        LOG_E("CKPT", "Failed to open checkpoint file %s: %s", tmp_path, strerror(errno));
        return -1;
    }
    
    /* Always ensure page 0 (containing metadata header) is included */
    uint32_t dirty_count = (uint32_t)dt_count_dirty(dt);
    bool page0_dirty = dt_is_dirty(dt, 0);
    if (!page0_dirty) {
        dirty_count += 1; /* Add page 0 */
    }
    
    ckpt_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = CKPT_MAGIC;
    header.version = CKPT_VERSION;
    header.timestamp_ns = now_ns();
    header.pid = (uint32_t)pid;
    header.heap_base_addr = (uint64_t)(uintptr_t)dt->heap_base;
    header.heap_size_bytes = (uint64_t)dt->heap_size;
    header.ucontext_size = uc ? sizeof(ucontext_t) : 0;
    header.dirty_page_count = dirty_count;
    header.total_pages = (uint32_t)dt->total_pages;
    
    if (write(fd, &header, sizeof(header)) != sizeof(header)) {
        LOG_E("CKPT", "Failed to write header to %s", tmp_path);
        close(fd);
        unlink(tmp_path);
        return -1;
    }
    
    if (uc && header.ucontext_size > 0) {
        if (write(fd, uc, sizeof(ucontext_t)) != sizeof(ucontext_t)) {
            LOG_E("CKPT", "Failed to write ucontext_t to %s", tmp_path);
            close(fd);
            unlink(tmp_path);
            return -1;
        }
    }
    
    uint8_t *heap_bytes = (uint8_t *)dt->heap_base;
    
    /* If page 0 wasn't in the dirty bitmap, write page 0 first */
    if (!page0_dirty) {
        uint32_t page0 = 0;
        write(fd, &page0, sizeof(page0));
        write(fd, heap_bytes, PAGE_SIZE_BYTES);
    }
    
    /* Write all dirty pages */
    for (size_t i = 0; i < dt->total_pages; i++) {
        if (dt_is_dirty(dt, i)) {
            uint32_t page_idx = (uint32_t)i;
            if (write(fd, &page_idx, sizeof(page_idx)) != sizeof(page_idx)) {
                close(fd);
                unlink(tmp_path);
                return -1;
            }
            if (write(fd, heap_bytes + (i * PAGE_SIZE_BYTES), PAGE_SIZE_BYTES) != PAGE_SIZE_BYTES) {
                close(fd);
                unlink(tmp_path);
                return -1;
            }
        }
    }
    
    fsync(fd);
    close(fd);
    
    /* Atomic rename */
    if (rename(tmp_path, filepath) < 0) {
        LOG_E("CKPT", "Failed to rename %s to %s: %s", tmp_path, filepath, strerror(errno));
        unlink(tmp_path);
        return -1;
    }
    
    LOG_I("CKPT", "Saved checkpoint to %s (pages=%u/%zu)",
          filepath, dirty_count, dt->total_pages);
    
    return 0;
}
