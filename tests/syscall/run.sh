#!/usr/bin/env bash
# tests/syscall/run.sh - Syscall regression test suite
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
ROOTFS="${ROOTFS:-/}"
BIN_DIR="$ROOT/build/tests/syscall"
FAILS=0

mkdir -p "$BIN_DIR"
gcc -O2 -Wall -Wextra "$ROOT/tests/syscall/faccessat2_probe.c" -o "$BIN_DIR/faccessat2_probe"

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

echo "== Phase 9 syscall regression tests (rootfs=$ROOTFS) =="

out=$("$PROOT" -R "$ROOTFS" "$BIN_DIR/faccessat2_probe" 2>&1)
code=$?

if [[ $code -eq 0 && "$out" == *"[PASS]"* ]]; then
    pass "faccessat2 probe: all test cases passed"
else
    fail "faccessat2 probe failed (code=$code, out=$out)"
fi

echo "== syscall: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
