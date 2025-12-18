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

# 4. Run QEMU Node 0
echo "[TEST] Launching QEMU Node 0..."
qemu-system-i386 \
    -kernel zenedge.bin \
    -serial file:qemu_node0.log \
    -display none \
    -device ivshmem-plain,memdev=hostmem \
    -object memory-backend-file,size=1M,share=on,mem-path=$SHM_FILE,id=hostmem \
    > /dev/null 2>&1 &
NODE0_PID=$!

echo "[TEST] Launching QEMU Node 1..."
# Launch Node 1
qemu-system-i386 \
    -kernel zenedge.bin \
    -serial file:qemu_node1.log \
    -display none \
    -device ivshmem-plain,memdev=hostmem \
    -object memory-backend-file,size=1M,share=on,mem-path=$SHM_FILE,id=hostmem \
    > /dev/null 2>&1 &
NODE1_PID=$!

echo "[TEST] Nodes running (PIDs $NODE0_PID, $NODE1_PID). Waiting 5 seconds..."
sleep 5

# 5. Cleanup
echo "[TEST] Stopping processes..."
kill $NODE0_PID 2>/dev/null || true
kill $NODE1_PID 2>/dev/null || true
kill $BRIDGE_PID 2>/dev/null || true
rm -f $SHM_FILE

# 6. Show results
echo -e "\n=== QEMU Node 0 Output (Tail) ==="
tail -n 20 qemu_node0.log

echo -e "\n=== QEMU Node 1 Output (Tail) ==="
tail -n 20 qemu_node1.log

# Check if Node 0 saw Node 1
grep -q "Node 1" qemu_node0.log && echo "[TEST] SUCCESS: Node 0 sees Node 1!" || echo "[TEST] FAILURE: Node 0 did not see Node 1"

echo -e "\n=== Bridge Output (All) ==="
cat $BRIDGE_LOG

echo -e "\n[TEST] Done."
