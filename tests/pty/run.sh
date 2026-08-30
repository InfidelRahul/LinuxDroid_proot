#!/usr/bin/env bash
# tests/pty/run.sh - Phase 13 PTY validation.
#
# PRoot does not own the Android UI; LinuxDroid's Kotlin layer owns PTY
# lifecycle later.  But PRoot must correctly support shell-over-PTY:
# stdin/stdout/stderr, signals and an interactive shell.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
ROOTFS="${ROOTFS:-/}"
FAILS=0

[[ -x "$PROOT" ]] || { echo "engine not built"; exit 1; }

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

echo "== Phase 13 PTY tests =="

if command -v python3 >/dev/null 2>&1; then
	out=$(python3 "$(dirname "$0")/pty_probe.py" "$PROOT" "$ROOTFS" 2>/dev/null)
	[[ "$out" == *"pty-hello"* ]] && pass "interactive shell over PTY" || fail "interactive shell over PTY"
else
	out=$("$PROOT" -R "$ROOTFS" /bin/sh -c 'echo pty-hello' 2>&1)
	[[ "$out" == *"pty-hello"* ]] && pass "shell output" || fail "shell output"
fi

echo "== pty: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
