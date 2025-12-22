"""
IFR parsing and verification helpers.
"""

import hashlib
from typing import Optional, Dict, Any

from .protocol import (
    IFR_V2_STRUCT,
    IFR_V3_STRUCT,
    IFR_MAGIC,
    IFR_VERSION_V2,
    IFR_VERSION_V3,
    IFR_PROFILE_MAX,
    IFR_V2_HASH_OFFSET,
    IFR_V3_HASH_OFFSET,
)


def parse_ifr_blob(data: bytes) -> Optional[Dict[str, Any]]:
    if not data or len(data) < 8:
        return None

    magic = int.from_bytes(data[0:4], "little")
    version = int.from_bytes(data[4:6], "little")

    if magic != IFR_MAGIC:
        return None

    if version == IFR_VERSION_V2 and len(data) >= IFR_V2_STRUCT.size:
        unpacked = IFR_V2_STRUCT.unpack(data[:IFR_V2_STRUCT.size])
        (magic, version, flags, job_id, episode_id, model_id, record_size, ts_usec,
         goodput, profile_len, _reserved, *rest) = unpacked
        profile = rest[:IFR_PROFILE_MAX]
        hash_bytes = rest[IFR_PROFILE_MAX]

        if record_size != IFR_V2_STRUCT.size or profile_len > IFR_PROFILE_MAX:
            return None

        expected = hashlib.sha256(data[:IFR_V2_HASH_OFFSET]).digest()
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

    if version == IFR_VERSION_V3 and len(data) >= IFR_V3_STRUCT.size:
        unpacked = IFR_V3_STRUCT.unpack(data[:IFR_V3_STRUCT.size])
        (magic, version, flags, record_size, job_id, episode_id, model_id,
         ts_usec, goodput, nonce, model_digest, policy_digest,
         flightrec_seal_hash, prev_chain_hash, ifr_hash, chain_hash,
         sig_classical) = unpacked

        if record_size != IFR_V3_STRUCT.size:
            return None

        expected_ifr = hashlib.sha256(data[:IFR_V3_HASH_OFFSET]).digest()
        ifr_ok = (expected_ifr == ifr_hash)

        chain_ctx = hashlib.sha256()
        chain_ctx.update(prev_chain_hash)
        chain_ctx.update(ifr_hash)
        chain_ctx.update(flightrec_seal_hash)
        chain_ctx.update(nonce)
        chain_ctx.update(model_digest)
        chain_ctx.update(policy_digest)
        expected_chain = chain_ctx.digest()
        chain_ok = (expected_chain == chain_hash)

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
            "nonce": nonce,
            "model_digest": model_digest,
            "policy_digest": policy_digest,
            "flightrec_seal_hash": flightrec_seal_hash,
            "prev_chain_hash": prev_chain_hash,
            "ifr_hash": ifr_hash,
            "chain_hash": chain_hash,
            "sig_classical": sig_classical,
            "hash_ok": ifr_ok and chain_ok,
            "chain_ok": chain_ok,
        }

    return None
