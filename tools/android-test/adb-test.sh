#!/usr/bin/env bash
# adb-test.sh - push the engine to a device, run the diagnostic suite,
#              and collect results (Phase 6).
#
# Usage:
#   tools/android-test/adb-test.sh [abi] [device]
#
#   abi    arm64-v8a (default) or x86_64
#   device adb serial (optional; defaults to single attached device)
set -euo pipefail

ABI="${1:-arm64-v8a}"
SERIAL="${2:-}"
ADB=(adb)
[[ -n "$SERIAL" ]] && ADB=(adb -s "$SERIAL")

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/android/$ABI"
TMP=/data/local/tmp

echo "== LinuxDroid-PRoot device test harness =="
echo "ABI:      $ABI"
echo "Artifacts:${BUILD}"

"${ADB[@]}" get-state >/dev/null 2>&1 || { echo "error: no adb device"; exit 1; }

for f in proot loader linuxdroid-selftest; do
	if [[ ! -f "$BUILD/$f" ]]; then
		echo "warning: missing $BUILD/$f (build first: make android-arm64 / android-x86_64)"
	fi
done

echo "== push =="
"${ADB[@]}" push "$BUILD/proot" "$TMP/" 
"${ADB[@]}" push "$BUILD/loader" "$TMP/" 2>/dev/null || true
"${ADB[@]}" push "$BUILD/linuxdroid-selftest" "$TMP/" 2>/dev/null || true

echo "== chmod =="
"${ADB[@]}" shell chmod 755 "$TMP/proot" "$TMP/loader" "$TMP/linuxdroid-selftest"

echo "== self-test =="
"${ADB[@]}" shell "$TMP/linuxdroid-selftest"

echo "== proot smoke =="
"${ADB[@]}" shell "$TMP/proot --version" | head -3

echo "== done =="
