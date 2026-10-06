#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

port_dir=$(cd -- "$(dirname -- "$0")/.." && pwd)
loader_build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-wgpu-loader.XXXXXX")
trap 'rm -rf "$loader_build_dir"' EXIT HUP INT TERM
loader_cc=${CC:-cc}

"$loader_cc" -std=c11 -D_DEFAULT_SOURCE -DNDEBUG -O2 \
  -Wall -Wextra -Werror -pthread \
  -I"$port_dir/runtime" -I"$port_dir/tests/loader_stubs" \
  "$port_dir/tests/vulkan_loader.c" "$port_dir/runtime/vulkan_loader.c" \
  "$port_dir/runtime/win64_bridge.c" \
  -o "$loader_build_dir/loader"
"$loader_build_dir/loader"
