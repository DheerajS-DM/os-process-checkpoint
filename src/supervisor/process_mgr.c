#include "common.h"
#include "ipc_bridge.h"
#include <sys/wait.h>

pid_t process_spawn_worker(int worker_idx, const char *workload, size_t heap_mb, const char *ckpt_path) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_E("PROC_MGR", "fork failed for worker %d: %s", worker_idx, strerror(errno));
        return -1;
    }
    
    if (pid == 0) {
        /* Child process: exec worker binary */
        char idx_str[16];
        char heap_str[16];
        snprintf(idx_str, sizeof(idx_str), "%d", worker_idx);
        snprintf(heap_str, sizeof(heap_str), "%zu", heap_mb);
        
        char *args[16];
        int arg_idx = 0;
        args[arg_idx++] = "./build/worker";
        args[arg_idx++] = "--worker-idx";
        args[arg_idx++] = idx_str;
        args[arg_idx++] = "--workload";
        args[arg_idx++] = (char *)(workload ? workload : "matrix");
        args[arg_idx++] = "--heap-mb";
        args[arg_idx++] = heap_str;
        
        if (ckpt_path && ckpt_path[0] != '\0') {
            args[arg_idx++] = "--restore";
            args[arg_idx++] = (char *)ckpt_path;
        }
        
        args[arg_idx] = NULL;
        
        execv(args[0], args);
        
        /* If execv fails */
        fprintf(stderr, "FATAL: execv failed for %s: %s\n", args[0], strerror(errno));
        _exit(EXIT_FAILURE);
    }
    
    LOG_I("PROC_MGR", "Spawned worker %d (PID %d, workload=%s, heap=%zu MB%s%s)",
          worker_idx, pid, workload ? workload : "matrix", heap_mb,
          ckpt_path ? ", restored from: " : "", ckpt_path ? ckpt_path : "");
    
    return pid;
}

int process_check_children(pid_t *dead_pid, int *exit_code, bool *was_signaled) {
    int status;
    pid_t p = waitpid(-1, &status, WNOHANG);
    if (p > 0) {
        if (dead_pid) *dead_pid = p;
        if (WIFEXITED(status)) {
            if (exit_code) *exit_code = WEXITSTATUS(status);
            if (was_signaled) *was_signaled = false;
        } else if (WIFSIGNALED(status)) {
            if (exit_code) *exit_code = WTERMSIG(status);
            if (was_signaled) *was_signaled = true;
        }
        return 1;
    }
    return (p == 0) ? 0 : -1;
}
