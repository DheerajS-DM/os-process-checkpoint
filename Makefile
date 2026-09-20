CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude
LDFLAGS  = -lrt -lpthread -lm

WORKER_CFLAGS = $(CFLAGS) -no-pie

BUILD_DIR = build

all: dirs supervisor worker tests

dirs:
	@mkdir -p $(BUILD_DIR) checkpoints

supervisor: src/supervisor/main.c \
            src/supervisor/scheduler.c \
            src/supervisor/process_mgr.c \
            src/supervisor/checkpoint_mgr.c \
            src/supervisor/ipc_shm.c \
            src/common/logging.c \
            src/common/timing.c
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/supervisor $^ $(LDFLAGS)

worker: src/worker/main.c \
        src/worker/dirty_tracker.c \
        src/worker/checkpoint.c \
        src/worker/restore.c \
        src/worker/workload_matrix.c \
        src/worker/workload_hash.c \
        src/worker/fault_inject.c \
        src/common/logging.c \
        src/common/timing.c
	$(CC) $(WORKER_CFLAGS) -o $(BUILD_DIR)/worker $^ $(LDFLAGS)

tests: test_mprotect test_scheduler test_checkpoint

test_mprotect: tests/test_mprotect.c \
              src/worker/dirty_tracker.c \
              src/common/logging.c \
              src/common/timing.c
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_mprotect $^ $(LDFLAGS)

test_scheduler: tests/test_scheduler.c \
                src/supervisor/scheduler.c \
                src/common/logging.c \
                src/common/timing.c
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_scheduler $^ $(LDFLAGS)

test_checkpoint: tests/test_checkpoint.c \
                 src/worker/checkpoint.c \
                 src/worker/restore.c \
                 src/worker/dirty_tracker.c \
                 src/common/logging.c \
                 src/common/timing.c
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_checkpoint $^ $(LDFLAGS)

test: tests
	bash tests/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) checkpoints/*.ckpt checkpoints/*.tmp test_run.ckpt

.PHONY: all dirs supervisor worker tests test clean
