#!/bin/bash
# run_bridge_test.sh
# Test script for ivshmem-plain integration with Python bridge

set -e

SHM_FILE="/dev/shm/zenedge_test"
BRIDGE_LOG="bridge_test.log"
QEMU_LOG="qemu_test.log"

echo "[TEST] Starting ivshmem-plain test..."

# 1. Create shared memory file (1MB)
echo "[TEST] Creating 1MB shared memory file at $SHM_FILE..."
dd if=/dev/zero of=$SHM_FILE bs=1M count=1 status=none

# 2. Start Python Bridge in background
echo "[TEST] Starting Python bridge..."
python3 -u -m bridge.zenedge_bridge --shm $SHM_FILE --create > $BRIDGE_LOG 2>&1 &
BRIDGE_PID=$!
echo "[TEST] Bridge running (PID $BRIDGE_PID)"

# Give bridge a moment to initialize
sleep 1

# 3. Build Kernel
echo "[TEST] Building kernel..."
make -B zenedge.bin > /dev/null

# 4. Run QEMU
echo "[TEST] Launching QEMU..."
# Note: We use -device ivshmem-plain with memdev property pointing to memory-backend-file
qemu-system-i386 \
    -kernel zenedge.bin \
    -serial stdio \
    -display none \
    -device ivshmem-plain,memdev=hostmem \
    -object memory-backend-file,size=1M,share=on,mem-path=$SHM_FILE,id=hostmem \
    > $QEMU_LOG 2>&1 &
QEMU_PID=$!

echo "[TEST] QEMU running (PID $QEMU_PID). Waiting 5 seconds..."
sleep 5

# 5. Cleanup
echo "[TEST] Stopping processes..."
kill $QEMU_PID 2>/dev/null || true
kill $BRIDGE_PID 2>/dev/null || true
rm -f $SHM_FILE

# 6. Show results
echo -e "\n=== QEMU Output (Tail) ==="
tail -n 20 $QEMU_LOG

echo -e "\n=== Bridge Output (All) ==="
cat $BRIDGE_LOG

echo -e "\n[TEST] Done."
