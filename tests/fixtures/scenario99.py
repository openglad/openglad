#!/usr/bin/env python3
"""Produce the historical parity scen99 without running a gameplay test.

The 163 bytes are the v9 scenario exported by LevelDataOps before extraction:
Coverage Level, three default-position objects (OB soldier, FX flash, WP knife),
and two description lines. This is an input fixture, not a replay golden.
"""
import hashlib
from pathlib import Path
import struct
import sys


def scenario_bytes():
    header = struct.pack("<3sB8s30sBhhH", b"FSS", 9, b"grid",
                         b"Coverage Level", 3, 7, 1234, 3)
    objects = b"".join(
        struct.pack("<BBhhBBBh12s10s", order, family, 0, 0, 0, 4,
                    command, 1, name, b"MSTRMSTRMS")
        for order, family, command, name in (
            (0, 0, 0, b"OB"), (4, 4, 0, b"FX"), (1, 0, 1, b"WP")))
    return header + objects + b"\x02\x06desc-a\x06desc-b"


def main():
    path = Path(sys.argv[2]) / "temp/scen/scen99.fss"
    if sys.argv[1] == "cleanup":
        path.unlink(missing_ok=True)
        return
    if sys.argv[1] != "setup":
        raise SystemExit("expected setup or cleanup")
    data = scenario_bytes()
    # Pin the independently captured pre-extraction fixture, including object
    # defaults: a constructor change must not silently rewrite parity inputs.
    expected = "c73df8880246395538f9a5a0e073a14d1b247f3470c8465d0d4e5473b2966e2d"
    if hashlib.sha256(data).hexdigest() != expected:
        raise SystemExit("scen99 differs from the historical fixture")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    if path.read_bytes() != data:
        raise SystemExit("scen99 fixture write was incomplete")
    print(f"scen99: {len(data)} historical bytes installed")


if __name__ == "__main__":
    main()
