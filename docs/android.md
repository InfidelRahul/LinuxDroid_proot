# Android-specific documentation

The primary deployment target is **Android 16 (API 36), ARM64 (arm64-v8a)**.
This page collects everything Android-specific: how to run on a device, known
kernel/user-space quirks, and the Android compatibility layer.

## Running on a device with adb

PRoot is a ptrace-based tool and must run with adequate privileges.  On a
normal Android device you do **not** need root to run PRoot on your own
processes, but the kernel must allow ptrace (Android 4.1+ allows an app to
ptrace its own children; `ro.debuggable` and ADB off-by-default ptrace scope
apply to *other* apps).

```sh
# push the standalone binaries
adb push build/android/arm64-v8a/proot /data/local/tmp/
adb push build/android/arm64-v8a/loader /data/local/tmp/
adb push build/android/arm64-v8a/linuxdroid-selftest /data/local/tmp/
adb shell chmod 755 /data/local/tmp/proot /data/local/tmp/loader /data/local/tmp/linuxdroid-selftest

# run the compatibility gate first
adb shell /data/local/tmp/linuxdroid-selftest

# then run PRoot on a guest program / rootfs
adb shell /data/local/tmp/proot -R /data/local/tmp/rootfs /bin/sh
```

> Do **not** package this into LinuxDroid yet.  First make the standalone
> binary work on a real device (Phase 5 / 6).  LinuxDroid integration is the
> last step and consumes release artifacts, not this source.

## Known Android quirks (engine-relevant)

| Area | Android behavior | Engine impact |
|------|------------------|---------------|
| PIE | Executables must be position-independent since API 21 | Build with `-fPIE -pie` |
| bionic vs glibc | Different libc, dynamic linker is `linker64` | Loader must hand off to `linker64` |
| seccomp | Android imposes a strict global seccomp policy (SIGSYS on filtered syscalls) | Seccomp path must interoperate; see `docs/android-compat/audit-seccomp.md` |
| ARM64 pointer tagging | Top byte tagging / TBI on recent kernels | Addresses must be untagged (`UNTAG_ADDRESS`); see `docs/android-compat/audit-pointer-tagging.md` |
| `/proc` | `/proc/self/maps`, mount info differ; hidepid variants | `/proc` translation; see `docs/android-compat/audit-proc.md` |
| ptrace scope | `kernel.yama.ptrace_scope` may restrict | Tests run under adb shell; document limits |
| Data paths | `/data/local/tmp`, `/sdcard`, app sandbox dirs | Bind-mount strategy differs from desktop |

Each workaround is documented under `docs/android-compat/` with its reason,
expected behavior, and Android requirement — turning one-off hacks into an
engineering history.

## Android compatibility layer

The Android-specific code lives **additively** in `native/android/` so the
generic engine stays untainted.  See `native/android/README.md` and
`docs/architecture.md`.

## Rootfs

A tiny ARM64 rootfs (busybox `sh`, `true`, `false`, `echo`, `/dev`, `/proc`) is
used for functional testing (Phase 8).  See `tests/functional/`.

## Device testing harness

`tools/android-test/` automates push → run → collect.  See
`tools/android-test/README.md`.

## Minimum supported Android version

Targeted: **Android 16 (API 36)** and newer.  The selftest reports the ABI and
runtime environment so LinuxDroid can make a launch decision per device.
