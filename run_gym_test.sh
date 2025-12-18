#!/bin/bash
# run_gym_test.sh

set -e

SHM_FILE="/dev/shm/zenedge_test"
GYM_LOG="gym_bridge.log"
QEMU_LOG="qemu_gym.log"

echo "[TEST] Starting Gym Bridge Test..."

# 1. Create SHM
dd if=/dev/zero of=$SHM_FILE bs=1M count=1 status=none

# 2. Start Gym Agent
echo "[TEST] Starting Zenedge Gym Agent..."
export PYTHONPATH=$PYTHONPATH:$(pwd)
python3 -u bridge/zenedge_gym_agent.py --shm $SHM_FILE > $GYM_LOG 2>&1 &
BRIDGE_PID=$!
echo "[TEST] Gym Agent running (PID $BRIDGE_PID)"

sleep 1

# 3. Build Kernel
make -B zenedge.bin > /dev/null

# 4. Run QEMU
echo "[TEST] Launching QEMU..."
qemu-system-i386 \
    -kernel zenedge.bin \
    -serial stdio \
    -display none \
    -device ivshmem-plain,memdev=hostmem \
    -object memory-backend-file,size=1M,share=on,mem-path=$SHM_FILE,id=hostmem \
    > $QEMU_LOG 2>&1 &
QEMU_PID=$!

echo "[TEST] Waiting 10 seconds for simulation..."
sleep 10

# 5. Cleanup
echo "[TEST] Stopping..."
kill $QEMU_PID 2>/dev/null || true
kill $BRIDGE_PID 2>/dev/null || true
rm -f $SHM_FILE

# 6. Results
echo -e "\n=== QEMU Output (Tail) ==="
tail -n 30 $QEMU_LOG

echo -e "\n=== Gym Agent Output (Tail) ==="
tail -n 30 $GYM_LOG

echo -e "\n[TEST] Done."
