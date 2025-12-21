"""
Arbiter integration helpers.
"""

import base64
import json
import os
import urllib.request
from typing import Any, Dict

from .ifr import parse_ifr_blob


def query_next_profile(ifr_raw: bytes, ifr_record: Dict[str, Any]) -> Dict[str, Any]:
    url = os.getenv("ZENEDGE_ARBITER_URL", "").strip()
    if url:
        try:
            payload = json.dumps({
                "ifr_b64": base64.b64encode(ifr_raw).decode("ascii"),
                "ifr": ifr_record,
            }).encode("utf-8")
            req = urllib.request.Request(
                url,
                data=payload,
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=2) as resp:
                body = resp.read().decode("utf-8")
                data = json.loads(body) if body else {}
                if isinstance(data, dict):
                    return data
        except Exception as exc:
            print(f"[ARBITER] request failed: {exc}")

    profile_env = os.getenv("ZENEDGE_ARB_PROFILE", "").strip()
    if profile_env:
        try:
            vals = [float(x) for x in profile_env.split(",") if x.strip()]
            if vals:
                return {"profile": vals}
        except Exception as exc:
            print(f"[ARBITER] invalid ZENEDGE_ARB_PROFILE: {exc}")

    return {"profile": None}


def verify_ifr_archive(out_dir: str = "/tmp/zenedge_ifr") -> None:
    if not os.path.isdir(out_dir):
        return

    bin_files = [f for f in os.listdir(out_dir) if f.endswith(".bin")]
    if not bin_files:
        return

    latest = max(bin_files, key=lambda f: os.path.getmtime(os.path.join(out_dir, f)))
    path = os.path.join(out_dir, latest)
    try:
        with open(path, "rb") as f:
            data = f.read()
        rec = parse_ifr_blob(data)
        if not rec:
            print(f"[ARBITER] IFR verify failed: {path}")
        elif rec["hash_ok"]:
            print(f"[ARBITER] IFR verify ok: {path}")
        else:
            print(f"[ARBITER] IFR hash mismatch: {path}")
    except Exception as exc:
        print(f"[ARBITER] IFR verify error: {exc}")
