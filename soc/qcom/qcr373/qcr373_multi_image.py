#!/usr/bin/env python3
# Copyright (c) 2026 Qualcomm
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import struct
from pathlib import Path

DESCRIPTOR_OFFSET = 0x300
ALIGNMENT = 0x400
NP_HEADER_MAGIC = b"BFNP"


def align_up(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def combine(ap_path, np_path, output_path):
    ap = bytearray(ap_path.read_bytes())
    np_image = np_path.read_bytes()

    if np_image[:4] != NP_HEADER_MAGIC:
        raise ValueError("NP image does not contain a QCR373 firmware header")
    if len(ap) < DESCRIPTOR_OFFSET + 48:
        raise ValueError("AP image is too small for its image descriptor")

    self_name, _, _ = struct.unpack_from("<8sII", ap, DESCRIPTOR_OFFSET)
    np_name, _, _ = struct.unpack_from("<8sII", ap, DESCRIPTOR_OFFSET + 16)
    if self_name.rstrip(b"\0") != b"SELF" or np_name.rstrip(b"\0") != b"NP":
        raise ValueError("AP image descriptor table is missing")

    np_start = align_up(len(ap), ALIGNMENT)
    np_end = np_start + len(np_image)
    ap.extend(b"\0" * (np_start - len(ap)))
    ap.extend(np_image)

    struct.pack_into("<8sII", ap, DESCRIPTOR_OFFSET,
                     b"SELF\0\0\0\0", 0, np_start)
    struct.pack_into("<8sII", ap, DESCRIPTOR_OFFSET + 16,
                     b"NP\0\0\0\0\0\0", np_start, np_end)
    output_path.write_bytes(ap)

    print(f"Combined QCR373 image: AP=0x0..0x{np_start:x}, "
          f"NP=0x{np_start:x}..0x{np_end:x}")


def main():
    parser = argparse.ArgumentParser(description="Combine QCR373 CPU images")
    parser.add_argument("--ap", required=True, type=Path)
    parser.add_argument("--np", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    combine(args.ap, args.np, args.output)


if __name__ == "__main__":
    main()
