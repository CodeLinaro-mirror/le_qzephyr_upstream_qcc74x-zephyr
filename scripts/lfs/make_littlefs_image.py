# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause

#!/usr/bin/env python3
"""Create a LittleFS filesystem image from files and directory contents."""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile


def parse_size(value: str) -> int:
    """Parse a byte count or a binary K/M/G suffix."""
    suffixes = {
        "": 1,
        "K": 1024,
        "KI": 1024,
        "KIB": 1024,
        "M": 1024 * 1024,
        "MI": 1024 * 1024,
        "MIB": 1024 * 1024,
        "G": 1024 * 1024 * 1024,
        "GI": 1024 * 1024 * 1024,
        "GIB": 1024 * 1024 * 1024,
    }
    text = value.strip().upper()
    number_end = 0

    while number_end < len(text) and text[number_end].isdigit():
        number_end += 1

    try:
        number = int(text[:number_end])
        multiplier = suffixes[text[number_end:]]
    except (KeyError, ValueError):
        raise argparse.ArgumentTypeError(
            "size must be a positive byte count or use K, M, or G suffixes "
            "(for example: 2097152, 2M, or 2MiB)"
        ) from None

    size = number * multiplier
    if size <= 0:
        raise argparse.ArgumentTypeError("size must be positive")

    return size


def copy_to_staging(source: pathlib.Path, destination: pathlib.Path,
                    origins: dict[pathlib.Path, pathlib.Path]) -> None:
    """Copy one file and reject target-path collisions."""
    if destination in origins:
        raise ValueError(
            f"LittleFS path collision: {destination} is provided by both "
            f"{origins[destination]} and {source}"
        )

    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    origins[destination] = source


def stage_file(source: pathlib.Path, staging: pathlib.Path,
               origins: dict[pathlib.Path, pathlib.Path]) -> None:
    if not source.is_file():
        raise ValueError(f"input file does not exist or is not a regular file: {source}")

    copy_to_staging(source, staging / source.name, origins)


def stage_directory(source: pathlib.Path, staging: pathlib.Path,
                    origins: dict[pathlib.Path, pathlib.Path]) -> None:
    """Copy the contents, rather than the directory itself, into the image."""
    if not source.is_dir():
        raise ValueError(f"input path does not exist or is not a directory: {source}")

    for child in sorted(source.rglob("*")):
        if child.is_dir():
            continue
        if not child.is_file():
            raise ValueError(f"input path contains a non-regular file: {child}")

        copy_to_staging(child, staging / child.relative_to(source), origins)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-f", "--file", action="append", default=[], type=pathlib.Path,
        help="file to add at the LittleFS root using its basename (repeatable)",
    )
    parser.add_argument(
        "-p", "--path", action="append", default=[], type=pathlib.Path,
        help="directory whose contents are recursively added at the LittleFS root "
             "(repeatable)",
    )
    parser.add_argument(
        "-s", "--size", required=True, type=parse_size,
        help="LittleFS image size: bytes or K/M/G suffix (for example: 2M)",
    )
    parser.add_argument(
        "-o", "--output", required=True, type=pathlib.Path,
        help="output LittleFS image path",
    )
    parser.add_argument(
        "--block-size", type=parse_size, default=4096,
        help="LittleFS erase block size (default: 4096)",
    )
    parser.add_argument(
        "--read-size", type=parse_size, default=16,
        help="LittleFS read size (default: 16)",
    )
    parser.add_argument(
        "--prog-size", type=parse_size, default=16,
        help="LittleFS program size (default: 16)",
    )
    parser.add_argument(
        "--mklittlefs", default="mklittlefs",
        help="mklittlefs executable (default: search PATH)",
    )
    args = parser.parse_args()

    if not args.file and not args.path:
        parser.error("at least one --file or --path input is required")
    if args.size % args.block_size:
        parser.error("--size must be an exact multiple of --block-size")

    tool = shutil.which(args.mklittlefs)
    if tool is None:
        parser.error(
            "mklittlefs is required to create a mountable LittleFS image; "
            "install it or pass --mklittlefs /path/to/mklittlefs"
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="qcc74x_lfs_") as temp_dir:
        staging = pathlib.Path(temp_dir)
        origins: dict[pathlib.Path, pathlib.Path] = {}

        try:
            for source in args.file:
                stage_file(source, staging, origins)
            for source in args.path:
                stage_directory(source, staging, origins)
        except ValueError as error:
            parser.error(str(error))

        subprocess.run([
            tool,
            "-c", str(staging),
            "-s", str(args.size),
            "-b", str(args.block_size),
            "-r", str(args.read_size),
            "-p", str(args.prog_size),
            str(args.output),
        ], check=True)

    if args.output.stat().st_size != args.size:
        raise RuntimeError(
            f"mklittlefs produced {args.output.stat().st_size} bytes, expected "
            f"{args.size}"
        )

    print(f"created {args.output} ({args.size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
