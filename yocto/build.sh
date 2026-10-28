#!/usr/bin/env bash
set -eo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$script_dir"

source sources/poky/oe-init-build-env build >/dev/null
set -u
exec bitbake pb2-image-minimal
