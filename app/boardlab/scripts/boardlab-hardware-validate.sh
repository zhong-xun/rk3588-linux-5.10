#!/usr/bin/env sh
set -eu

# Canonical board-profile-aware validator.  Keep the historical
# rk3588-hardware-validate.sh path operational for customer scripts.
SCRIPT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")" && pwd)
exec "$SCRIPT_DIR/rk3588-hardware-validate.sh" "$@"
