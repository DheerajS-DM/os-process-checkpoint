#include "fault_inject.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <signal.h>

static pthread_t g_chaos_thread;
static volatile bool g_chaos_running = false;
static int g_chaos_server_fd = -1;
static void *g_chaos_heap_base = NULL;
static size_t g_chaos_heap_size = 0;

static void handle_chaos_cmd(const chaos_request_t *req, chaos_response_t *resp) {
    memset(resp, 0, sizeof(*resp));
    
    if (req->target_pid != 0 && req->target_pid != (uint32_t)getpid()) {
        resp->success = 0;
        snprintf(resp->message, sizeof(resp->message), "PID mismatch (target=%u, my=%d)", req->target_pid, getpid());
        return;
    }
    
    LOG_W("CHAOS", "Executing chaos command %u on PID %d (reason: %s)",
          req->cmd_type, getpid(), req->reason);
    
    resp->success = 1;
    snprintf(resp->message, sizeof(resp->message), "Command %u acknowledged", req->cmd_type);
    
    switch (req->cmd_type) {
        case CHAOS_CMD_OOB_WRITE: {
            LOG_F("CHAOS", "Simulating Out-Of-Bounds crash via NULL dereference...");
            volatile int *null_ptr = (volatile int *)0x0;
            *null_ptr = 0xDEADBEEF; /* Trapped as out-of-bounds -> real SIGSEGV fatal crash */
            break;
        }
        case CHAOS_CMD_SIGKILL: {
            LOG_F("CHAOS", "Simulating hard kernel crash via SIGKILL...");
            kill(getpid(), SIGKILL);
            break;
        }
        case CHAOS_CMD_CORRUPT_HEAP: {
            LOG_W("CHAOS", "Corrupting 16 random heap locations...");
            if (g_chaos_heap_base && g_chaos_heap_size > 0) {
                uint8_t *heap = (uint8_t *)g_chaos_heap_base;
                for (int i = 0; i < 16; i++) {
                    size_t idx = rand() % g_chaos_heap_size;
                    heap[idx] ^= 0xFF;
                }
            }
            break;
        }
        case CHAOS_CMD_HANG: {
            LOG_W("CHAOS", "Entering artificial infinite hang loop...");
            while (1) {
                /* Non-yielding CPU burn */
            }
            break;
        }
        default:
            resp->success = 0;
            snprintf(resp->message, sizeof(resp->message), "Unknown chaos command %u", req->cmd_type);
            break;
    }
}

static void *chaos_thread_worker(void *arg) {
    (void)arg;
    while (g_chaos_running) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(g_chaos_server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!g_chaos_running) break;
            usleep(10000);
            continue;
        }
        
        chaos_request_t req;
        ssize_t n = read(client_fd, &req, sizeof(req));
        if (n == sizeof(req)) {
            chaos_response_t resp;
            handle_chaos_cmd(&req, &resp);
            if (write(client_fd, &resp, sizeof(resp)) < 0) {
                /* write error ignore in signal handler/thread */
            }
        }
        close(client_fd);
    }
    return NULL;
}

int chaos_server_start(void *heap_base, size_t heap_size) {
    g_chaos_heap_base = heap_base;
    g_chaos_heap_size = heap_size;
    
    char sock_path[100];
    snprintf(sock_path, sizeof(sock_path), "%s.%d", CHAOS_SOCK_PATH, getpid());
    unlink(sock_path);
    
    g_chaos_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_chaos_server_fd < 0) {
        LOG_E("CHAOS", "Failed to create UNIX socket: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    
    if (bind(g_chaos_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_E("CHAOS", "Failed to bind socket %s: %s", sock_path, strerror(errno));
        close(g_chaos_server_fd);
        g_chaos_server_fd = -1;
        return -1;
    }
    
    if (listen(g_chaos_server_fd, 5) < 0) {
        LOG_E("CHAOS", "listen failed: %s", strerror(errno));
        close(g_chaos_server_fd);
        g_chaos_server_fd = -1;
        return -1;
    }
    
    g_chaos_running = true;
    if (pthread_create(&g_chaos_thread, NULL, chaos_thread_worker, NULL) != 0) {
        LOG_E("CHAOS", "Failed to spawn chaos listener thread");
        close(g_chaos_server_fd);
        g_chaos_server_fd = -1;
        g_chaos_running = false;
        return -1;
    }
    
    LOG_D("CHAOS", "Chaos injection server listening on %s", sock_path);
    return 0;
}

void chaos_server_stop(void) {
    if (!g_chaos_running) return;
    g_chaos_running = false;
    
    if (g_chaos_server_fd >= 0) {
        close(g_chaos_server_fd);
        g_chaos_server_fd = -1;
    }
    
    char sock_path[100];
    snprintf(sock_path, sizeof(sock_path), "%s.%d", CHAOS_SOCK_PATH, getpid());
    unlink(sock_path);
    
    pthread_cancel(g_chaos_thread);
    pthread_join(g_chaos_thread, NULL);
}

int chaos_send_command(chaos_cmd_type_t cmd, uint8_t worker_idx, uint32_t target_pid, const char *reason, chaos_response_t *resp) {
    char sock_path[100];
    snprintf(sock_path, sizeof(sock_path), "%s.%u", CHAOS_SOCK_PATH, target_pid);
    
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    
    chaos_request_t req;
    memset(&req, 0, sizeof(req));
    req.cmd_type = (uint8_t)cmd;
    req.target_worker_idx = worker_idx;
    req.target_pid = target_pid;
    if (reason) strncpy(req.reason, reason, sizeof(req.reason) - 1);
    
    if (write(fd, &req, sizeof(req)) != sizeof(req)) {
        close(fd);
        return -1;
    }
    
    if (resp) {
        if (read(fd, resp, sizeof(*resp)) < 0) {
            /* read error */
        }
    }
    
    close(fd);
    return 0;
}
