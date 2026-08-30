#!/usr/bin/env bash
# ndk-env.sh - export the Android NDK toolchain environment.
#
# Usage:
#   source android/ndk-env.sh [/path/to/android-ndk] [api]
#
# Sets:
#   NDK_ROOT, NDK_LLVM, ANDROID_API
#   ANDROID_CC (aarch64 clang wrapper), ANDROID_STRIP/OBJCOPY/OBJDUMP
set -euo pipefail

export NDK_ROOT="${1:-${ANDROID_NDK_ROOT:-}}"
export ANDROID_API="${2:-21}"
if [[ -z "${NDK_ROOT}" ]]; then
	echo "Error: NDK_ROOT not set." >&2
	echo "Usage: source android/ndk-env.sh /path/to/android-ndk [api]" >&2
	return 1 2>/dev/null || exit 1
fi

export NDK_LLVM="${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64"
export ANDROID_CC="${NDK_LLVM}/bin/aarch64-linux-android${ANDROID_API}-clang"
export ANDROID_STRIP="${NDK_LLVM}/bin/llvm-strip"
export ANDROID_OBJCOPY="${NDK_LLVM}/bin/llvm-objcopy"
export ANDROID_OBJDUMP="${NDK_LLVM}/bin/llvm-objdump"
export ANDROID_AR="${NDK_LLVM}/bin/llvm-ar"

echo "NDK_ROOT=$NDK_ROOT"
echo "ANDROID_CC=$ANDROID_CC"
[[ -x "${ANDROID_CC}" ]] || {
	echo "Error: clang wrapper not found: ${ANDROID_CC}" >&2
	return 1 2>/dev/null || exit 1
}
echo "NDK toolchain ready (API $ANDROID_API)."
