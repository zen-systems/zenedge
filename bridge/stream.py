"""
Streaming obs/action rings (SPSC).

Kernel:
- Produces actions (action ring)
- Consumes observations (obs ring)

Host:
- Consumes actions
- Produces observations
"""

from typing import Optional, Tuple

from .protocol import (
    IPC_STREAM_MAGIC,
    IPC_OBS_RING_OFFSET,
    IPC_ACT_RING_OFFSET,
    IPC_OBS_RING_SIZE,
    IPC_ACT_RING_SIZE,
    RING_HEADER_STRUCT,
    RING_HEADER_SIZE,
    RingHeader,
    OBS_ENTRY_STRUCT,
    ACT_ENTRY_STRUCT,
)


class StreamRing:
    def __init__(self, shm, offset: int, entry_struct, size: int):
        self.shm = shm
        self.offset = offset
        self.entry_struct = entry_struct
        self.entry_size = entry_struct.size
        self.size = size

    def _read_header(self) -> RingHeader:
        self.shm.seek(self.offset)
        data = self.shm.read(RING_HEADER_STRUCT.size)
        return RingHeader.unpack(data)

    def _write_head(self, head: int) -> None:
        self.shm.seek(self.offset + 4)
        self.shm.write(head.to_bytes(4, 'little'))

    def _write_tail(self, tail: int) -> None:
        self.shm.seek(self.offset + 8)
        self.shm.write(tail.to_bytes(4, 'little'))

    def ready(self) -> bool:
        hdr = self._read_header()
        return hdr.magic == IPC_STREAM_MAGIC and hdr.size == self.size

    def pop(self) -> Optional[Tuple]:
        hdr = self._read_header()
        if hdr.magic != IPC_STREAM_MAGIC or hdr.head == hdr.tail:
            return None

        entry_offset = self.offset + RING_HEADER_SIZE + (hdr.tail * self.entry_size)
        self.shm.seek(entry_offset)
        data = self.shm.read(self.entry_size)
        entry = self.entry_struct.unpack(data)

        new_tail = (hdr.tail + 1) % hdr.size
        self._write_tail(new_tail)
        return entry

    def push(self, entry: Tuple) -> bool:
        hdr = self._read_header()
        if hdr.magic != IPC_STREAM_MAGIC:
            return False

        next_head = (hdr.head + 1) % hdr.size
        if next_head == hdr.tail:
            return False

        entry_offset = self.offset + RING_HEADER_SIZE + (hdr.head * self.entry_size)
        self.shm.seek(entry_offset)
        self.shm.write(self.entry_struct.pack(*entry))

        self._write_head(next_head)
        return True


class StreamRings:
    def __init__(self, shm):
        self.obs_ring = StreamRing(shm, IPC_OBS_RING_OFFSET, OBS_ENTRY_STRUCT, IPC_OBS_RING_SIZE)
        self.act_ring = StreamRing(shm, IPC_ACT_RING_OFFSET, ACT_ENTRY_STRUCT, IPC_ACT_RING_SIZE)

    def ready(self) -> bool:
        return self.obs_ring.ready() and self.act_ring.ready()
