#!/usr/bin/env python3
import mmap
import os
import struct
import time
import sys

# Constants matching kernel/ipc/ipc_proto.h
IPC_CMD_RING_OFFSET  = 0x00000
IPC_RSP_RING_OFFSET  = 0x08000
IPC_DOORBELL_OFFSET  = 0x10000
IPC_HEAP_CTL_OFFSET  = 0x10100
IPC_HEAP_DATA_OFFSET = 0x11000
IPC_RING_SIZE        = 1024
IPC_MAGIC            = 0x51DECA9E
IPC_RSP_MAGIC        = 0x52535030
IPC_DOORBELL_MAGIC   = 0x444F4F52

SHM_PATH = "/tmp/zenedge_shm"
SHM_SIZE = 1048576 # 1MB

class IpcBridge:
    def __init__(self, shm_path):
        self.shm_path = shm_path
        self.shm_file = None
        self.mm = None
        self._setup_shm()

    def _setup_shm(self):
        """Open and mmap the shared file."""
        if not os.path.exists(self.shm_path):
            print(f"[Bridge] Creating {self.shm_path}...")
            with open(self.shm_path, "wb") as f:
                f.write(b'\x00' * SHM_SIZE)
        
        self.shm_file = open(self.shm_path, "r+b")
        self.mm = mmap.mmap(self.shm_file.fileno(), SHM_SIZE)
        print(f"[Bridge] Mapped {self.shm_path} ({SHM_SIZE} bytes)")

    def run(self):
        print("[Bridge] Starting polling loop (Ctrl+C to stop)...")
        try:
            while True:
                self._poll_cmd_ring()
                time.sleep(0.01) # 10ms poll
        except KeyboardInterrupt:
            print("\n[Bridge] Stopping...")
            self.mm.close()
            self.shm_file.close()

    def _poll_cmd_ring(self):
        # Struct layout for Ring Buffer Header:
        # uint32_t magic;
        # uint32_t head;
        # uint32_t tail;
        # uint32_t size;
        # uint32_t reserved[4];
        
        offset = IPC_CMD_RING_OFFSET
        hdr_fmt = "<IIII" # Little endian 4x uint32
        magic, head, tail, size = struct.unpack_from(hdr_fmt, self.mm, offset)

        if magic != IPC_MAGIC:
            # print(f"[Bridge] Waiting for initialization (Magic: {hex(magic)})...")
            return

        if head != tail:
            print(f"[Bridge] New Command! Head={head}, Tail={tail}")
            
            # Read Packet at Tail
            # Header size = 32 bytes (8 * 4) including reserved
            # Packet size = 16 bytes
            ring_header_size = 32
            packet_size = 16
            
            packet_offset = offset + ring_header_size + (tail * packet_size)
            pkt_fmt = "<HHQ" # cmd, flags, payload_id (Wait, struct packing?)
            # Struct: 
            # uint16_t cmd;
            # uint16_t flags;
            # uint32_t payload_id;
            # uint64_t timestamp;
            pkt_fmt = "<HHIQ"
            
            cmd, flags, payload_id, ts = struct.unpack_from(pkt_fmt, self.mm, packet_offset)
            print(f"[Bridge] RECV Cmd={hex(cmd)} Flags={hex(flags)} Payload={hex(payload_id)}")
            
            # Process (Mock)
            # Send Response
            self._send_response(cmd, 0)
            
            # Update Tail (Consume)
            tail = (tail + 1) % size
            struct.pack_into("<I", self.mm, offset + 8, tail) # Update tail in shared memory

    def _send_response(self, orig_cmd, result):
        offset = IPC_RSP_RING_OFFSET
        hdr_fmt = "<IIII" 
        magic, head, tail, size = struct.unpack_from(hdr_fmt, self.mm, offset)
        
        if magic != IPC_RSP_MAGIC:
            print("[Bridge] Response ring invalid magic")
            return

        next_head = (head + 1) % size
        if next_head == tail:
            print("[Bridge] Response ring full")
            return
            
        # Write Response Packet
        # Struct:
        # uint16_t status;
        # uint16_t orig_cmd;
        # uint32_t result;
        # uint64_t timestamp;
        rsp_fmt = "<HHIQ"
        
        ring_header_size = 32
        packet_size = 16
        
        packet_offset = offset + ring_header_size + (head * packet_size)
        
        # RSP_OK = 0x8000
        struct.pack_into(rsp_fmt, self.mm, packet_offset, 0x8000, orig_cmd, result, int(time.time()*1000000))
        
        # Update Head
        struct.pack_into("<I", self.mm, offset + 4, next_head)
        print(f"[Bridge] SENT Rsp Head={next_head}")

if __name__ == "__main__":
    bridge = IpcBridge(SHM_PATH)
    bridge.run()
