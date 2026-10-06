#!/usr/bin/env python3
"""Materialize the pinned SDK once without resetting its working lockfiles."""

import hashlib
from pathlib import Path
import shutil
import stat
import sys


def main() -> None:
    source = Path(sys.argv[1]).resolve()
    sdk = Path(sys.argv[2]).resolve()
    files = []
    fingerprint = hashlib.sha256()
    for path in sorted(source.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Unexpected symlink in the pinned source snapshot: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(source)
        content = path.read_bytes()
        executable = bool(path.stat().st_mode & stat.S_IXUSR)
        fingerprint.update(relative.as_posix().encode() + b"\0")
        fingerprint.update(bytes([executable]))
        fingerprint.update(hashlib.sha256(content).digest())
        files.append((path, relative, content, executable))

    stamp = sdk / ".cosmo-source-stamp"
    expected = fingerprint.hexdigest() + "\n"
    if stamp.exists():
        if stamp.read_text() != expected:
            raise RuntimeError(
                "The vendored SDK changed. Choose a new COSMO_RUST_BUILD_ROOT; "
                "existing SDK sources and lockfiles were left unchanged."
            )
        return

    # An old or partially materialized SDK can be adopted only when all source
    # files already present match the pinned snapshot. Never silently reset it.
    for path, relative, content, executable in files:
        destination = sdk / relative
        destination.parent.resolve().relative_to(sdk)
        if destination.is_symlink():
            raise RuntimeError(f"Unexpected symlink in staged SDK source: {destination}")
        if destination.exists():
            if (not destination.is_file() or destination.read_bytes() != content or
                    bool(destination.stat().st_mode & stat.S_IXUSR) != executable):
                raise RuntimeError(f"Staged SDK source differs; refusing to reset {destination}")

    for path, relative, _, _ in files:
        destination = sdk / relative
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    stamp.write_text(expected)


if __name__ == "__main__":
    main()
