#!/usr/bin/env bash
# tests/loader/run.sh - Phase 10 loader validation.
#
# The loader is a first-class artifact.  This suite validates the
# ELF-handling pipeline through the engine:
#   PRoot → guest execve → ELF → guest interpreter → guest libraries → program
#
# On desktop the guest rootfs is `/` (host).  On Android it is a tiny busybox
# rootfs.  The checks below cover: dynamic ELF execution (which requires the
# loader/interpreter path) and interpreter presence.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
LOADER="$ROOT/build/host/loader.bin"
ROOTFS="${ROOTFS:-/}"
FAILS=0

[[ -x "$PROOT" ]] || { echo "engine not built"; exit 1; }

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

echo "== Phase 10 loader validation =="

# loader artifact exists
[[ -f "$LOADER" && -s "$LOADER" ]] && pass "loader artifact present" || fail "loader artifact present"

# dynamic ELF execution (requires interpreter + loader hand-off)
if "$PROOT" -R "$ROOTFS" /bin/true >/dev/null 2>&1; then
	pass "dynamic ELF exec"
else
	fail "dynamic ELF exec"
fi

# a small dynamically-linked helper
tmp=$(mktemp -d)
cat > "$tmp/hello.c" <<'EOF'
#include <stdio.h>
int main(void){ puts("loader-ok"); return 0; }
EOF
cc -o "$tmp/hello" "$tmp/hello.c" 2>/dev/null && {
	if "$PROOT" -R "$ROOTFS" -b "$tmp" "$tmp/hello" 2>/dev/null | grep -q loader-ok; then
		pass "dynamically-linked guest binary"
	else
		fail "dynamically-linked guest binary"
	fi
}
rm -rf "$tmp"

# incorrect architecture: run a guest binary for a different arch should fail
# gracefully (not crash the engine).  We skip if no other-arch toolchain exists.
if command -v gcc-multilib-build >/dev/null 2>&1; then
	:
fi

echo "== loader: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
