#!/usr/bin/env bash
# tests/functional/run.sh - Phase 8 functional Linux tests.
#
# Runs the built engine against host-rootfs guest programs.  Uses `-R /` so
# the host root acts as the guest rootfs (desktop).  On Android this becomes
# `-R /data/local/tmp/rootfs` with a tiny busybox rootfs.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
ROOTFS="${ROOTFS:-/}"
FAILS=0

[[ -x "$PROOT" ]] || { echo "engine not built: run 'make proot' first"; exit 1; }

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

run_ok() { # name, args...  -> expects exit 0
	local name="$1"; shift
	if "$PROOT" -R "$ROOTFS" "$@" >/dev/null 2>&1; then
		pass "$name"
	else
		fail "$name"
	fi
}

echo "== Phase 8 functional tests (rootfs=$ROOTFS) =="

run_ok "/bin/true" /bin/true

# false must return non-zero
if "$PROOT" -R "$ROOTFS" /bin/false >/dev/null 2>&1; then
	fail "/bin/false (should return non-zero)"
else
	pass "/bin/false (returns non-zero as expected)"
fi

# echo with args
out=$("$PROOT" -R "$ROOTFS" /bin/echo "hello proot" 2>/dev/null)
[[ "$out" == "hello proot" ]] && pass "echo" || fail "echo"

run_ok "/bin/sh -c exit 0" /bin/sh -c "exit 0"

# exit code propagation (guard against set -e)
if "$PROOT" -R "$ROOTFS" /bin/sh -c "exit 7" >/dev/null 2>&1; then
	fail "exit code propagation"
else
	code=$?
	[[ "$code" -eq 7 ]] && pass "exit code propagation (7)" || fail "exit code propagation (got $code)"
fi

# bind mount / path translation
tmp=$(mktemp -d)
if "$PROOT" -R "$ROOTFS" -b "$tmp:/ldtest" /bin/test -d /ldtest 2>/dev/null; then
	pass "bind mount"
else
	fail "bind mount"
fi
rm -rf "$tmp"

echo "== functional: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
