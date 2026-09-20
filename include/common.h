#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

#define PAGE_SIZE_BYTES       4096
#define MAX_WORKERS           8
#define DEFAULT_HEAP_MB       512
#define MAX_HEAP_PAGES        131072    /* 512 MB / 4 KB = 131,072 pages */
#define HEATMAP_BUCKETS       4096      /* 131072 / 32 = 4096 coarse blocks */
#define UNLOCK_BLOCK_PAGES    16        /* 64 KB block unlock per fault mitigation */

#define SHM_NAME              "/wsl_telemetry"
#define SEM_NAME              "/wsl_telem_sem"
#define CHAOS_SOCK_PATH       "/tmp/wsl_chaos.sock"
#define CKPT_MAGIC            0x434B5054  /* 'CKPT' */
#define CKPT_VERSION          1

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

void log_msg(log_level_t level, const char *component, const char *fmt, ...);

#define LOG_D(comp, fmt, ...) log_msg(LOG_DEBUG, comp, fmt, ##__VA_ARGS__)
#define LOG_I(comp, fmt, ...) log_msg(LOG_INFO, comp, fmt, ##__VA_ARGS__)
#define LOG_W(comp, fmt, ...) log_msg(LOG_WARN, comp, fmt, ##__VA_ARGS__)
#define LOG_E(comp, fmt, ...) log_msg(LOG_ERROR, comp, fmt, ##__VA_ARGS__)
#define LOG_F(comp, fmt, ...) log_msg(LOG_FATAL, comp, fmt, ##__VA_ARGS__)

#define DIE_ON_ERR(call, msg) do { \
    if ((call) < 0) { \
        LOG_F("FATAL", "%s: %s (errno=%d)", msg, strerror(errno), errno); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

int64_t now_ns(void);
int64_t elapsed_ns(int64_t start_ns);
void timespec_add_ms(struct timespec *ts, int ms);

#endif /* COMMON_H */
