#!/usr/bin/env python3
"""Materialize verified upstream sources and reviewed patches under o/webgpu."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent.parent
CRATES = {
    "ash": ("0.38.0+1.3.281", "0bb44936d800fea8f016d7f2311c6a4f97aebd5dc86f09906139ec848cf3a46f"),
    "wgpu-hal": ("29.0.3", "31f8e1a9e7a8512f276f7c62e018c7fa8d60954303fed2e5750114332049193f"),
}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def apply(path, name):
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i",
                    str(ROOT / "patches" / name)], cwd=path, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bootstrap-lock", action="store_true",
                        help="maintainer operation: start from upstream Cargo.lock")
    args = parser.parse_args()
    output = REPO / "o/webgpu"
    sources = output / "source"
    deps = output / "deps"
    deps.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / "IMPORT.json").read_text())
    for project in manifest["sources"]:
        base = ROOT / "upstream"
        if project["project"] == "webgpu-headers":
            base /= "ffi/webgpu-headers"
        for entry in project["included"]:
            file = base / entry["path"]
            if digest(file) != entry["sha256"]:
                raise SystemExit(f"Modified pristine import: {file}; put port changes in patches/")
    # These are disposable materializations; originals stay in third_party.
    if sources.exists():
        shutil.rmtree(sources)
    shutil.copytree(ROOT / "upstream", sources)
    apply(sources, "wgpu-native-cosmopolitan.patch")
    paths = {}
    for name, (version, checksum) in CRATES.items():
        archive = deps / f"{name}.crate"
        if not archive.exists():
            url = f"https://static.crates.io/crates/{name}/{name}-{version}.crate"
            temporary = archive.with_suffix(".part")
            with urllib.request.urlopen(url, timeout=120) as response, temporary.open("wb") as sink:
                shutil.copyfileobj(response, sink)
            temporary.replace(archive)
        if digest(archive) != checksum:
            raise SystemExit(f"Checksum mismatch: {archive}; remove the corrupt cached download")
        path = deps / f"{name}-{version}"
        if path.exists():
            shutil.rmtree(path)
        with tarfile.open(archive) as source:
            # Python's data filter rejects device nodes, escaping paths and links.
            source.extractall(deps, filter="data")
        apply(path, f"{name}-cosmopolitan.patch")
        paths[name] = path
    config = "[patch.crates-io]\n"
    for name, path in paths.items():
        config += f"{name} = {{ path = {json.dumps(str(path))} }}\n"
    (output / "cargo.cosmo.toml").write_text(config)
    lock = ROOT / "Cargo.cosmo.lock"
    if not args.bootstrap_lock:
        if not lock.is_file():
            raise SystemExit("Missing reviewed Cargo.cosmo.lock; do not resolve an unpinned build")
        shutil.copyfile(lock, sources / "Cargo.lock")
    subprocess.run(["python3", str(ROOT / "generate_vulkan_signatures.py"),
                    str(paths["ash"]), str(output / "generated/vulkan_signatures.inc")],
                   check=True)


if __name__ == "__main__":
    main()
