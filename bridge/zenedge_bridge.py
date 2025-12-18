#!/usr/bin/env python3
"""
ZENEDGE Bridge - Linux side of the sidecar IPC

Communicates with the ZENEDGE kernel via shared memory, implementing
the ring buffer protocol for commands and responses.
"""

import mmap
import os
import sys
import time
import argparse
from pathlib import Path
from typing import Optional, Tuple, Callable, Dict

from .protocol import (
    IPC_SHARED_MEM_SIZE,
    IPC_CMD_RING_OFFSET,
    IPC_RSP_RING_OFFSET,
    IPC_DOORBELL_OFFSET,
    IPC_MAGIC,
    IPC_RSP_MAGIC,
    DOORBELL_MAGIC,
    IPC_RING_SIZE,
    RING_HEADER_SIZE,
    PACKET_SIZE,
    RESPONSE_SIZE,
    CMD_NAMES,
    RSP_OK,
    RSP_ERROR,
    RingHeader,
    Packet,
    Response,
    get_timestamp,
    RING_HEADER_STRUCT,
    PACKET_STRUCT,
    RESPONSE_STRUCT,
)
from .heap import HeapManager
from .models import ModelCache


class ZenedgeBridge:
    """
    Main bridge class that handles IPC with ZENEDGE kernel.

    Usage:
        bridge = ZenedgeBridge("/dev/shm/zenedge.shm")
        bridge.register_handler(CMD_PING, handle_ping)
        bridge.run()
    """

    def __init__(self, shm_path: str = "/dev/shm/zenedge.shm",
                 model_dir: str = "./models",
                 create: bool = False):
        """
        Initialize the bridge.

        Args:
            shm_path: Path to shared memory file
            model_dir: Directory containing PyTorch models
            create: If True, create the shared memory file if it doesn't exist
        """
        self.shm_path = Path(shm_path)
        self.handlers: Dict[int, Callable] = {}
        self.running = False

        # Statistics
        self.stats = {
            'commands_received': 0,
            'responses_sent': 0,
            'errors': 0,
            'start_time': None,
        }

        # Open or create shared memory file
        if create and not self.shm_path.exists():
            print(f"[BRIDGE] Creating shared memory file: {self.shm_path}")
            with open(self.shm_path, 'wb') as f:
                f.write(b'\x00' * IPC_SHARED_MEM_SIZE)

        if not self.shm_path.exists():
            raise FileNotFoundError(
                f"Shared memory file not found: {self.shm_path}\n"
                f"Start QEMU first or use --create to create it."
            )

        # Memory map the file
        self.fd = os.open(str(self.shm_path), os.O_RDWR)
        self.shm = mmap.mmap(self.fd, IPC_SHARED_MEM_SIZE)

        print(f"[BRIDGE] Mapped shared memory: {self.shm_path} ({IPC_SHARED_MEM_SIZE} bytes)")

        # Initialize subsystems
        self.heap = HeapManager(self.shm)
        self.model_cache = ModelCache(model_dir)

        # Verify shared memory is initialized
        self._verify_initialization()

    def _verify_initialization(self):
        """Check that ZENEDGE has initialized the shared memory."""
        # Read command ring header
        self.shm.seek(IPC_CMD_RING_OFFSET)
        cmd_header_data = self.shm.read(RING_HEADER_STRUCT.size)
        cmd_header = RingHeader.unpack(cmd_header_data)

        # Read response ring header
        self.shm.seek(IPC_RSP_RING_OFFSET)
        rsp_header_data = self.shm.read(RING_HEADER_STRUCT.size)
        rsp_header = RingHeader.unpack(rsp_header_data)

        # Read doorbell
        self.shm.seek(IPC_DOORBELL_OFFSET)
        doorbell_magic = int.from_bytes(self.shm.read(4), 'little')

        print(f"[BRIDGE] Command ring: magic={cmd_header.magic:#010x}, "
              f"head={cmd_header.head}, tail={cmd_header.tail}, size={cmd_header.size}")
        print(f"[BRIDGE] Response ring: magic={rsp_header.magic:#010x}, "
              f"head={rsp_header.head}, tail={rsp_header.tail}, size={rsp_header.size}")
        print(f"[BRIDGE] Doorbell magic: {doorbell_magic:#010x}")

        if cmd_header.magic != IPC_MAGIC:
            print(f"[BRIDGE] WARNING: Command ring not initialized "
                  f"(expected {IPC_MAGIC:#010x})")
        if rsp_header.magic != IPC_RSP_MAGIC:
            print(f"[BRIDGE] WARNING: Response ring not initialized "
                  f"(expected {IPC_RSP_MAGIC:#010x})")
        if doorbell_magic != DOORBELL_MAGIC:
            print(f"[BRIDGE] WARNING: Doorbell not initialized "
                  f"(expected {DOORBELL_MAGIC:#010x})")

    def _read_cmd_ring_header(self) -> RingHeader:
        """Read the command ring header."""
        self.shm.seek(IPC_CMD_RING_OFFSET)
        data = self.shm.read(RING_HEADER_STRUCT.size)
        return RingHeader.unpack(data)

    def _write_cmd_ring_tail(self, tail: int):
        """Update the command ring tail pointer."""
        self.shm.seek(IPC_CMD_RING_OFFSET + 8)  # Offset to tail field
        self.shm.write(tail.to_bytes(4, 'little'))

    def _read_rsp_ring_header(self) -> RingHeader:
        """Read the response ring header."""
        self.shm.seek(IPC_RSP_RING_OFFSET)
        data = self.shm.read(RING_HEADER_STRUCT.size)
        return RingHeader.unpack(data)

    def _write_rsp_ring_header(self, head: int):
        """Update the response ring head pointer."""
        self.shm.seek(IPC_RSP_RING_OFFSET + 4)  # Offset to head field
        self.shm.write(head.to_bytes(4, 'little'))

    def _read_doorbell(self) -> Tuple[int, int]:
        """Read cmd_doorbell and rsp_doorbell values."""
        self.shm.seek(IPC_DOORBELL_OFFSET + 8)  # Skip magic and version
        cmd_doorbell = int.from_bytes(self.shm.read(4), 'little')
        self.shm.seek(IPC_DOORBELL_OFFSET + 20)  # rsp_doorbell offset
        rsp_doorbell = int.from_bytes(self.shm.read(4), 'little')
        return cmd_doorbell, rsp_doorbell

    def _write_rsp_doorbell(self, value: int):
        """Write to the response doorbell."""
        self.shm.seek(IPC_DOORBELL_OFFSET + 20)  # rsp_doorbell offset
        self.shm.write(value.to_bytes(4, 'little'))

        # Increment rsp_writes counter
        self.shm.seek(IPC_DOORBELL_OFFSET + 36)  # rsp_writes offset
        current = int.from_bytes(self.shm.read(4), 'little')
        self.shm.seek(IPC_DOORBELL_OFFSET + 36)
        self.shm.write((current + 1).to_bytes(4, 'little'))

    def poll_command(self) -> Optional[Packet]:
        """
        Check for and consume one command from the ring.

        Returns:
            Packet if a command was available, None otherwise
        """
        header = self._read_cmd_ring_header()

        if header.magic != IPC_MAGIC:
            return None  # Ring not initialized

        # Check if ring is empty
        if header.head == header.tail:
            return None

        # Read packet at tail position
        packet_offset = (IPC_CMD_RING_OFFSET + RING_HEADER_SIZE +
                        (header.tail * PACKET_SIZE))
        self.shm.seek(packet_offset)
        packet_data = self.shm.read(PACKET_SIZE)
        packet = Packet.unpack(packet_data)

        # Update tail (consume the packet)
        new_tail = (header.tail + 1) % header.size
        self._write_cmd_ring_tail(new_tail)

        self.stats['commands_received'] += 1

        return packet

    def send_response(self, status: int, orig_cmd: int, result: int = 0, duration_us: int = 0):
        """
        Send a response to ZENEDGE.

        Args:
            status: Response status (RSP_OK, RSP_ERROR, etc.)
            orig_cmd: The command this is responding to
            result: Result value (e.g., blob_id for tensor results)
            duration_us: Server-side execution duration in microseconds
        """
        header = self._read_rsp_ring_header()

        if header.magic != IPC_RSP_MAGIC:
            print("[BRIDGE] ERROR: Response ring not initialized")
            return

        # Check if ring is full
        next_head = (header.head + 1) % header.size
        if next_head == header.tail:
            print("[BRIDGE] ERROR: Response ring is full")
            return

        # Create response
        response = Response(
            status=status,
            orig_cmd=orig_cmd,
            result=result,
            timestamp=duration_us if duration_us > 0 else get_timestamp()
        )

        # Write to ring at head position
        response_offset = (IPC_RSP_RING_OFFSET + RING_HEADER_SIZE +
                         (header.head * RESPONSE_SIZE))
        self.shm.seek(response_offset)
        self.shm.write(response.pack())

        # Update head
        self._write_rsp_ring_header(next_head)

        # Ring doorbell
        self._write_rsp_doorbell(next_head)

        self.stats['responses_sent'] += 1

    def register_handler(self, cmd: int, handler: Callable):
        """
        Register a handler function for a command type.

        Handler signature: handler(bridge, packet) -> (status, result)
        """
        self.handlers[cmd] = handler

    def dispatch(self, packet: Packet) -> Tuple[int, int, int]:
        """
        Dispatch a packet to its handler.

        Returns:
            (status, result, duration_us) tuple
        """
        cmd_name = CMD_NAMES.get(packet.cmd, f"UNKNOWN({packet.cmd:#06x})")
        print(f"[BRIDGE] Received: {cmd_name} payload={packet.payload_id} "
              f"flags={packet.flags:#06x}")

        if packet.cmd in self.handlers:
            try:
                t_start = time.time()
                status, result = self.handlers[packet.cmd](self, packet)
                t_end = time.time()
                duration_us = int((t_end - t_start) * 1_000_000)
                return status, result, duration_us
            except Exception as e:
                print(f"[BRIDGE] Handler error for {cmd_name}: {e}")
                self.stats['errors'] += 1
                return RSP_ERROR, 0, 0
        else:
            print(f"[BRIDGE] No handler for {cmd_name}")
            return RSP_ERROR, 0, 0

    def run(self, poll_interval: float = 0.001):
        """
        Main event loop - poll for commands and dispatch to handlers.

        Args:
            poll_interval: Time to sleep between polls (seconds)
        """
        self.running = True
        self.stats['start_time'] = time.time()

        print("[BRIDGE] Starting main loop (Ctrl+C to stop)")
        print(f"[BRIDGE] Poll interval: {poll_interval*1000:.1f}ms")

        try:
            while self.running:
                packet = self.poll_command()

                if packet is not None:
                    status, result, duration_us = self.dispatch(packet)
                    self.send_response(status, packet.cmd, result, duration_us)
                else:
                    time.sleep(poll_interval)

        except KeyboardInterrupt:
            print("\n[BRIDGE] Interrupted by user")
        finally:
            self.running = False
            self._print_stats()

    def run_once(self) -> bool:
        """
        Process one command if available.

        Returns:
            True if a command was processed, False otherwise
        """
        packet = self.poll_command()
        if packet is not None:
            status, result, duration_us = self.dispatch(packet)
            self.send_response(status, packet.cmd, result, duration_us)
            return True
        return False

    def _print_stats(self):
        """Print session statistics."""
        elapsed = time.time() - self.stats['start_time'] if self.stats['start_time'] else 0
        print(f"\n[BRIDGE] Session statistics:")
        print(f"  Duration: {elapsed:.1f}s")
        print(f"  Commands received: {self.stats['commands_received']}")
        print(f"  Responses sent: {self.stats['responses_sent']}")
        print(f"  Errors: {self.stats['errors']}")
        if elapsed > 0:
            rate = self.stats['commands_received'] / elapsed
            print(f"  Rate: {rate:.1f} cmd/s")

    def close(self):
        """Clean up resources."""
        if hasattr(self, 'shm') and self.shm:
            self.shm.close()
        if hasattr(self, 'fd') and self.fd:
            os.close(self.fd)
        print("[BRIDGE] Closed")


def main():
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="ZENEDGE Bridge - Linux sidecar for ZENEDGE kernel"
    )
    parser.add_argument(
        "--shm", "-s",
        default="/dev/shm/zenedge.shm",
        help="Path to shared memory file (default: /dev/shm/zenedge.shm)"
    )
    parser.add_argument(
        "--models", "-m",
        default="./models",
        help="Path to model directory (default: ./models)"
    )
    parser.add_argument(
        "--create", "-c",
        action="store_true",
        help="Create shared memory file if it doesn't exist"
    )
    parser.add_argument(
        "--poll-interval", "-p",
        type=float,
        default=0.001,
        help="Poll interval in seconds (default: 0.001)"
    )

    args = parser.parse_args()

    # Import handlers here to avoid circular import
    from .handlers import register_all_handlers

    try:
        bridge = ZenedgeBridge(
            shm_path=args.shm,
            model_dir=args.models,
            create=args.create
        )

        # Register command handlers
        register_all_handlers(bridge)

        # Run main loop
        bridge.run(poll_interval=args.poll_interval)

    except FileNotFoundError as e:
        print(f"[BRIDGE] Error: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"[BRIDGE] Fatal error: {e}")
        raise


if __name__ == "__main__":
    main()
