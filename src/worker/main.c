#include "common.h"
#include "dirty_tracker.h"
#include "checkpoint.h"
#include "workload.h"
#include "fault_inject.h"
#include <sys/mman.h>
#include <signal.h>
#include <getopt.h>

static dirty_tracker_t g_tracker;
static void *g_worker_heap = NULL;
static size_t g_worker_heap_size = 0;
static int g_worker_idx = 0;
static workload_kind_t g_workload_kind = WORKLOAD_MATRIX;
static volatile bool g_should_stop = false;
static char g_ckpt_dir[256] = "checkpoints";

static void sigusr1_ckpt_handler(int sig, siginfo_t *info, void *ucontext_raw) {
    (void)sig;
    (void)info;
    
    char ckpt_path[512];
    snprintf(ckpt_path, sizeof(ckpt_path), "%s/worker_%d_%d.ckpt", g_ckpt_dir, g_worker_idx, getpid());
    
    ucontext_t *uc = (ucontext_t *)ucontext_raw;
    if (ckpt_save_to_file(ckpt_path, uc, &g_tracker, getpid()) == 0) {
        dt_reset(&g_tracker);
        kill(getppid(), SIGUSR2);
    }
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_should_stop = true;
}

int main(int argc, char **argv) {
    size_t heap_mb = DEFAULT_HEAP_MB;
    char restore_path[512] = {0};
    
    static struct option long_options[] = {
        {"workload",    required_argument, 0, 'w'},
        {"heap-mb",     required_argument, 0, 'm'},
        {"worker-idx",  required_argument, 0, 'i'},
        {"restore",     required_argument, 0, 'r'},
        {"ckpt-dir",    required_argument, 0, 'd'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "w:m:i:r:d:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'w':
                if (strcmp(optarg, "matrix") == 0) g_workload_kind = WORKLOAD_MATRIX;
                else if (strcmp(optarg, "hash") == 0) g_workload_kind = WORKLOAD_HASH_RING;
                else g_workload_kind = WORKLOAD_MATRIX;
                break;
            case 'm':
                heap_mb = (size_t)atoi(optarg);
                if (heap_mb < 64) heap_mb = 64;
                if (heap_mb > 1024) heap_mb = 1024;
                break;
            case 'i':
                g_worker_idx = atoi(optarg);
                break;
            case 'r':
                strncpy(restore_path, optarg, sizeof(restore_path) - 1);
                break;
            case 'd':
                strncpy(g_ckpt_dir, optarg, sizeof(g_ckpt_dir) - 1);
                break;
            case 'h':
            default:
                fprintf(stderr, "Usage: %s [--workload matrix|hash] [--heap-mb <MB>] [--worker-idx <N>] [--restore <ckpt>] [--ckpt-dir <dir>]\n", argv[0]);
                return EXIT_SUCCESS;
        }
    }
    
    g_worker_heap_size = heap_mb * 1024 * 1024;
    
    LOG_I("WORKER", "Starting worker %d (PID %d, workload=%s, heap=%zu MB)",
          g_worker_idx, getpid(),
          g_workload_kind == WORKLOAD_MATRIX ? "Matrix Cascade" : "Hash Ring",
          heap_mb);
    
    /* Allocate heap space */
    g_worker_heap = mmap(NULL, g_worker_heap_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g_worker_heap == MAP_FAILED) {
        LOG_F("WORKER", "mmap failed for %zu MB heap: %s", heap_mb, strerror(errno));
        return EXIT_FAILURE;
    }
    
    /* Setup dirty tracker */
    if (dt_init(&g_tracker, g_worker_heap, g_worker_heap_size) < 0) {
        LOG_F("WORKER", "Failed to initialize dirty tracker");
        return EXIT_FAILURE;
    }
    
    /* Workload context */
    workload_ctx_t wctx;
    memset(&wctx, 0, sizeof(wctx));
    wctx.kind = g_workload_kind;
    wctx.heap_base = g_worker_heap;
    wctx.heap_size = g_worker_heap_size;
    wctx.should_stop = &g_should_stop;
    wctx.iterations = 0;
    
    /* If restore requested, load previous dirty pages */
    if (restore_path[0] != '\0') {
        uint32_t restored_pages = 0;
        LOG_I("WORKER", "Restoring state from %s ...", restore_path);
        if (ckpt_load_from_file(restore_path, NULL, g_worker_heap, g_worker_heap_size, &restored_pages) < 0) {
            LOG_E("WORKER", "Restore failed, re-initializing fresh state");
            if (g_workload_kind == WORKLOAD_MATRIX) workload_matrix_init(&wctx);
            else workload_hash_init(&wctx);
        } else {
            LOG_I("WORKER", "State restored successfully (%u pages)", restored_pages);
        }
    } else {
        if (g_workload_kind == WORKLOAD_MATRIX) workload_matrix_init(&wctx);
        else workload_hash_init(&wctx);
    }
    
    /* Install SIGSEGV handler and protect heap with PROT_READ */
    if (dt_install_handler(&g_tracker) < 0 || dt_protect_heap(&g_tracker) < 0) {
        LOG_F("WORKER", "Failed to setup memory trap");
        return EXIT_FAILURE;
    }
    
    /* Setup SIGUSR1 handler for checkpoints */
    struct sigaction sa_usr1;
    memset(&sa_usr1, 0, sizeof(sa_usr1));
    sa_usr1.sa_sigaction = sigusr1_ckpt_handler;
    sa_usr1.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa_usr1.sa_mask);
    sigaction(SIGUSR1, &sa_usr1, NULL);
    
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);
    
    /* Start chaos injection socket listener */
    chaos_server_start(g_worker_heap, g_worker_heap_size);
    
    LOG_I("WORKER", "Worker %d ready. Entering compute loop...", g_worker_idx);
    
    while (!g_should_stop) {
        if (g_workload_kind == WORKLOAD_MATRIX) {
            workload_matrix_step(&wctx);
        } else {
            workload_hash_step(&wctx);
        }
        usleep(500);
    }
    
    chaos_server_stop();
    dt_cleanup(&g_tracker);
    if (g_worker_heap) {
        munmap(g_worker_heap, g_worker_heap_size);
    }
    
    return EXIT_SUCCESS;
}
