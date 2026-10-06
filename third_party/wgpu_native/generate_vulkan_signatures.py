#!/usr/bin/env python3
"""Generate scalar ABI metadata from the pinned ash dispatch tables.

The hashes make this an explicit audit of ash 0.38.0+1.3.281, not a best-effort
parser for arbitrary Rust. Every procedure name must be accounted for. Vulkan
callbacks are not dispatch-table entries and need separate reverse adapters.
"""
import argparse
import hashlib
import pathlib
import re

INPUTS = {
    "tables.rs": "96348884754c9933de49f8449396796edcff91f40694bce5d382ca361260315e",
    "extensions_generated.rs": "685065d2e4a312211c70b35ee79adfad4b051ca5324127e74725a1efebed9660",
}
RETURNS = {"", "Result", "Bool32", "DeviceAddress", "PFN_vkVoidFunction",
           "u32", "u64", "DeviceSize"}
FUNCTION = re.compile(
    r'unsafe extern "system" fn\s+\w+\s*\((.*?)\)\s*'
    r'(?:->\s*([^\{]+))?\{.*?let cname\s*=\s*'
    r'CStr::from_bytes_with_nul_unchecked\(\s*b"(vk\w+)\\0"', re.S)


def parameters(text):
    depth, start = 0, 0
    for index, char in enumerate(text):
        if char in "<[":
            depth += 1
        elif char in ">]":
            depth -= 1
        elif char == "," and depth == 0:
            if text[start:index].strip():
                yield text[start:index].split(":", 1)[1].strip()
            start = index + 1
    if text[start:].strip():
        yield text[start:].split(":", 1)[1].strip()


def generate(ash):
    signatures = {}
    for filename, checksum in INPUTS.items():
        data = (ash / "src" / filename).read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise SystemExit(f"Unreviewed ash dispatch table: {filename}")
        text = data.decode()
        expected = set(re.findall(r'b"(vk\w+)\\0"', text))
        found = set()
        for match in FUNCTION.finditer(text):
            arguments, result, name = match.groups()
            if (result or "").strip() not in RETURNS:
                raise SystemExit(f"Unsupported return type for {name}: {result}")
            kinds = []
            for kind in parameters(arguments):
                # In these exact tables all by-value types other than f32 are
                # integer enums, flags, or handles of at most 64 bits. Pointers
                # carry aggregates indirectly. The source hashes guard this audit.
                kinds.append({"f32": "f", "f64": "d"}.get(kind, "i"))
            signature = "".join(kinds)
            if len(signature) > 32:
                raise SystemExit(f"Too many arguments for {name}")
            if name in signatures and signatures[name] != signature:
                raise SystemExit(f"Inconsistent signatures for {name}")
            signatures[name] = signature
            found.add(name)
        if found != expected:
            raise SystemExit(f"Unparsed Vulkan procedures: {sorted(expected - found)}")
    lines = ["/* Generated from checksum-pinned ash; do not edit. */",
             "static const struct VulkanSignature kVulkanSignatures[] = {"]
    lines.extend(f'  {{"{name}", "{sig}"}},' for name, sig in sorted(signatures.items()))
    lines.append("};")
    return "\n".join(lines) + "\n", len(signatures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ash", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    output, count = generate(args.ash)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)
    print(f"Audited {count} Vulkan procedure signatures -> {args.output}")
