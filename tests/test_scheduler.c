#include "common.h"
#include "dpc_rr.h"
#include <assert.h>

void test_scheduler_priority_math(void) {
    LOG_I("TEST", "Running test_scheduler_priority_math...");
    
    dpc_scheduler_t sched;
    sched_init(&sched);
    
    int idx0 = sched_add_task(&sched, 1001, 0, "matrix", 10.0);
    int idx1 = sched_add_task(&sched, 1002, 1, "hash", 10.0);
    assert(idx0 >= 0 && idx1 >= 0);
    
    int64_t start_time = now_ns();
    sched.last_epoch_ns = start_time;
    
    /* Task 0 consumed 8ms CPU out of 10ms */
    sched.tasks[idx0].epoch_cpu_ns = 8LL * 1000000LL;
    sched.tasks[idx0].epoch_wait_ns = 2LL * 1000000LL;
    
    /* Task 1 waited 10ms */
    sched.tasks[idx1].epoch_cpu_ns = 0;
    sched.tasks[idx1].epoch_wait_ns = 10LL * 1000000LL;
    
    int64_t epoch_end = start_time + 10LL * 1000000LL;
    sched_recalculate_priorities(&sched, epoch_end);
    
    /* Task 1 should have higher priority due to wait boost */
    assert(sched.tasks[idx1].p_dyn > sched.tasks[idx0].p_dyn);
    
    LOG_I("TEST", "Priority math: Task 0 (CPU heavy) P_dyn=%.2f, Task 1 (waiting) P_dyn=%.2f -> PASSED",
          sched.tasks[idx0].p_dyn, sched.tasks[idx1].p_dyn);
}

void test_starvation_promotion(void) {
    LOG_I("TEST", "Running test_starvation_promotion...");
    
    dpc_scheduler_t sched;
    sched_init(&sched);
    
    int idx = sched_add_task(&sched, 2001, 0, "compute", 5.0);
    sched.tasks[idx].tier = TIER_2; /* Demoted to batch tier */
    sched.tasks[idx].state = TASK_STATE_STOPPED;
    /* Simulate task waiting longer than the starvation threshold */
    sched.tasks[idx].last_state_change_ns = now_ns() - (STARVATION_THRESHOLD_NS + 100000000LL);
    
    sched_recalculate_priorities(&sched, now_ns());
    
    /* Should have been auto-promoted to Tier 0 */
    assert(sched.tasks[idx].tier == TIER_0);
    assert(sched.tasks[idx].starvation_promotions == 1);
    
    LOG_I("TEST", "Starvation promotion from Tier 2 to Tier 0: PASSED");
}

int main(void) {
    LOG_I("TEST", "=== Starting DPC-RR Scheduler Unit Tests ===");
    test_scheduler_priority_math();
    test_starvation_promotion();
    LOG_I("TEST", "=== ALL SCHEDULER TESTS PASSED ===");
    return 0;
}
