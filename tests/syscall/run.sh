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
gcc -O2 -Wall -Wextra "$ROOT/tests/syscall/minimal_syscall_harness.c" -o "$BIN_DIR/minimal_syscall_harness"

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

echo "== LinuxDroid Syscall Regression Tests (rootfs=$ROOTFS) =="

out=$("$PROOT" -R "$ROOTFS" "$BIN_DIR/faccessat2_probe" 2>&1)
code=$?

if [[ $code -eq 0 && "$out" == *"[PASS]"* ]]; then
    pass "faccessat2 probe: all test cases passed"
else
    fail "faccessat2 probe failed (code=$code, out=$out)"
fi

out_harness=$("$PROOT" -R "$ROOTFS" -0 "$BIN_DIR/minimal_syscall_harness" 2>&1)
code_harness=$?

if [[ $code_harness -eq 0 && "$out_harness" == *"Harness Summary: PASS"* ]]; then
    pass "minimal syscall harness: all test cases passed"
else
    fail "minimal syscall harness failed (code=$code_harness, out=$out_harness)"
fi

echo "== syscall: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
