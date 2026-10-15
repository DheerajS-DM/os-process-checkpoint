#!/usr/bin/env bash
# ==============================================================================
# deploy_mac.sh - Native macOS Deployment & Execution Helper
# Process Recovery Engine & DPC-RR Dynamic Priority Scheduler
# ==============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ANSI Colors
CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
MAGENTA='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

print_banner() {
    echo -e "${CYAN}${BOLD}"
    echo "=================================================================="
    echo "  🚀 Process Recovery Engine — Native macOS (Darwin) Runner       "
    echo "=================================================================="
    echo -e "${NC}"
}

print_help() {
    echo -e "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  --test        Compile binaries with Clang and run unit test suite"
    echo "  --start       Build and launch C Supervisor Daemon with workers"
    echo "  --gui         Launch PyQt6 Telemetry & Control GUI on macOS Aqua"
    echo "  --all         Build, verify tests, launch supervisor & start GUI"
    echo "  --clean       Remove build artifacts and checkpoint snapshots"
    echo "  --help, -h    Show this help message"
    echo ""
    echo "Default (no args): Compiles and runs all unit tests."
}

check_prereqs() {
    echo -e "${CYAN}>>> Checking macOS developer tools...${NC}"
    if ! command -v clang &>/dev/null && ! command -v gcc &>/dev/null; then
        echo -e "${RED}[ERROR] Neither clang nor gcc found. Please install Xcode Command Line Tools:${NC}"
        echo "  xcode-select --install"
        exit 1
    fi

    if ! command -v make &>/dev/null; then
        echo -e "${RED}[ERROR] make command not found.${NC}"
        exit 1
    fi

    echo -e "${GREEN}✓ Native C build toolchain detected ($(clang --version | head -n 1))${NC}"
}

build_and_verify() {
    echo ""
    echo -e "${CYAN}>>> Building binaries via Clang...${NC}"
    make clean
    make all

    echo ""
    echo -e "${CYAN}>>> Executing native unit test suite...${NC}"
    mkdir -p checkpoints
    
    echo -e "\n${BOLD}[1/4] Running mprotect & Memory Trap Unit Test...${NC}"
    ./build/test_mprotect

    echo -e "\n${BOLD}[2/4] Running DPC-RR Scheduler Unit Test...${NC}"
    ./build/test_scheduler

    echo -e "\n${BOLD}[3/4] Running Checkpoint Serialization Unit Test...${NC}"
    ./build/test_checkpoint

    echo -e "\n${BOLD}[4/4] Running Python CTypes Shared Memory Struct Test...${NC}"
    if command -v python3 &>/dev/null; then
        python3 tests/test_shm_bridge.py
    else
        echo -e "${YELLOW}[WARN] python3 not found, skipping ctypes layout test.${NC}"
    fi

    echo ""
    echo -e "${GREEN}${BOLD}=================================================="
    echo "   🎉 ALL UNIT TESTS & VERIFICATIONS PASSED!      "
    echo -e "==================================================${NC}"
}

start_supervisor() {
    echo ""
    echo -e "${MAGENTA}${BOLD}>>> Launching Supervisor Daemon (Native Darwin)...${NC}"
    mkdir -p checkpoints build
    make supervisor worker
    
    # Clean up old shm or sockets
    rm -f /tmp/wsl_telemetry.shm /tmp/wsl_chaos.sock.*
    
    exec ./build/supervisor --workers 2 --heap-mb 256 --checkpoint-interval 4000
}

launch_gui() {
    echo ""
    echo -e "${CYAN}${BOLD}>>> Launching PyQt6 Telemetry & Control Dashboard...${NC}"
    
    # Try virtual environment first if present
    PYTHON_BIN="python3"
    if [ -f "./venv/bin/python" ]; then
        PYTHON_BIN="./venv/bin/python"
    elif [ -f "./venv/bin/python3" ]; then
        PYTHON_BIN="./venv/bin/python3"
    fi

    # Check PyQt6 installation
    if ! $PYTHON_BIN -c "import PyQt6, pyqtgraph" &>/dev/null; then
        echo -e "${YELLOW}[WARN] PyQt6 or pyqtgraph not found in $PYTHON_BIN environment.${NC}"
        echo "Installing required dashboard dependencies..."
        $PYTHON_BIN -m pip install -r dashboard/requirements.txt
    fi

    $PYTHON_BIN dashboard/main.py
}

# Main Execution Switch
print_banner
check_prereqs

case "$1" in
    --test)
        build_and_verify
        ;;
    --start)
        start_supervisor
        ;;
    --gui)
        launch_gui
        ;;
    --all)
        build_and_verify
        echo ""
        echo -e "${CYAN}>>> Launching Supervisor daemon in background...${NC}"
        rm -f /tmp/wsl_telemetry.shm /tmp/wsl_chaos.sock.*
        ./build/supervisor --workers 2 --heap-mb 256 --checkpoint-interval 4000 &
        SUPERVISOR_PID=$!
        sleep 0.5
        
        echo -e "${GREEN}✓ Supervisor running (PID $SUPERVISOR_PID). Launching Dashboard GUI...${NC}"
        trap "kill $SUPERVISOR_PID 2>/dev/null || true" EXIT INT TERM
        launch_gui
        ;;
    --clean)
        echo -e "${CYAN}>>> Cleaning build and checkpoints...${NC}"
        make clean
        rm -f /tmp/wsl_telemetry.shm /tmp/wsl_chaos.sock.*
        echo -e "${GREEN}✓ Cleanup complete.${NC}"
        ;;
    --help|-h)
        print_help
        ;;
    "")
        build_and_verify
        ;;
    *)
        echo -e "${RED}[ERROR] Unknown option: $1${NC}"
        print_help
        exit 1
        ;;
esac
