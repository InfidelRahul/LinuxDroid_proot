#!/usr/bin/env bash
# adb-test.sh - push the engine to a device, run the diagnostic suite,
#              and collect results (Phase 6 / Phase 10).
#
# Usage:
#   tools/android-test/adb-test.sh [abi] [device]
#
#   abi    arm64-v8a (default) or x86_64
#   device adb serial (optional; defaults to single attached device)
#
# Environment:
#   ROOTFS_TGZ  optional path to a minimal ARM64 Linux rootfs tarball
#               (e.g. busybox/Alpine).  When set it is unpacked to
#               /data/local/tmp/ldrootfs and the engine is validated
#               against a real guest rootfs.  When unset the rootfs
#               stage is skipped (reported as SKIP).
#
# The validation is progressive: version -> self-test -> tagged-address
# memory regression (ARM64 tracee normalization) -> basic program
# execution -> optional real rootfs.  Each step records exit code,
# stdout and stderr.  Nothing here packages or installs LinuxDroid: the
# standalone PRoot artifacts are tested as-is.
set -uo pipefail

ABI="${1:-arm64-v8a}"
SERIAL="${2:-}"
ADB=(adb)
[[ -n "$SERIAL" ]] && ADB=(adb -s "$SERIAL"])

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/android/$ABI"
TMP=/data/local/tmp

FAILS=0
pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }
skip() { echo "[SKIP] $1"; }

echo "== LinuxDroid-PRoot device test harness =="
echo "ABI:      $ABI"
echo "Artifacts:${BUILD}"

"${ADB[@]}" get-state >/dev/null 2>&1 || { echo "error: no adb device"; exit 1; }

for f in proot loader linuxdroid-selftest guest-mem; do
	if [[ ! -f "$BUILD/$f" ]]; then
		echo "warning: missing $BUILD/$f (build first: make android-arm64 / android-x86_64)"
	fi
done

echo "== push =="
"${ADB[@]}" push "$BUILD/proot" "$TMP/"
"${ADB[@]}" push "$BUILD/loader" "$TMP/" 2>/dev/null || true
"${ADB[@]}" push "$BUILD/linuxdroid-selftest" "$TMP/" 2>/dev/null || true
"${ADB[@]}" push "$BUILD/guest-mem" "$TMP/" 2>/dev/null || true

echo "== chmod =="
"${ADB[@]}" shell chmod 755 "$TMP/proot" "$TMP/loader" "$TMP/linuxdroid-selftest" "$TMP/guest-mem"

# ---- environment capture ---------------------------------------------------
echo "== environment =="
"${ADB[@]}" shell "echo android=\$(getprop ro.build.version.release); \
	echo sdk=\$(getprop ro.build.version.sdk); \
	echo security_patch=\$(getprop ro.build.version.security_patch); \
	echo kernel=\$(uname -r); \
	echo abi=\$(getprop ro.product.cpu.abi); \
	echo device=\$(getprop ro.product.model)"

# ---- 1. version -------------------------------------------------------------
echo "== proot --version =="
if "${ADB[@]}" shell "$TMP/proot --version" 2>&1 | tee /dev/stderr | grep -q .; then
	pass "proot --version"
else
	fail "proot --version"
fi
# (--build-info is not an upstream PRoot CLI option; --version carries the
#  git version above.  Reported here so nobody assumes it exists.)

# ---- 2. self-test -----------------------------------------------------------
echo "== linuxdroid-selftest =="
if "${ADB[@]}" shell "$TMP/linuxdroid-selftest"; then
	pass "selftest"
else
	fail "selftest (exit $?)"
fi

# ---- 3. memory regression (tagged-address normalization) --------------------
# The probe exercises the engine's whole tracee-memory boundary; the
# selftest rows above (memory / ptrace / ptrace-untag / pvm-untag) cover
# process_vm and PEEK/POKE standalone, including the fallback family.
echo "== memory: guest probe under engine =="
if "${ADB[@]}" shell "$TMP/proot $TMP/guest-mem"; then
	pass "guest-mem under proot"
else
	fail "guest-mem under proot (exit $?)"
fi

# ---- 4. basic program execution ---------------------------------------------
echo "== basic execution =="
"${ADB[@]}" shell "$TMP/proot /system/bin/sh -c 'exit 0'" >/dev/null 2>&1 \
	&& pass "sh -c exit 0" || fail "sh -c exit 0"

out="$("${ADB[@]}" shell "$TMP/proot /system/bin/echo hello-proot" 2>/dev/null)"
[[ "$out" == *hello-proot* ]] && pass "echo" || fail "echo (got: $out)"

out="$("${ADB[@]}" shell "$TMP/proot /system/bin/sh -c 'echo sh-ok'" 2>/dev/null)"
[[ "$out" == *sh-ok* ]] && pass "sh" || fail "sh (got: $out)"

# ---- 5. real rootfs (optional) ------------------------------------------------
if [[ -n "${ROOTFS_TGZ:-}" && -f "$ROOTFS_TGZ" ]]; then
	echo "== rootfs: $ROOTFS_TGZ =="
	"${ADB[@]}" shell "rm -rf $TMP/ldrootfs && mkdir -p $TMP/ldrootfs"
	"${ADB[@]}" push "$ROOTFS_TGZ" "$TMP/ldrootfs/rootfs.tgz"
	"${ADB[@]}" shell "cd $TMP/ldrootfs && tar xf rootfs.tgz 2>/dev/null || busybox tar xf rootfs.tgz"
	if "${ADB[@]}" shell "$TMP/proot -r $TMP/ldrootfs /bin/true" >/dev/null 2>&1; then
		pass "rootfs /bin/true"
	else
		fail "rootfs /bin/true"
	fi
	out="$("${ADB[@]}" shell "$TMP/proot -r $TMP/ldrootfs /bin/echo rootfs-ok" 2>/dev/null)"
	[[ "$out" == *rootfs-ok* ]] && pass "rootfs echo" || fail "rootfs echo (got: $out)"
	out="$("${ADB[@]}" shell "$TMP/proot -r $TMP/ldrootfs /bin/sh -c 'echo sh-rootfs-ok'" 2>/dev/null)"
	[[ "$out" == *sh-rootfs-ok* ]] && pass "rootfs sh" || fail "rootfs sh (got: $out)"
	# Tagged-pointer normalization against a real guest rootfs.
	if "${ADB[@]}" shell "$TMP/proot -r $TMP/ldrootfs $TMP/guest-mem"; then
		pass "rootfs guest-mem"
	else
		fail "rootfs guest-mem"
	fi
else
	skip "real rootfs (set ROOTFS_TGZ=<tarball> to enable)"
fi

echo "== result: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
