"""
IFR parsing and verification helpers.
"""

import hashlib
from typing import Optional, Dict, Any

from .protocol import (
    IFR_STRUCT,
    IFR_MAGIC,
    IFR_VERSION,
    IFR_PROFILE_MAX,
    IFR_HASH_OFFSET,
)


def parse_ifr_blob(data: bytes) -> Optional[Dict[str, Any]]:
    if not data or len(data) < IFR_STRUCT.size:
        return None

    unpacked = IFR_STRUCT.unpack(data[:IFR_STRUCT.size])
    (magic, version, flags, job_id, episode_id, model_id, record_size, ts_usec,
     goodput, profile_len, _reserved, *rest) = unpacked

    profile = rest[:IFR_PROFILE_MAX]
    hash_bytes = rest[IFR_PROFILE_MAX]

    if magic != IFR_MAGIC:
        return None
    if version != IFR_VERSION:
        return None
    if record_size != IFR_STRUCT.size:
        return None
    if profile_len > IFR_PROFILE_MAX:
        return None

    expected = hashlib.sha256(data[:IFR_HASH_OFFSET]).digest()
    hash_ok = (expected == hash_bytes)

    return {
        "magic": magic,
        "version": version,
        "flags": flags,
        "job_id": job_id,
        "episode_id": episode_id,
        "model_id": model_id,
        "record_size": record_size,
        "ts_usec": ts_usec,
        "goodput": goodput,
        "profile_len": profile_len,
        "profile": list(profile[:profile_len]),
        "hash": hash_bytes,
        "hash_ok": hash_ok,
    }
