# EINVAL investigation (Phase 3)

This document records the *previous EINVAL investigation* — the concrete
failure that originally motivated the Android compatibility layer.  The goal
is to keep the reasoning (not just the fix) so future engineers can reproduce
and extend it.

## Symptom

On Android (particularly ARM64), PRoot memory / ptrace operations that work on
desktop Linux fail with `EINVAL` (and occasionally `EFAULT`).  A guest process
crashes or a memory access reports an invalid argument instead of succeeding.

## Leading hypothesis: ARM64 pointer tagging

Modern ARM64 kernels implement **Top-Byte-Ignore (TBI)** / pointer tagging.
User-space pointers may carry a non-zero top byte that the hardware ignores
for memory access but which the **kernel's `process_vm_*` and ptrace
interfaces reject with `EINVAL`**.  PRoot (and many debuggers) pass addresses
around as raw `void*`; when the tracee's pointer carries a tag, the syscall
fails.

Additional related suspects in the `EINVAL` family on Android:

* `PTRACE_PEEKDATA` / `PTRACE_POKEDATA` with a tagged address.
* `process_vm_readv` / `process_vm_writev` local/remote iovec with a tagged
  address.
* seccomp `SECCOMP_MODE_FILTER` interplay on filtered syscalls.
* Android's `kernel.yama.ptrace_scope` denying the ptrace call (`EPERM`, not
  `EINVAL`, but worth distinguishing).
* ABI / register-file mismatches (misread `user_regs_struct`).

## Work items produced from this investigation

1. `audit-pointer-tagging.md` — the ARM64 TBI / `UNTAG_ADDRESS()` audit.
2. `audit-ptrace-memory.md` — the ptrace + process_vm + PEEK/POKE audit.
3. `audit-seccomp.md` — the Android seccomp interaction audit.
4. `audit-signals.md` — signal behavior including SIGSEGV with tagged addresses.
5. `audit-proc.md` — Android `/proc` quirks.

## How to reproduce on a device

```sh
adb push build/android/arm64-v8a/linuxdroid-selftest /data/local/tmp/
adb shell /data/local/tmp/linuxdroid-selftest
```

Any `FAIL` in the `memory` / `ptrace` rows reproduces the class of bug.  A
tighter repro is to run PRoot on a guest that touches memory heavily and
observe `EINVAL` in `strace`-like logs.

## Rule

Every fix that falls out of this investigation is recorded in the audit docs
with the shape:

```
Reason / Expected behavior / Android requirement / Decision / Test
```

so that it becomes engineering history rather than a one-off patch.
