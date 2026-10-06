#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

port_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
abi_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-wgpu-abi.XXXXXX")
trap 'rm -rf "$abi_build_dir"' EXIT HUP INT TERM
abi_cc=${CC:-cc}

"$abi_cc" -std=c11 -D_DEFAULT_SOURCE -DNDEBUG -O2 \
  -Wall -Wextra -Werror \
  -I"$port_dir/runtime" \
  "$port_dir/tests/abi.c" "$port_dir/runtime/win64_bridge.c" \
  -o "$abi_build_dir/abi"
"$abi_build_dir/abi"
