#include "dpc_rr.h"

void sched_init(dpc_scheduler_t *sched) {
    if (!sched) return;
    memset(sched, 0, sizeof(*sched));
    sched->current_running_idx = -1;
    sched->last_epoch_ns = now_ns();
}

int sched_add_task(dpc_scheduler_t *sched, pid_t pid, int worker_idx, const char *workload_name, double p_base) {
    if (!sched || sched->num_tasks >= MAX_WORKERS) return -1;
    
    int idx = -1;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (!sched->tasks[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return -1;
    
    task_entry_t *t = &sched->tasks[idx];
    memset(t, 0, sizeof(*t));
    t->pid = pid;
    t->worker_idx = worker_idx;
    t->tier = TIER_0;
    t->state = TASK_STATE_IDLE;
    t->p_base = p_base;
    t->p_dyn = p_base;
    t->active = true;
    t->last_state_change_ns = now_ns();
    if (workload_name) {
        strncpy(t->workload_name, workload_name, sizeof(t->workload_name) - 1);
    }
    
    sched->num_tasks++;
    LOG_I("SCHED", "Enqueued task worker_idx=%d PID=%d (Tier=0, P_base=%.2f, workload=%s)",
          worker_idx, pid, p_base, t->workload_name);
    return idx;
}

void sched_remove_task(dpc_scheduler_t *sched, pid_t pid) {
    if (!sched) return;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (sched->tasks[i].active && sched->tasks[i].pid == pid) {
            sched->tasks[i].active = false;
            sched->tasks[i].state = TASK_STATE_DEAD;
            sched->num_tasks--;
            if (sched->current_running_idx == i) {
                sched->current_running_idx = -1;
            }
            LOG_I("SCHED", "Removed task PID=%d from scheduler", pid);
            break;
        }
    }
}

task_entry_t *sched_find_task(dpc_scheduler_t *sched, pid_t pid) {
    if (!sched) return NULL;
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (sched->tasks[i].active && sched->tasks[i].pid == pid) {
            return &sched->tasks[i];
        }
    }
    return NULL;
}

int sched_get_tier_quantum_ms(sched_tier_t tier) {
    switch (tier) {
        case TIER_0: return QUANTUM_TIER_0_MS; /* 10 ms */
        case TIER_1: return QUANTUM_TIER_1_MS; /* 25 ms */
        case TIER_2: return QUANTUM_TIER_2_MS; /* 60 ms */
        default:     return QUANTUM_TIER_1_MS;
    }
}

void sched_recalculate_priorities(dpc_scheduler_t *sched, int64_t now) {
    if (!sched) return;
    
    int64_t delta_t = now - sched->last_epoch_ns;
    if (delta_t <= 0) delta_t = 1000000; /* 1 ms safety guard */
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        task_entry_t *t = &sched->tasks[i];
        if (!t->active || t->state == TASK_STATE_DEAD) continue;
        
        /* Starvation check: if task waited longer than 150 ms, promote to Tier 0 */
        int64_t current_wait = (t->state == TASK_STATE_STOPPED) ? (now - t->last_state_change_ns) : 0;
        if (current_wait >= STARVATION_THRESHOLD_NS && t->tier > TIER_0) {
            LOG_W("SCHED", "Starvation detected for PID %d (wait=%ld ms) -> Promoting to Tier 0",
                  t->pid, current_wait / 1000000);
            t->tier = TIER_0;
            t->starvation_promotions++;
        }
        
        /* Calculate dynamic priority ratio */
        double cpu_ratio = (double)t->epoch_cpu_ns / (double)delta_t;
        double wait_ratio = (double)t->epoch_wait_ns / (double)delta_t;
        
        t->p_dyn = t->p_base - (DPC_ALPHA * cpu_ratio) + (DPC_BETA * wait_ratio);
        
        /* Reset epoch counters */
        t->epoch_cpu_ns = 0;
        t->epoch_wait_ns = 0;
    }
    
    sched->last_epoch_ns = now;
}

int sched_pick_next_task(dpc_scheduler_t *sched) {
    if (!sched || sched->num_tasks == 0) return -1;
    
    int best_idx = -1;
    sched_tier_t best_tier = NUM_TIERS;
    double highest_priority = -1e9;
    
    /* Strict tier priority: Tier 0 > Tier 1 > Tier 2. Within tier: highest p_dyn */
    for (int i = 0; i < MAX_WORKERS; i++) {
        task_entry_t *t = &sched->tasks[i];
        if (!t->active || t->state == TASK_STATE_DEAD) continue;
        
        if (t->tier < best_tier) {
            best_tier = t->tier;
            highest_priority = t->p_dyn;
            best_idx = i;
        } else if (t->tier == best_tier) {
            if (t->p_dyn > highest_priority) {
                highest_priority = t->p_dyn;
                best_idx = i;
            }
        }
    }
    
    return best_idx;
}
