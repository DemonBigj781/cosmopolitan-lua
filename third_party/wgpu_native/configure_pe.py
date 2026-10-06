#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Give DLL-created Windows threads a usable default stack in the built APE.

Cosmopolitan 4.0.2 hardcodes a 64 KiB PE stack reserve in ape/ape.S. Its own
threads use separately allocated stacks, but native libraries can use the
executable's default. Mesa 26.1.3's src/c11/impl/threads_win32.c, for example,
calls _beginthreadex with stack_size=0. Configure the generated executable
before hashing it; the same resulting bytes are distributed to both OSes.
"""

from pathlib import Path
import struct
import sys

STACK_RESERVE = 8 * 1024 * 1024
STACK_COMMIT = 4096


def configure(path: Path) -> None:
    data = bytearray(path.read_bytes())
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("expected the generated APE with its DOS/PE header")
    pe, = struct.unpack_from("<I", data, 0x3C)
    optional = pe + 24
    if optional + 96 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing or truncated PE header")
    machine, = struct.unpack_from("<H", data, pe + 4)
    optional_size, = struct.unpack_from("<H", data, pe + 20)
    magic, = struct.unpack_from("<H", data, optional)
    if machine != 0x8664 or magic != 0x20B or optional_size < 96:
        raise ValueError("expected the pinned x86-64 PE32+ layout")
    reserve, commit = struct.unpack_from("<QQ", data, optional + 72)
    if reserve not in (65536, STACK_RESERVE) or commit != STACK_COMMIT:
        raise ValueError(f"unexpected pinned SDK stack defaults: {reserve}/{commit}")
    struct.pack_into("<Q", data, optional + 72, STACK_RESERVE)
    path.write_bytes(data)
    print(f"Windows native thread stack: reserve={STACK_RESERVE}, commit={commit}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} GENERATED_APE")
    try:
        configure(Path(sys.argv[1]))
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(f"PE stack configuration failed: {error}") from error
