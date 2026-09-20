#include "checkpoint.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

int ckpt_load_from_file(const char *filepath, ucontext_t *uc, void *heap_base, size_t heap_size, uint32_t *restored_pages) {
    if (!filepath || !heap_base || heap_size == 0) return -1;
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        LOG_E("RESTORE", "Failed to open checkpoint %s: %s", filepath, strerror(errno));
        return -1;
    }
    
    ckpt_header_t header;
    if (read(fd, &header, sizeof(header)) != sizeof(header)) {
        LOG_E("RESTORE", "Failed to read header from %s", filepath);
        close(fd);
        return -1;
    }
    
    if (header.magic != CKPT_MAGIC) {
        LOG_E("RESTORE", "Invalid magic 0x%08X (expected 0x%08X)", header.magic, CKPT_MAGIC);
        close(fd);
        return -1;
    }
    
    if (header.version != CKPT_VERSION) {
        LOG_E("RESTORE", "Unsupported checkpoint version %u", header.version);
        close(fd);
        return -1;
    }
    
    if (header.ucontext_size > 0 && uc) {
        if (read(fd, uc, sizeof(ucontext_t)) != sizeof(ucontext_t)) {
            LOG_E("RESTORE", "Failed to read ucontext_t from %s", filepath);
            close(fd);
            return -1;
        }
    } else if (header.ucontext_size > 0) {
        /* Skip ucontext if caller didn't supply pointer */
        lseek(fd, header.ucontext_size, SEEK_CUR);
    }
    
    uint8_t *heap_bytes = (uint8_t *)heap_base;
    uint32_t loaded = 0;
    
    for (uint32_t i = 0; i < header.dirty_page_count; i++) {
        uint32_t page_idx = 0;
        if (read(fd, &page_idx, sizeof(page_idx)) != sizeof(page_idx)) {
            LOG_E("RESTORE", "Failed reading page_idx at entry %u", i);
            close(fd);
            return -1;
        }
        
        if (page_idx * PAGE_SIZE_BYTES >= heap_size) {
            LOG_E("RESTORE", "Page index %u out of bounds for heap size %zu", page_idx, heap_size);
            close(fd);
            return -1;
        }
        
        uint8_t *dest = heap_bytes + (page_idx * PAGE_SIZE_BYTES);
        ssize_t n = read(fd, dest, PAGE_SIZE_BYTES);
        if (n != PAGE_SIZE_BYTES) {
            LOG_E("RESTORE", "Short read on page %u (%zd != %d)", page_idx, n, PAGE_SIZE_BYTES);
            close(fd);
            return -1;
        }
        loaded++;
    }
    
    close(fd);
    
    if (restored_pages) {
        *restored_pages = loaded;
    }
    
    LOG_I("RESTORE", "Successfully restored %u dirty pages from %s (target heap=%p)",
          loaded, filepath, heap_base);
    return 0;
}
