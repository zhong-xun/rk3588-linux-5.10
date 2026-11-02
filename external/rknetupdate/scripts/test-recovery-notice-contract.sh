#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/rknetupdate-recovery-notice.XXXXXX)"
BIN="$TMP/recovery-notice-contract"

cleanup()
{
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

cc -std=c11 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
	"$ROOT/scripts/testdata/recovery-notice-contract.c" -o "$BIN"
TEST_ROOT="$TMP/fs" mkdir -p "$TMP/fs"
TEST_ROOT="$TMP/fs" "$BIN"
