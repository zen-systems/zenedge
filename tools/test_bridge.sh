#!/bin/bash

# Setup Shared Memory
dd if=/dev/zero of=/tmp/zenedge_shm bs=1M count=1 2>/dev/null

# Start Bridge (Background)
python3 tools/bridge_run.py > bridge_test_final.log 2>&1 &
BRIDGE_PID=$!
echo "Bridge started with PID $BRIDGE_PID"

# Start QEMU (Background) - Inject 'ping' command
# Note warning: 'ping' must be type AFTER shell is ready.
# We'll use a sleep to delay input, or just send it immediately and hope shell buffers it.
# To be safe, we'll use a subshell with sleep.
(sleep 2; echo "ping"; sleep 1; echo "ping"; sleep 2) | \
qemu-system-i386 -cdrom zenedge.iso -nographic -serial mon:stdio \
  -device ivshmem-plain,memdev=hostmem \
  -object memory-backend-file,size=1M,share=on,mem-path=/tmp/zenedge_shm,id=hostmem \
  -display none > qemu_bridge_final.log 2>&1 &
QEMU_PID=$!
echo "QEMU started with PID $QEMU_PID"

# Wait for test to run
sleep 8

# Cleanup
kill $QEMU_PID
kill $BRIDGE_PID

echo "Test Complete."
