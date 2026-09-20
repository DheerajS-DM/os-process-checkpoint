#include "common.h"
#include "dpc_rr.h"
#include "ipc_bridge.h"
#include "checkpoint.h"
#include <signal.h>
#include <getopt.h>
#include <sys/wait.h>

extern pid_t process_spawn_worker(int worker_idx, const char *workload, size_t heap_mb, const char *ckpt_path);
extern int process_check_children(pid_t *dead_pid, int *exit_code, bool *was_signaled);
extern int ckpt_mgr_init(const char *dir);
extern void ckpt_mgr_trigger_worker(pid_t pid);
extern int ckpt_mgr_get_latest(int worker_idx, char *out_path, size_t max_len);

static dpc_scheduler_t g_sched;
static telemetry_shm_t *g_shm = NULL;
static volatile bool g_running = true;
static int g_num_workers = 4;
static size_t g_heap_mb = 256;
static int64_t g_ckpt_interval_ns = 4000LL * 1000000LL; /* 4s checkpoint */
static int64_t g_last_ckpt_time_ns = 0;
static int64_t g_supervisor_start_ns = 0;

static void sigterm_handler(int sig) {
    (void)sig;
    g_running = false;
}

static void sigusr2_ckpt_done_handler(int sig) {
    (void)sig;
    LOG_I("SUPERVISOR", "Received SIGUSR2: worker checkpoint completed");
    if (g_shm) {
        g_shm->last_rpo_ns = now_ns() - g_last_ckpt_time_ns;
        
        /* Reset dirty page maps on checkpoint completion */
        for (int i = 0; i < MAX_WORKERS; i++) {
            g_shm->workers[i].dirty_pages = 0;
            memset(g_shm->workers[i].heap_heatmap, 0, sizeof(g_shm->workers[i].heap_heatmap));
        }
    }
}

static void update_telemetry_shm(int64_t now) {
    if (!g_shm) return;
    
    g_shm->supervisor_uptime_ns = now - g_supervisor_start_ns;
    g_shm->num_workers = g_num_workers;
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        task_entry_t *t = &g_sched.tasks[i];
        worker_telemetry_t *wt = &g_shm->workers[i];
        
        if (t->active) {
            wt->pid = t->pid;
            wt->active = 1;
            wt->tier = (uint8_t)t->tier;
            wt->state = (uint8_t)t->state;
            wt->p_dyn = t->p_dyn;
            wt->p_base = t->p_base;
            wt->cpu_ns = t->total_cpu_ns;
            wt->wait_ns = t->total_wait_ns;
            strncpy(wt->workload_name, t->workload_name, sizeof(wt->workload_name) - 1);
            
            /* Linear write head progression while running */
            if (t->state == TASK_STATE_RUNNING) {
                int64_t cycle = (now / 40000000LL);
                int head_pos = (int)((cycle * (i + 1) * 7) % HEATMAP_BUCKETS);
                
                /* Mark block active */
                for (int k = 0; k < 32; k++) {
                    int pos = (head_pos + k) % HEATMAP_BUCKETS;
                    wt->heap_heatmap[pos] = 255;
                }
                
                /* Count active dirty blocks */
                uint32_t active_count = 0;
                for (int b = 0; b < HEATMAP_BUCKETS; b++) {
                    if (wt->heap_heatmap[b] > 0) active_count++;
                }
                wt->dirty_pages = active_count * UNLOCK_BLOCK_PAGES;
                wt->total_faults += 4;
            }
        } else {
            wt->active = 0;
        }
    }
    
    __atomic_fetch_add(&g_shm->epoch, 1, __ATOMIC_RELEASE);
}

int main(int argc, char **argv) {
    char workloads_arg[128] = "matrix,hash,matrix,hash";
    int ckpt_interval_ms = 4000;
    
    static struct option long_options[] = {
        {"workers",            required_argument, 0, 'w'},
        {"workloads",          required_argument, 0, 'l'},
        {"heap-mb",            required_argument, 0, 'm'},
        {"checkpoint-interval",required_argument, 0, 'c'},
        {"help",               no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "w:l:m:c:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'w': g_num_workers = atoi(optarg); break;
            case 'l': strncpy(workloads_arg, optarg, sizeof(workloads_arg) - 1); break;
            case 'm': g_heap_mb = (size_t)atoi(optarg); break;
            case 'c': ckpt_interval_ms = atoi(optarg); break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [--workers <N>] [--workloads <matrix,hash,...>] [--heap-mb <MB>] [--checkpoint-interval <ms>]\n", argv[0]);
                return EXIT_SUCCESS;
        }
    }
    
    if (g_num_workers < 1) g_num_workers = 1;
    if (g_num_workers > MAX_WORKERS) g_num_workers = MAX_WORKERS;
    g_ckpt_interval_ns = (int64_t)ckpt_interval_ms * 1000000LL;
    g_supervisor_start_ns = now_ns();
    g_last_ckpt_time_ns = g_supervisor_start_ns;
    
    LOG_I("SUPERVISOR", "=== Starting DPC-RR Process Recovery Supervisor ===");
    LOG_I("SUPERVISOR", "Configuration: workers=%d, heap_per_worker=%zu MB, total_heap=%zu MB, ckpt_interval=%d ms",
          g_num_workers, g_heap_mb, g_num_workers * g_heap_mb, ckpt_interval_ms);
    
    signal(SIGINT, sigterm_handler);
    signal(SIGTERM, sigterm_handler);
    
    struct sigaction sa_usr2;
    memset(&sa_usr2, 0, sizeof(sa_usr2));
    sa_usr2.sa_handler = sigusr2_ckpt_done_handler;
    sigaction(SIGUSR2, &sa_usr2, NULL);
    
    if (ipc_shm_init_master(&g_shm) < 0) {
        LOG_F("SUPERVISOR", "Failed to initialize shared memory");
        return EXIT_FAILURE;
    }
    
    ckpt_mgr_init("checkpoints");
    sched_init(&g_sched);
    
    char *token = strtok(workloads_arg, ",");
    for (int i = 0; i < g_num_workers; i++) {
        const char *wl = token ? token : (i % 2 == 0 ? "matrix" : "hash");
        pid_t pid = process_spawn_worker(i, wl, g_heap_mb, NULL);
        if (pid > 0) {
            usleep(10000);
            kill(pid, SIGSTOP);
            
            double p_base = 10.0 - (double)i * 1.5;
            sched_add_task(&g_sched, pid, i, wl, p_base);
            
            task_entry_t *t = sched_find_task(&g_sched, pid);
            if (t) t->state = TASK_STATE_STOPPED;
            
            ipc_push_gantt_event(g_shm, pid, i, EVENT_SPAWN, 0, 0, "Spawned & Paused");
        }
        if (token) token = strtok(NULL, ",");
    }
    
    int current_task_idx = -1;
    int64_t current_quantum_start_ns = now_ns();
    int current_quantum_ms = QUANTUM_TIER_0_MS;
    
    while (g_running) {
        int64_t now = now_ns();
        
        /* 1. Check for dead children */
        pid_t dead_pid = 0;
        int exit_code = 0;
        bool was_signaled = false;
        
        while (process_check_children(&dead_pid, &exit_code, &was_signaled) > 0) {
            int64_t crash_time_ns = now_ns();
            LOG_E("SUPERVISOR", "Worker PID %d terminated (code=%d, signaled=%d)",
                  dead_pid, exit_code, was_signaled);
            
            task_entry_t *dead_task = sched_find_task(&g_sched, dead_pid);
            if (dead_task) {
                int worker_idx = dead_task->worker_idx;
                char workload_name[32];
                strncpy(workload_name, dead_task->workload_name, sizeof(workload_name) - 1);
                workload_name[sizeof(workload_name) - 1] = '\0';
                double p_base = dead_task->p_base;
                
                ipc_push_gantt_event(g_shm, dead_pid, worker_idx, EVENT_CRASH, dead_task->tier, 0, "Crash Event");
                sched_remove_task(&g_sched, dead_pid);
                if (current_task_idx == worker_idx) {
                    current_task_idx = -1;
                }
                
                char latest_ckpt[512] = {0};
                bool has_ckpt = (ckpt_mgr_get_latest(worker_idx, latest_ckpt, sizeof(latest_ckpt)) == 0);
                
                ipc_push_gantt_event(g_shm, 0, worker_idx, EVENT_RESTORE_START, 0, 0, "Restoring Process");
                
                pid_t new_pid = process_spawn_worker(worker_idx, workload_name, g_heap_mb, has_ckpt ? latest_ckpt : NULL);
                if (new_pid > 0) {
                    usleep(10000);
                    kill(new_pid, SIGSTOP);
                    
                    sched_add_task(&g_sched, new_pid, worker_idx, workload_name, p_base);
                    task_entry_t *nt = sched_find_task(&g_sched, new_pid);
                    if (nt) nt->state = TASK_STATE_STOPPED;
                    
                    int64_t rto_ns = now_ns() - crash_time_ns;
                    g_shm->last_rto_ns = rto_ns;
                    LOG_I("SUPERVISOR", "Restoration complete: worker %d new PID=%d, RTO=%.2f ms",
                          worker_idx, new_pid, (double)rto_ns / 1000000.0);
                    ipc_push_gantt_event(g_shm, new_pid, worker_idx, EVENT_RESTORE_DONE, 0, 0, "Restored & Ready");
                }
            }
        }
        
        /* 2. Quantum preemption */
        int64_t time_in_quantum_ms = (now - current_quantum_start_ns) / 1000000LL;
        
        if (current_task_idx >= 0 && time_in_quantum_ms >= current_quantum_ms) {
            task_entry_t *cur = &g_sched.tasks[current_task_idx];
            if (cur->active && cur->state == TASK_STATE_RUNNING) {
                kill(cur->pid, SIGSTOP);
                cur->state = TASK_STATE_STOPPED;
                cur->last_state_change_ns = now;
                cur->total_cpu_ns += (now - current_quantum_start_ns);
                cur->epoch_cpu_ns += (now - current_quantum_start_ns);
                
                sched_tier_t old_tier = cur->tier;
                if (cur->tier < TIER_2) {
                    cur->tier++;
                    cur->quantum_exhaustions++;
                    ipc_push_gantt_event(g_shm, cur->pid, cur->worker_idx, EVENT_DEMOTE, old_tier, cur->tier, "Demoted");
                }
            }
            current_task_idx = -1;
        }
        
        /* 3. Periodic checkpoint & dirty page flush */
        if (now - g_last_ckpt_time_ns >= g_ckpt_interval_ns) {
            LOG_I("SUPERVISOR", "Triggering periodic checkpoint across active workers");
            for (int i = 0; i < MAX_WORKERS; i++) {
                task_entry_t *t = &g_sched.tasks[i];
                if (t->active && t->state != TASK_STATE_DEAD) {
                    ckpt_mgr_trigger_worker(t->pid);
                    ipc_push_gantt_event(g_shm, t->pid, t->worker_idx, EVENT_CHECKPOINT_START, t->tier, t->tier, "Checkpoint Taken");
                }
            }
            g_last_ckpt_time_ns = now;
        }
        
        /* 4. Update wait times */
        for (int i = 0; i < MAX_WORKERS; i++) {
            task_entry_t *t = &g_sched.tasks[i];
            if (t->active && t->state == TASK_STATE_STOPPED) {
                t->total_wait_ns += 5000000LL;
                t->epoch_wait_ns += 5000000LL;
            }
        }
        
        /* 5. Recalculate priorities */
        sched_recalculate_priorities(&g_sched, now);
        
        /* 6. Dispatch next task */
        if (current_task_idx < 0) {
            int next_idx = sched_pick_next_task(&g_sched);
            if (next_idx >= 0) {
                current_task_idx = next_idx;
                task_entry_t *next_t = &g_sched.tasks[next_idx];
                
                next_t->state = TASK_STATE_RUNNING;
                next_t->last_scheduled_ns = now;
                next_t->last_state_change_ns = now;
                current_quantum_ms = sched_get_tier_quantum_ms(next_t->tier);
                current_quantum_start_ns = now;
                
                kill(next_t->pid, SIGCONT);
            }
        }
        
        /* 7. Update Telemetry */
        update_telemetry_shm(now);
        usleep(5000);
    }
    
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (g_sched.tasks[i].active) {
            kill(g_sched.tasks[i].pid, SIGTERM);
        }
    }
    
    while (waitpid(-1, NULL, 0) > 0);
    ipc_shm_cleanup_master(g_shm);
    return EXIT_SUCCESS;
}
