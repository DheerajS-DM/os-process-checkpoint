#include "ipc_bridge.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>

static sem_t *g_sem = NULL;

int ipc_shm_init_master(telemetry_shm_t **out_shm) {
    if (!out_shm) return -1;
    
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_NAME);
    
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd < 0) {
        LOG_E("SHM", "shm_open failed: %s", strerror(errno));
        return -1;
    }
    
    size_t size = sizeof(telemetry_shm_t);
    if (ftruncate(fd, size) < 0) {
        LOG_E("SHM", "ftruncate failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    
    telemetry_shm_t *shm = (telemetry_shm_t *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (shm == MAP_FAILED) {
        LOG_E("SHM", "mmap failed: %s", strerror(errno));
        return -1;
    }
    
    memset(shm, 0, size);
    shm->epoch = 1;
    
    g_sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (g_sem == SEM_FAILED) {
        LOG_W("SHM", "sem_open failed: %s, falling back to epoch-based synchronization", strerror(errno));
        g_sem = NULL;
    }
    
    *out_shm = shm;
    LOG_I("SHM", "Initialized POSIX shared memory bridge %s (size=%zu KB)", SHM_NAME, size / 1024);
    return 0;
}

int ipc_shm_open_client(telemetry_shm_t **out_shm, int *out_fd) {
    if (!out_shm) return -1;
    
    int fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (fd < 0) return -1;
    
    size_t size = sizeof(telemetry_shm_t);
    telemetry_shm_t *shm = (telemetry_shm_t *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        close(fd);
        return -1;
    }
    
    if (out_fd) *out_fd = fd;
    else close(fd);
    
    *out_shm = shm;
    return 0;
}

void ipc_shm_close_client(telemetry_shm_t *shm, int fd) {
    if (shm && shm != MAP_FAILED) {
        munmap(shm, sizeof(telemetry_shm_t));
    }
    if (fd >= 0) {
        close(fd);
    }
}

void ipc_shm_cleanup_master(telemetry_shm_t *shm) {
    if (shm && shm != MAP_FAILED) {
        munmap(shm, sizeof(telemetry_shm_t));
    }
    shm_unlink(SHM_NAME);
    if (g_sem && g_sem != SEM_FAILED) {
        sem_close(g_sem);
        sem_unlink(SEM_NAME);
        g_sem = NULL;
    }
    LOG_I("SHM", "Cleaned up shared memory bridge");
}

void ipc_push_gantt_event(telemetry_shm_t *shm, uint32_t pid, uint8_t worker_idx,
                          gantt_event_type_t type, uint8_t from_tier, uint8_t to_tier, const char *details) {
    if (!shm) return;
    
    uint32_t idx = shm->gantt_head % GANTT_RING_SIZE;
    gantt_event_t *ev = &shm->gantt_ring[idx];
    
    ev->timestamp_ns = now_ns();
    ev->pid = pid;
    ev->worker_idx = worker_idx;
    ev->event_type = (uint8_t)type;
    ev->from_tier = from_tier;
    ev->to_tier = to_tier;
    if (details) {
        strncpy(ev->details, details, sizeof(ev->details) - 1);
        ev->details[sizeof(ev->details) - 1] = '\0';
    } else {
        ev->details[0] = '\0';
    }
    
    shm->gantt_head = (shm->gantt_head + 1) % GANTT_RING_SIZE;
    if (shm->gantt_count < GANTT_RING_SIZE) {
        shm->gantt_count++;
    }
    
    /* Bump epoch to notify readers of update */
    __atomic_fetch_add(&shm->epoch, 1, __ATOMIC_RELEASE);
}
