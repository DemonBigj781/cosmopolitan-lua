#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
llvm_cosmo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$llvm_cosmo_dir/build.py" "$@"
