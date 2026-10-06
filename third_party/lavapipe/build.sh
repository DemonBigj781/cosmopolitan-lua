#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
lavapipe_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$lavapipe_dir/build.py" "$@"
