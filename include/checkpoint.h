#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include "common.h"
#include "dirty_tracker.h"
#include <ucontext.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           /* CKPT_MAGIC = 0x434B5054 ('CKPT') */
    uint32_t version;         /* CKPT_VERSION = 1 */
    int64_t timestamp_ns;
    uint32_t pid;
    uint64_t heap_base_addr;
    uint64_t heap_size_bytes;
    uint32_t ucontext_size;
    uint32_t dirty_page_count;
    uint32_t total_pages;
} ckpt_header_t;

typedef struct {
    uint32_t page_idx;
    uint8_t data[PAGE_SIZE_BYTES];
} ckpt_page_entry_t;
#pragma pack(pop)

int ckpt_save_to_file(const char *filepath, const ucontext_t *uc, const dirty_tracker_t *dt, pid_t pid);
int ckpt_load_from_file(const char *filepath, ucontext_t *uc, void *heap_base, size_t heap_size, uint32_t *restored_pages);

#endif /* CHECKPOINT_H */
