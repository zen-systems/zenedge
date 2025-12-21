"""
ZENEDGE Shared Heap Manager

Manages blob and tensor read/write operations in the shared memory heap.
The heap uses a bitmap allocator with 64-byte blocks.
"""

import mmap
from typing import Optional, Dict, Tuple
import numpy as np

from .protocol import (
    IPC_HEAP_CTL_OFFSET,
    IPC_HEAP_DATA_OFFSET,
    IPC_HEAP_DATA_SIZE,
    HEAP_BLOCK_SIZE,
    HEAP_MAX_BLOCKS,
    HEAP_BITMAP_SIZE,
    HEAP_CTL_HEADER_SIZE,
    BLOB_MAGIC,
    IPC_HEAP_MAGIC,
    BLOB_TYPE_TENSOR,
    BLOB_TYPE_RESULT,
    BLOB_HEADER_SIZE,
    TENSOR_HEADER_SIZE,
    DTYPE_TO_NUMPY,
    NUMPY_TO_DTYPE,
    DTYPE_SIZES,
    BlobHeader,
    TensorHeader,
    HeapControl,
    compute_checksum,
)


class HeapManager:
    """
    Manages the shared heap region for blob and tensor storage.

    The heap layout:
    - Control block at IPC_HEAP_CTL_OFFSET (magic, counters, bitmap)
    - Data region at IPC_HEAP_DATA_OFFSET (blob headers + data)
    """

    def __init__(self, shm: mmap.mmap):
        self.shm = shm
        self.ctl_offset = IPC_HEAP_CTL_OFFSET
        self.data_offset = IPC_HEAP_DATA_OFFSET
        self.data_size = IPC_HEAP_DATA_SIZE

        # Cache of known blob locations: blob_id -> offset from data_offset
        self._blob_cache: Dict[int, int] = {}

    def _read_heap_control(self) -> HeapControl:
        """Read the heap control block."""
        self.shm.seek(self.ctl_offset)
        data = self.shm.read(HEAP_CTL_HEADER_SIZE)
        return HeapControl.unpack(data)

    def _read_bitmap(self) -> bytes:
        """Read the heap bitmap."""
        self.shm.seek(self.ctl_offset + HEAP_CTL_HEADER_SIZE)
        return self.shm.read(HEAP_BITMAP_SIZE)

    def _write_bitmap(self, bitmap: bytes):
        """Write the heap bitmap."""
        self.shm.seek(self.ctl_offset + HEAP_CTL_HEADER_SIZE)
        self.shm.write(bitmap)

    def _update_heap_control(self, free_blocks: int, next_blob_id: int):
        """Update free_blocks and next_blob_id in heap control."""
        # Read current control to preserve other fields
        ctl = self._read_heap_control()

        # Write back with updated values
        self.shm.seek(self.ctl_offset)
        import struct
        from .protocol import HEAP_CTL_STRUCT
        data = HEAP_CTL_STRUCT.pack(
            ctl.magic, ctl.version, ctl.total_blocks,
            free_blocks, next_blob_id, 0, 0, 0
        )
        self.shm.write(data)

    def _find_blob_offset(self, blob_id: int) -> Optional[int]:
        """
        Find a blob by ID by scanning the heap data region.
        Returns offset from data_offset, or None if not found.
        """
        # Check cache first
        if blob_id in self._blob_cache:
            return self._blob_cache[blob_id]

        # Scan the heap data region for blobs
        offset = 0
        while offset < self.data_size - BLOB_HEADER_SIZE:
            self.shm.seek(self.data_offset + offset)
            header_data = self.shm.read(BLOB_HEADER_SIZE)

            if len(header_data) < BLOB_HEADER_SIZE:
                break

            header = BlobHeader.unpack(header_data)

            if header.magic == BLOB_MAGIC:
                # Valid blob found
                self._blob_cache[header.blob_id] = offset

                if header.blob_id == blob_id:
                    return offset

                # Skip to next potential blob (aligned to block size)
                blob_total_size = BLOB_HEADER_SIZE + header.size
                blocks_used = (blob_total_size + HEAP_BLOCK_SIZE - 1) // HEAP_BLOCK_SIZE
                offset += blocks_used * HEAP_BLOCK_SIZE
            else:
                # Not a valid blob header, skip one block
                offset += HEAP_BLOCK_SIZE

        return None

    def read_blob_header(self, blob_id: int) -> Optional[BlobHeader]:
        """Read a blob header by ID."""
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            return None

        self.shm.seek(self.data_offset + offset)
        data = self.shm.read(BLOB_HEADER_SIZE)
        return BlobHeader.unpack(data)

    def read_blob_data(self, blob_id: int) -> Optional[bytes]:
        """Read raw blob data (excluding header)."""
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            return None

        self.shm.seek(self.data_offset + offset)
        header_data = self.shm.read(BLOB_HEADER_SIZE)
        header = BlobHeader.unpack(header_data)

        if header.magic != BLOB_MAGIC:
            return None

        # Read data following the header
        self.shm.seek(self.data_offset + offset + BLOB_HEADER_SIZE)
        return self.shm.read(header.size)

    def read_tensor(self, blob_id: int) -> Optional[np.ndarray]:
        """
        Read a tensor blob and return as numpy array.

        The blob structure for BLOB_TYPE_TENSOR:
        - BlobHeader (32 bytes)
        - TensorHeader (40 bytes)
        - Raw tensor data
        """
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            print(f"[HEAP] Blob {blob_id} not found")
            return None

        # Read blob header
        self.shm.seek(self.data_offset + offset)
        blob_header_data = self.shm.read(BLOB_HEADER_SIZE)
        blob_header = BlobHeader.unpack(blob_header_data)

        if blob_header.magic != BLOB_MAGIC:
            print(f"[HEAP] Invalid blob magic for blob {blob_id}")
            return None

        if blob_header.type != BLOB_TYPE_TENSOR:
            print(f"[HEAP] Blob {blob_id} is not a tensor (type={blob_header.type})")
            return None

        # Read tensor header (immediately after blob header)
        tensor_header_data = self.shm.read(TENSOR_HEADER_SIZE)
        tensor_header = TensorHeader.unpack(tensor_header_data)

        # Get numpy dtype
        if tensor_header.dtype not in DTYPE_TO_NUMPY:
            print(f"[HEAP] Unknown tensor dtype {tensor_header.dtype}")
            return None

        numpy_dtype = DTYPE_TO_NUMPY[tensor_header.dtype]

        # Calculate expected data size
        num_elements = 1
        for i in range(tensor_header.ndim):
            num_elements *= tensor_header.shape[i]

        element_size = DTYPE_SIZES[tensor_header.dtype]
        data_size = num_elements * element_size

        # Read tensor data (Zero-Copy)
        start = self.data_offset + offset + BLOB_HEADER_SIZE + TENSOR_HEADER_SIZE
        end = start + data_size
        
        # Create a view directly into the mmap
        # Note: numpy.frombuffer with mmap or memoryview creates a read-only array sharing memory
        try:
             # Use memoryview to slice without copy
             mem_view = memoryview(self.shm)[start:end]
             arr = np.frombuffer(mem_view, dtype=numpy_dtype)
             arr = arr.reshape(tensor_header.shape)
             
             # Don't copy unless necessary (e.g. if we need to write to it and mmap is RO? 
             # mmap is usually RW. BUT np.frombuffer might return read-only if source is.
             # We want zero-copy read for inference input.)
             return arr
        except Exception as e:
             print(f"[HEAP] Zero-copy read failed: {e}, falling back to copy")
             self.shm.seek(start)
             data = self.shm.read(data_size)
             arr = np.frombuffer(data, dtype=numpy_dtype)
             arr = arr.reshape(tensor_header.shape)
             return arr

    def write_blob_data(self, blob_id: int, data: bytes) -> bool:
        """
        Write data to an existing blob (overwrites data portion only).
        Used when ZENEDGE allocates the blob and Python fills it.
        """
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            print(f"[HEAP] Blob {blob_id} not found for write")
            return False

        # Read header to verify
        self.shm.seek(self.data_offset + offset)
        header_data = self.shm.read(BLOB_HEADER_SIZE)
        header = BlobHeader.unpack(header_data)

        if header.magic != BLOB_MAGIC:
            print(f"[HEAP] Invalid blob magic for blob {blob_id}")
            return False

        if len(data) > header.size:
            print(f"[HEAP] Data too large ({len(data)} > {header.size})")
            return False

        # Write data
        self.shm.seek(self.data_offset + offset + BLOB_HEADER_SIZE)
        self.shm.write(data)

        # Update checksum in header
        header.checksum = compute_checksum(data)
        self.shm.seek(self.data_offset + offset)
        self.shm.write(header.pack())

        return True

    def write_tensor_to_blob(self, blob_id: int, arr: np.ndarray) -> bool:
        """
        Write a numpy array to an existing tensor blob.
        The blob must already be allocated with BLOB_TYPE_TENSOR.
        """
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            print(f"[HEAP] Blob {blob_id} not found for tensor write")
            return False

        # Read blob header
        self.shm.seek(self.data_offset + offset)
        header_data = self.shm.read(BLOB_HEADER_SIZE)
        header = BlobHeader.unpack(header_data)

        if header.type != BLOB_TYPE_TENSOR:
            print(f"[HEAP] Blob {blob_id} is not a tensor")
            return False

        # Get dtype
        dtype_str = str(arr.dtype)
        if dtype_str not in NUMPY_TO_DTYPE:
            print(f"[HEAP] Unsupported numpy dtype {dtype_str}")
            return False

        dtype = NUMPY_TO_DTYPE[dtype_str]
        ndim = len(arr.shape)

        if ndim > 4:
            print(f"[HEAP] Too many dimensions ({ndim} > 4)")
            return False

        # Calculate strides in bytes
        strides = tuple(s * arr.itemsize // arr.itemsize for s in arr.strides)
        # Actually, strides should be in bytes
        strides = arr.strides

        # Create tensor header
        tensor_header = TensorHeader(dtype, ndim, arr.shape, strides)

        # Check if data fits
        tensor_data = arr.tobytes()
        total_size = TENSOR_HEADER_SIZE + len(tensor_data)

        if total_size > header.size:
            print(f"[HEAP] Tensor data too large ({total_size} > {header.size})")
            return False

        # Write tensor header
        self.shm.seek(self.data_offset + offset + BLOB_HEADER_SIZE)
        self.shm.write(tensor_header.pack())

        # Write tensor data
        self.shm.write(tensor_data)

        # Update blob header checksum
        all_data = tensor_header.pack() + tensor_data
        header.checksum = compute_checksum(all_data)
        self.shm.seek(self.data_offset + offset)
        self.shm.write(header.pack())

        return True

    def allocate_blob(self, size: int, blob_type: int = BLOB_TYPE_RESULT) -> Optional[int]:
        """
        Allocate a new blob in the heap.

        This modifies the bitmap and heap control block.
        Returns blob_id on success, None on failure.
        """
        # Calculate blocks needed (including blob header)
        total_size = BLOB_HEADER_SIZE + size
        blocks_needed = (total_size + HEAP_BLOCK_SIZE - 1) // HEAP_BLOCK_SIZE

        # Read current heap state
        ctl = self._read_heap_control()

        if ctl.magic != IPC_HEAP_MAGIC:
            print("[HEAP] Heap not initialized (invalid magic)")
            return None

        if ctl.free_blocks < blocks_needed:
            print(f"[HEAP] Not enough free blocks ({ctl.free_blocks} < {blocks_needed})")
            return None

        # Read bitmap
        bitmap = bytearray(self._read_bitmap())

        # Find contiguous free blocks
        start_block = self._find_free_blocks(bitmap, blocks_needed)
        if start_block is None:
            print(f"[HEAP] No contiguous region of {blocks_needed} blocks")
            return None

        # Mark blocks as used
        for i in range(blocks_needed):
            block = start_block + i
            byte_idx = block // 8
            bit_idx = block % 8
            bitmap[byte_idx] |= (1 << bit_idx)

        # Get next blob ID
        blob_id = ctl.next_blob_id
        if blob_id == 0:
            blob_id = 1  # IDs start at 1

        # Calculate offset in data region
        data_offset = start_block * HEAP_BLOCK_SIZE

        # Create blob header
        header = BlobHeader(
            magic=BLOB_MAGIC,
            blob_id=blob_id,
            type=blob_type,
            flags=0,
            size=size,
            offset=data_offset,
            checksum=0
        )

        # Write blob header
        self.shm.seek(self.data_offset + data_offset)
        self.shm.write(header.pack())

        # Write updated bitmap
        self._write_bitmap(bytes(bitmap))

        # Update heap control
        self._update_heap_control(
            free_blocks=ctl.free_blocks - blocks_needed,
            next_blob_id=blob_id + 1
        )

        # Cache the new blob location
        self._blob_cache[blob_id] = data_offset

        print(f"[HEAP] Allocated blob {blob_id}: {blocks_needed} blocks at offset {data_offset:#x}")
        return blob_id

    def _find_free_blocks(self, bitmap: bytearray, count: int) -> Optional[int]:
        """Find a contiguous run of 'count' free blocks in the bitmap."""
        run_start = None
        run_length = 0

        for block in range(HEAP_MAX_BLOCKS):
            byte_idx = block // 8
            bit_idx = block % 8

            if byte_idx >= len(bitmap):
                break

            is_used = (bitmap[byte_idx] >> bit_idx) & 1

            if not is_used:
                if run_start is None:
                    run_start = block
                run_length += 1

                if run_length >= count:
                    return run_start
            else:
                run_start = None
                run_length = 0

        return None

    def allocate_tensor(self, arr: np.ndarray) -> Optional[int]:
        """
        Allocate a new tensor blob and write the array to it.
        Returns blob_id on success, None on failure.
        """
        dtype_str = str(arr.dtype)
        if dtype_str not in NUMPY_TO_DTYPE:
            print(f"[HEAP] Unsupported numpy dtype {dtype_str}")
            return None

        ndim = len(arr.shape)
        if ndim > 4:
            print(f"[HEAP] Too many dimensions ({ndim} > 4)")
            return None

        # Calculate size needed
        tensor_data = arr.tobytes()
        total_size = TENSOR_HEADER_SIZE + len(tensor_data)

        # Allocate blob
        blob_id = self.allocate_blob(total_size, BLOB_TYPE_TENSOR)
        if blob_id is None:
            return None

        # Write tensor
        if not self.write_tensor_to_blob(blob_id, arr):
            # TODO: free the blob on failure
            return None

        return blob_id

    def free_blob(self, blob_id: int) -> bool:
        """Free a blob and return its blocks to the free pool."""
        offset = self._find_blob_offset(blob_id)
        if offset is None:
            print(f"[HEAP] Blob {blob_id} not found for free")
            return False

        # Read header to get size
        self.shm.seek(self.data_offset + offset)
        header_data = self.shm.read(BLOB_HEADER_SIZE)
        header = BlobHeader.unpack(header_data)

        if header.magic != BLOB_MAGIC:
            return False

        # Calculate blocks used
        total_size = BLOB_HEADER_SIZE + header.size
        blocks_used = (total_size + HEAP_BLOCK_SIZE - 1) // HEAP_BLOCK_SIZE
        start_block = offset // HEAP_BLOCK_SIZE

        # Read bitmap
        bitmap = bytearray(self._read_bitmap())

        # Clear blocks
        for i in range(blocks_used):
            block = start_block + i
            byte_idx = block // 8
            bit_idx = block % 8
            bitmap[byte_idx] &= ~(1 << bit_idx)

        # Clear blob header magic to mark as free
        self.shm.seek(self.data_offset + offset)
        self.shm.write(b'\x00' * 4)  # Clear magic

        # Write updated bitmap
        self._write_bitmap(bytes(bitmap))

        # Update heap control
        ctl = self._read_heap_control()
        self._update_heap_control(
            free_blocks=ctl.free_blocks + blocks_used,
            next_blob_id=ctl.next_blob_id
        )

        # Remove from cache
        if blob_id in self._blob_cache:
            del self._blob_cache[blob_id]

        print(f"[HEAP] Freed blob {blob_id}: {blocks_used} blocks")
        return True

    def get_stats(self) -> dict:
        """Get heap statistics."""
        ctl = self._read_heap_control()
        return {
            'magic_valid': ctl.magic == IPC_HEAP_MAGIC,
            'total_blocks': ctl.total_blocks,
            'free_blocks': ctl.free_blocks,
            'used_blocks': ctl.total_blocks - ctl.free_blocks,
            'next_blob_id': ctl.next_blob_id,
            'total_bytes': ctl.total_blocks * HEAP_BLOCK_SIZE,
            'free_bytes': ctl.free_blocks * HEAP_BLOCK_SIZE,
        }

    def clear_cache(self):
        """Clear the blob location cache."""
        self._blob_cache.clear()
