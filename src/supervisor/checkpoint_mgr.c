#include "common.h"
#include "dpc_rr.h"
#include "ipc_bridge.h"
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

static char g_ckpt_dir[256] = "checkpoints";

int ckpt_mgr_init(const char *dir) {
    if (dir && dir[0] != '\0') {
        strncpy(g_ckpt_dir, dir, sizeof(g_ckpt_dir) - 1);
    }
    struct stat st;
    if (stat(g_ckpt_dir, &st) < 0) {
        if (mkdir(g_ckpt_dir, 0755) < 0 && errno != EEXIST) {
            LOG_E("CKPT_MGR", "Failed to create checkpoint directory %s: %s", g_ckpt_dir, strerror(errno));
            return -1;
        }
    }
    LOG_I("CKPT_MGR", "Checkpoint storage directory ready: %s", g_ckpt_dir);
    return 0;
}

void ckpt_mgr_trigger_worker(pid_t pid) {
    if (pid <= 0) return;
    LOG_D("CKPT_MGR", "Sending SIGUSR1 to trigger checkpoint on PID %d", pid);
    kill(pid, SIGUSR1);
}

int ckpt_mgr_get_latest(int worker_idx, char *out_path, size_t max_len) {
    DIR *d = opendir(g_ckpt_dir);
    if (!d) return -1;
    
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "worker_%d_", worker_idx);
    
    struct dirent *entry;
    time_t latest_mtime = 0;
    char best_file[512] = {0};
    
    while ((entry = readdir(d)) != NULL) {
        if (strncmp(entry->d_name, prefix, strlen(prefix)) == 0 &&
            strstr(entry->d_name, ".ckpt") != NULL &&
            strstr(entry->d_name, ".tmp") == NULL) {
            
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", g_ckpt_dir, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) == 0) {
                if (st.st_mtime > latest_mtime) {
                    latest_mtime = st.st_mtime;
                    strncpy(best_file, full_path, sizeof(best_file) - 1);
                }
            }
        }
    }
    closedir(d);
    
    if (best_file[0] != '\0') {
        strncpy(out_path, best_file, max_len - 1);
        return 0;
    }
    return -1;
}
