#!/bin/bash
#
# ZENEDGE QEMU Launch Script
#
# Launches QEMU with shared memory for IPC with the Python bridge.
# The shared memory file is created in /dev/shm and can be accessed
# by both QEMU (as ivshmem device) and the Python bridge (via mmap).
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZENEDGE_DIR="$(dirname "$SCRIPT_DIR")"
ISO_PATH="${ZENEDGE_DIR}/zenedge.iso"
SHM_FILE="/dev/shm/zenedge.shm"
SHM_SIZE="1M"
MEMORY="128"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Parse arguments
DISPLAY_MODE="none"
SERIAL_MODE="stdio"
DEBUG_MODE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --display|-d)
            DISPLAY_MODE="$2"
            shift 2
            ;;
        --serial|-s)
            SERIAL_MODE="$2"
            shift 2
            ;;
        --debug|-D)
            DEBUG_MODE="-s -S"
            shift
            ;;
        --iso|-i)
            ISO_PATH="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --display, -d MODE   Display mode: none, sdl, gtk (default: none)"
            echo "  --serial, -s MODE    Serial mode: stdio, file:path (default: stdio)"
            echo "  --debug, -D          Enable GDB debugging (-s -S)"
            echo "  --iso, -i PATH       Path to ISO file (default: zenedge.iso)"
            echo "  --help, -h           Show this help"
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check for ISO
if [ ! -f "$ISO_PATH" ]; then
    log_error "ISO not found: $ISO_PATH"
    log_info "Build it first with: make iso"
    exit 1
fi

# Check for QEMU
if ! command -v qemu-system-i386 &> /dev/null; then
    log_error "qemu-system-i386 not found"
    log_info "Install QEMU: brew install qemu (macOS) or apt install qemu-system-x86 (Linux)"
    exit 1
fi

# Create shared memory file if needed
if [ ! -f "$SHM_FILE" ]; then
    log_info "Creating shared memory file: $SHM_FILE"
    dd if=/dev/zero of="$SHM_FILE" bs=1M count=1 2>/dev/null
    chmod 666 "$SHM_FILE"
fi

# Check file size
ACTUAL_SIZE=$(stat -f%z "$SHM_FILE" 2>/dev/null || stat -c%s "$SHM_FILE" 2>/dev/null)
if [ "$ACTUAL_SIZE" -lt 1048576 ]; then
    log_warn "Shared memory file is smaller than expected, recreating..."
    dd if=/dev/zero of="$SHM_FILE" bs=1M count=1 2>/dev/null
fi

log_info "ZENEDGE QEMU Launcher"
log_info "====================="
log_info "ISO: $ISO_PATH"
log_info "Shared Memory: $SHM_FILE ($SHM_SIZE)"
log_info "Display: $DISPLAY_MODE"
log_info "Serial: $SERIAL_MODE"
[ -n "$DEBUG_MODE" ] && log_info "Debug: GDB on localhost:1234"

echo ""
log_info "Starting QEMU..."
log_info "Press Ctrl+A, X to exit"
echo ""

# Build QEMU command
QEMU_CMD=(
    qemu-system-i386
    -cdrom "$ISO_PATH"
    -m "$MEMORY"
    -object "memory-backend-file,id=shm,size=$SHM_SIZE,mem-path=$SHM_FILE,share=on"
    -device "ivshmem-plain,memdev=shm"
    -serial "$SERIAL_MODE"
    -display "$DISPLAY_MODE"
)

# Add debug options if requested
if [ -n "$DEBUG_MODE" ]; then
    QEMU_CMD+=($DEBUG_MODE)
fi

# Run QEMU
exec "${QEMU_CMD[@]}"
