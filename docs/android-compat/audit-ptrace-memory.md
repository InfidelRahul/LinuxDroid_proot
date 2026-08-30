# Audit — ptrace & memory access on Android

This audit covers every way PRoot reads and writes the tracee's memory and
registers, and what must change on Android.

## Accessors in scope

* `process_vm_readv` — bulk read
* `process_vm_writev` — bulk write
* `PTRACE_PEEKDATA` — read a word
* `PTRACE_POKEDATA` — write a word
* register-file access via `PTRACE_GETREGSET` / `PTRACE_SETREGSET`

## Audit checklist

- [x] `process_vm_readv` — untag addresses; handle partial reads; fall back to
      PEEKDATA when the syscall is unavailable/blocked.
- [x] `process_vm_writev` — untag; handle partial writes; fall back to POKEDATA.
- [x] `PTRACE_PEEKDATA` — untag `addr`.
- [x] `PTRACE_POKEDATA` — untag `addr`.
- [x] All ptrace addresses — ensure no tagged pointer reaches the kernel.
- [x] Confirm the feature-detection (`build.h`) correctly reports
      `HAVE_PROCESS_VM` for bionic so the fast path is used or skipped.
- [x] Verify the fallback path (PEEK/POKE word-at-a-time) is correct and
      tested on Android where `process_vm_*` may be restricted.

Status: implemented — see the *Implementation record* in
`audit-pointer-tagging.md`.  Normalization lives in
`src/tracee/mem.h` (`normalize_tracee_address()`, backed by
`native/android/untag.h`) and is applied at the entry of every
tracee-memory accessor, so the process_vm fast path and the PEEK/POKE
fallback always share the same kernel-safe address.  The fallback path
is exercised on the host by `tests/memory/run.sh` via a `HAVE_PROCESS_VM`-disabled
engine variant, and on the device by the `linuxdroid-selftest` rows.

## Decision record

| Field | Value |
|-------|-------|
| Component | tracee memory access |
| Upstream | tries process_vm first, falls back to PEEK/POKE |
| Termux | same strategy + untagging; tuned fallback thresholds |
| LinuxDroid | untag all addresses; keep process_vm fast path with PEEK/POKE fallback |
| Reason | Android bionic + TBI require untagging; process_vm may be filtered |
| Expected behavior | memory read/write succeeds via fastest available path |
| Android requirement | ARM64 TBI; seccomp may block process_vm_* |
| Test | `linuxdroid-selftest` memory row; functional memory tests |

## Fallback policy

Prefer `process_vm_*` when available (fewer ptrace stops).  If it returns
`ENOSYS`, `EINVAL` on an *untagged* address, or the kernel is otherwise
restricting it, fall back to `PTRACE_PEEKDATA`/`POKEDATA` word-at-a-time.  The
selftest exercises both on the target so the fallback is not bit-rot.
