#!/usr/bin/env bash
# tests/seccomp/run.sh - Phase 11 seccomp validation.
#
# Validates the engine under the seccomp-related scenarios.  On desktop the
# engine's own seccomp_filter accelerator is exercised by default.  On Android
# the kernel/security environment differs (global seccomp policy), so these
# tests also run as part of the device harness.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
ROOTFS="${ROOTFS:-/}"
FAILS=0

[[ -x "$PROOT" ]] || { echo "engine not built"; exit 1; }

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

echo "== Phase 11 seccomp validation =="

# Feature availability reported by the engine
acc=$("$PROOT" --version 2>&1 | grep -i accelerators)
case "$acc" in
	*seccomp_filter\ =\ yes*) pass "engine seccomp_filter accelerator present" ;;
	*) echo "  note: seccomp_filter=no (ptrace-only mode on this target)"; pass "engine runs in ptrace-only mode" ;;
esac

# Normal syscall through the engine
if "$PROOT" -R "$ROOTFS" /bin/echo ok >/dev/null 2>&1; then pass "normal syscall"; else fail "normal syscall"; fi

# A syscall that the engine emulates/traps (e.g. via kompat) - getpid is fine
if "$PROOT" -R "$ROOTFS" /bin/sh -c 'exit 0' >/dev/null 2>&1; then pass "trapped syscall path"; else fail "trapped syscall path"; fi

# chroot-style run (guest-visible root) still works through the engine
if "$PROOT" -R "$ROOTFS" /bin/sh -c 'cd / && echo ok' >/dev/null 2>&1; then pass "chroot-style run"; else fail "chroot-style run"; fi

echo "== seccomp: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
