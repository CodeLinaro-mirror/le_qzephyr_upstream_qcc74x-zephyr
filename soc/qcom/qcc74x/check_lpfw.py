# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3
"""Reject an LPFW binary whose header or shared ABI cannot match this SoC."""

import argparse
from pathlib import Path
import struct


def validate(data):
    if len(data) < 40:
        raise ValueError("LPFW header is truncated")
    _, magic, start, code_end, memory_end = struct.unpack_from("<5I", data)
    if magic != 0x7766706C:
        raise ValueError("LPFW magic mismatch")
    if not start == 0x63010000 < code_end <= memory_end <= start + 28 * 1024:
        raise ValueError("LPFW does not fit the approved 28 KiB window")
    if code_end - start != len(data):
        raise ValueError("LPFW payload length does not match code_end")
    if data[20:].split(b"\0", 1)[0] != b"qcc74x-pds1-abi2":
        raise ValueError("LPFW shared ABI mismatch; rebuild APP and LPFW together")
    return memory_end - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blob", type=Path)
    args = parser.parse_args()
    try:
        used = validate(args.blob.read_bytes())
    except (OSError, ValueError) as exc:
        parser.exit(1, f"LPFW validation failed: {exc}\n")
    print(f"LPFW validated: {used}/28672 bytes including BSS and stack")


if __name__ == "__main__":
    main()
