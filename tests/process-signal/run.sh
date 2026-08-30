#!/usr/bin/env bash
# tests/process-signal/run.sh - Phase 12 process & signal validation.
#
# Validates fork / clone / execve / wait / exit and signal delivery through
# the engine.  A small C probe (probe.c) is used so signals and process
# relationships are exercised precisely, not just via shell.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
ROOTFS="${ROOTFS:-/}"
CC="${CC:-gcc}"
FAILS=0

[[ -x "$PROOT" ]] || { echo "engine not built"; exit 1; }

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

# Build the probe
PROBE_DIR="$(mktemp -d)"
trap 'rm -rf "$PROBE_DIR"' EXIT
"$CC" -O2 -o "$PROBE_DIR/probe" "$(dirname "$0")/probe.c" || { echo "probe build failed"; exit 1; }

echo "== Phase 12 process/signal tests =="

# direct (no proot) baseline
"$PROBE_DIR/probe" >/dev/null 2>&1 && pass "baseline (no proot)" || fail "baseline (no proot)"

# through proot
if "$PROOT" -R "$ROOTFS" -b "$PROBE_DIR:/probe" /probe/probe >/dev/null 2>&1; then
	pass "fork/exec/wait/signals via proot"
else
	fail "fork/exec/wait/signals via proot"
fi

# SIGTERM to a proot-spawned shell (with a hard timeout so a hang fails fast)
timeout 8 "$PROOT" -R "$ROOTFS" \
	/bin/sh -c 'trap "exit 0" TERM; while :; do sleep 0.1; done' >/dev/null 2>&1 &
pid=$!
sleep 0.8
kill -TERM "$pid" 2>/dev/null
wait "$pid" 2>/dev/null
rc=$?
if [[ "$rc" -eq 0 ]]; then pass "SIGTERM to traced process"; else fail "SIGTERM to traced process (rc=$rc)"; fi

echo "== process-signal: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
