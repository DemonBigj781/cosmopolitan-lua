#!/usr/bin/env python3
"""Restore the pinned Cosmocc archive's symlinks when unzip flattened them."""

import hashlib
from pathlib import Path
import stat
import sys
import zipfile


def main() -> None:
    sdk = Path(sys.argv[1]).resolve()
    root = sdk / "vendor/cosmocc"
    archive = sdk / "cache/cosmocc-4.0.2.zip"
    pending = []
    with zipfile.ZipFile(archive) as source:
        for info in source.infolist():
            if not stat.S_ISLNK(info.external_attr >> 16):
                continue
            path = root / info.filename
            target = source.read(info).decode()
            path.parent.resolve().relative_to(root)
            (path.parent / target).resolve().relative_to(root)
            if path.is_symlink():
                if str(path.readlink()) != target:
                    raise RuntimeError(f"Unexpected SDK symlink: {path}")
                continue
            if path.exists() and path.read_bytes() != target.encode():
                raise RuntimeError(f"Unexpected SDK link placeholder: {path}")
            pending.append((path, target))

    if not pending:
        return
    expected = "85b8c37a406d862e656ad4ec14be9f6ce474c1b436b9615e91a55208aced3f44"
    digest = hashlib.sha256()
    with archive.open("rb") as data:
        for block in iter(lambda: data.read(1024 * 1024), b""):
            digest.update(block)
    actual = digest.hexdigest()
    if actual != expected:
        raise RuntimeError("The Cosmocc archive does not match the pinned SHA-256")
    for path, target in pending:
        path.unlink(missing_ok=True)
        path.symlink_to(target)
    print(f"Restored {len(pending)} compiler symlinks from the verified archive")


if __name__ == "__main__":
    main()
