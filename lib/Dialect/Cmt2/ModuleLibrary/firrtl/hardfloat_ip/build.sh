#!/usr/bin/env bash
set -euo pipefail
# This adapter belongs to AQUAS's CMT2 library, not upstream standalone CIRCT.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../../.." && pwd)"
exec bash "$ROOT/scripts/build-hardfloat-ip.sh" "$@"
