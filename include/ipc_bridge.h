#ifndef IPC_BRIDGE_H
#define IPC_BRIDGE_H

#include "common.h"

#define GANTT_RING_SIZE 512

typedef enum {
    EVENT_NONE = 0,
    EVENT_SPAWN,
    EVENT_SCHEDULE,
    EVENT_PREEMPT,
    EVENT_PROMOTE,
    EVENT_DEMOTE,
    EVENT_CHECKPOINT_START,
    EVENT_CHECKPOINT_DONE,
    EVENT_CRASH,
    EVENT_RESTORE_START,
    EVENT_RESTORE_DONE
} gantt_event_type_t;

#pragma pack(push, 1)

typedef struct {
    int64_t timestamp_ns;
    uint32_t pid;
    uint8_t worker_idx;
    uint8_t event_type;
    uint8_t from_tier;
    uint8_t to_tier;
    char details[24];
} gantt_event_t;

typedef struct {
    uint32_t pid;
    uint8_t active;
    uint8_t tier;
    uint8_t state;
    uint8_t pad;
    
    double p_dyn;
    double p_base;
    
    int64_t cpu_ns;
    int64_t wait_ns;
    
    uint32_t dirty_pages;
    uint32_t total_faults;
    uint32_t checkpoint_count;
    uint32_t restore_count;
    
    int64_t last_ckpt_duration_ns;
    int64_t last_restore_duration_ns;
    
    char workload_name[32];
    uint8_t heap_heatmap[HEATMAP_BUCKETS]; /* 4096 coarse blocks */
} worker_telemetry_t;

typedef struct {
    uint64_t epoch;
    uint32_t num_workers;
    uint32_t pad;
    
    int64_t supervisor_uptime_ns;
    int64_t last_rto_ns;
    int64_t last_rpo_ns;
    double checkpoint_overhead_pct;
    
    worker_telemetry_t workers[MAX_WORKERS];
    
    uint32_t gantt_head;
    uint32_t gantt_count;
    gantt_event_t gantt_ring[GANTT_RING_SIZE];
} telemetry_shm_t;

#pragma pack(pop)

int ipc_shm_init_master(telemetry_shm_t **out_shm);
int ipc_shm_open_client(telemetry_shm_t **out_shm, int *out_fd);
void ipc_shm_close_client(telemetry_shm_t *shm, int fd);
void ipc_shm_cleanup_master(telemetry_shm_t *shm);

void ipc_push_gantt_event(telemetry_shm_t *shm, uint32_t pid, uint8_t worker_idx,
                          gantt_event_type_t type, uint8_t from_tier, uint8_t to_tier, const char *details);

#endif /* IPC_BRIDGE_H */
