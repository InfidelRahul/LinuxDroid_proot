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

- [ ] `process_vm_readv` — untag addresses; handle partial reads; fall back to
      PEEKDATA when the syscall is unavailable/blocked.
- [ ] `process_vm_writev` — untag; handle partial writes; fall back to POKEDATA.
- [ ] `PTRACE_PEEKDATA` — untag `addr`.
- [ ] `PTRACE_POKEDATA` — untag `addr`.
- [ ] All ptrace addresses — ensure no tagged pointer reaches the kernel.
- [ ] Confirm the feature-detection (`build.h`) correctly reports
      `HAVE_PROCESS_VM` for bionic so the fast path is used or skipped.
- [ ] Verify the fallback path (PEEK/POKE word-at-a-time) is correct and
      tested on Android where `process_vm_*` may be restricted.

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
