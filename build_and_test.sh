#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="${SOURCE_DIR:-$SCRIPT_DIR}"
TARGET_DIR="${TARGET_DIR:-$HOME/tanmay}"

echo ">>> Syncing files from Windows to Linux workspace..."
mkdir -p "$TARGET_DIR"
cp -ru "$SOURCE_DIR"/* "$TARGET_DIR"/
cd "$TARGET_DIR"

echo ">>> Building C binaries..."
mkdir -p build checkpoints

gcc -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude \
    -o build/supervisor \
    src/supervisor/main.c \
    src/supervisor/scheduler.c \
    src/supervisor/process_mgr.c \
    src/supervisor/checkpoint_mgr.c \
    src/supervisor/ipc_shm.c \
    src/common/logging.c \
    src/common/timing.c \
    -lrt -lpthread -lm

gcc -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude -no-pie \
    -o build/worker \
    src/worker/main.c \
    src/worker/dirty_tracker.c \
    src/worker/checkpoint.c \
    src/worker/restore.c \
    src/worker/workload_matrix.c \
    src/worker/workload_hash.c \
    src/worker/fault_inject.c \
    src/common/logging.c \
    src/common/timing.c \
    -lrt -lpthread -lm

gcc -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude \
    -o build/test_mprotect \
    tests/test_mprotect.c \
    src/worker/dirty_tracker.c \
    src/common/logging.c \
    src/common/timing.c \
    -lrt -lpthread -lm

gcc -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude \
    -o build/test_scheduler \
    tests/test_scheduler.c \
    src/supervisor/scheduler.c \
    src/common/logging.c \
    src/common/timing.c \
    -lrt -lpthread -lm

gcc -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -Iinclude \
    -o build/test_checkpoint \
    tests/test_checkpoint.c \
    src/worker/checkpoint.c \
    src/worker/restore.c \
    src/worker/dirty_tracker.c \
    src/common/logging.c \
    src/common/timing.c \
    -lrt -lpthread -lm

echo ">>> Build completed successfully!"

echo ""
echo ">>> [1/4] Running mprotect & Memory Trap Unit Test..."
./build/test_mprotect

echo ""
echo ">>> [2/4] Running DPC-RR Scheduler Unit Test..."
./build/test_scheduler

echo ""
echo ">>> [3/4] Running Checkpoint Serialization Unit Test..."
./build/test_checkpoint

echo ""
echo ">>> [4/4] Running Python CTypes Shared Memory Struct Test..."
python3 tests/test_shm_bridge.py

echo ""
echo "=================================================="
echo "   🎉 ALL UNIT TESTS & VERIFICATIONS PASSED!      "
echo "=================================================="
