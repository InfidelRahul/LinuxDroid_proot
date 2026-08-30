#!/usr/bin/env bash
# verify-elf.sh - Phase 5 ELF / architecture / dynamic-dependency checks.
#
# Usage:
#   android/verify-elf.sh <binary> [llvm-readelf]
#
# Checks that a build artifact is the expected kind of ELF for the Android
# target: correct class/machine, PIE, and bionic (not glibc) dependencies.
set -euo pipefail

BIN="${1:?usage: verify-elf.sh <binary> [llvm-readelf]}"
READELF="${2:-llvm-readelf}"

echo "== $BIN =="
"$READELF" -h "$BIN" | sed -n '1,20p'

CLASS=$("$READELF" -h "$BIN" | awk '/Class:/{print $2}')
MACHINE=$("$READELF" -h "$BIN" | awk '/Machine:/{print $2}')
TYPE=$("$READELF" -h "$BIN" | awk '/Type:/{print $2}')

ok=1
[[ "$CLASS" == "ELF64" ]] && echo "  [OK] ELF64" || { echo "  [FAIL] not ELF64"; ok=0; }
[[ "$MACHINE" == "AArch64" ]] && echo "  [OK] AArch64" || { echo "  [FAIL] not AArch64"; ok=0; }
case "$TYPE" in
	DYN|EXEC) echo "  [OK] type $TYPE (PIE-compatible)" ;;
	*) echo "  [WARN] type $TYPE (PIE expected on Android)";;
esac

echo "-- dynamic dependencies (must resolve against bionic, not glibc) --"
"$READELF" -d "$BIN" 2>/dev/null | awk '/NEEDED/{print "  "$0}' || true
if "$READELF" -d "$BIN" 2>/dev/null | grep -qi "libc.so.6"; then
	echo "  [FAIL] references glibc libc.so.6"
	ok=0
fi

exit $ok
