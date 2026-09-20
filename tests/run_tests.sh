#!/usr/bin/env bash
set -e

echo "=================================================="
echo "    Running Process Recovery Engine Test Suite   "
echo "=================================================="

# Ensure build directory exists and binaries are compiled
mkdir -p build
make tests

echo ""
echo "[1/4] Running mprotect & memory trap tests..."
./build/test_mprotect

echo ""
echo "[2/4] Running DPC-RR scheduler tests..."
./build/test_scheduler

echo ""
echo "[3/4] Running checkpoint serialization tests..."
./build/test_checkpoint

echo ""
echo "[4/4] Running Python SHM bridge layout tests..."
python3 tests/test_shm_bridge.py

echo ""
echo "=================================================="
echo "        🎉 ALL UNIT & SUITE TESTS PASSED!         "
echo "=================================================="
