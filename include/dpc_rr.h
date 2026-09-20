#ifndef DPC_RR_H
#define DPC_RR_H

#include "common.h"

typedef enum {
    TIER_0 = 0, /* Interactive: 100 ms (scaled for human-observable UI) */
    TIER_1 = 1, /* Balanced: 250 ms */
    TIER_2 = 2, /* Batch/Compute: 600 ms */
    NUM_TIERS = 3
} sched_tier_t;

typedef enum {
    TASK_STATE_IDLE = 0,
    TASK_STATE_RUNNING,
    TASK_STATE_STOPPED,
    TASK_STATE_CHECKPOINTING,
    TASK_STATE_DEAD
} task_state_t;

/* Human-observable quantum intervals (100ms, 250ms, 600ms) */
#define QUANTUM_TIER_0_MS 100
#define QUANTUM_TIER_1_MS 250
#define QUANTUM_TIER_2_MS 600

#define DPC_ALPHA                 1.5   /* CPU consumption penalty */
#define DPC_BETA                  2.0   /* Starvation boost weight */
#define STARVATION_THRESHOLD_NS   (500LL * 1000000LL) /* 500 ms for observable promotion */

typedef struct task_entry {
    pid_t pid;
    int worker_idx;
    sched_tier_t tier;
    task_state_t state;
    
    double p_base;
    double p_dyn;
    
    int64_t total_cpu_ns;
    int64_t total_wait_ns;
    
    int64_t epoch_cpu_ns;
    int64_t epoch_wait_ns;
    int64_t last_state_change_ns;
    int64_t last_scheduled_ns;
    
    uint32_t quantum_exhaustions;
    uint32_t starvation_promotions;
    
    char workload_name[32];
    bool active;
} task_entry_t;

typedef struct {
    task_entry_t tasks[MAX_WORKERS];
    int num_tasks;
    int current_running_idx;
    int64_t last_epoch_ns;
} dpc_scheduler_t;

void sched_init(dpc_scheduler_t *sched);
int sched_add_task(dpc_scheduler_t *sched, pid_t pid, int worker_idx, const char *workload_name, double p_base);
void sched_remove_task(dpc_scheduler_t *sched, pid_t pid);
task_entry_t *sched_find_task(dpc_scheduler_t *sched, pid_t pid);

void sched_recalculate_priorities(dpc_scheduler_t *sched, int64_t now);
int sched_pick_next_task(dpc_scheduler_t *sched);
int sched_get_tier_quantum_ms(sched_tier_t tier);

#endif /* DPC_RR_H */
