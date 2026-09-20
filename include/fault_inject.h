#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include "common.h"

typedef enum {
    CHAOS_CMD_NONE = 0,
    CHAOS_CMD_OOB_WRITE,      /* Trigger real crash by dereferencing NULL */
    CHAOS_CMD_SIGKILL,        /* Kill worker immediately */
    CHAOS_CMD_CORRUPT_HEAP,   /* Randomly flip bits in heap */
    CHAOS_CMD_HANG            /* Enter infinite non-yielding loop */
} chaos_cmd_type_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t cmd_type;
    uint8_t target_worker_idx;
    uint32_t target_pid;
    char reason[32];
} chaos_request_t;

typedef struct {
    uint8_t success;
    char message[64];
} chaos_response_t;
#pragma pack(pop)

int chaos_server_start(void *heap_base, size_t heap_size);
void chaos_server_stop(void);

int chaos_send_command(chaos_cmd_type_t cmd, uint8_t worker_idx, uint32_t target_pid, const char *reason, chaos_response_t *resp);

#endif /* FAULT_INJECT_H */
