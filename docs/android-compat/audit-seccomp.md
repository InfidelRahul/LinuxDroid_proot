# Audit — Android seccomp behavior

## Why Android seccomp is different

Android kernels impose a **global, always-on seccomp-bpf policy** on all apps
(installed early by the zygote).  Unknown/unnecessary syscalls are blocked and
return `ENOSYS` or deliver `SIGSYS`.  PRoot itself installs a seccomp filter
(to trap tracee syscalls at the ptrace level and to implement `kompat`
syscall-number translation).  The two policies must interoperate, and PRoot
must not assume the permissive desktop behavior.

## Audit checklist

- [ ] Normal syscall — passes through PRoot's filter untouched.
- [ ] Trapped syscall — correctly routed to the ptrace engine.
- [ ] SIGSYS — handled/delivered correctly when Android's policy (not PRoot's)
      kills a syscall.
- [ ] Emulation — `kompat` / syscall-number translation still works.
- [ ] ENOSYS — the graceful "unknown syscall" path works on Android.
- [ ] Architecture detection — PRoot must detect the *guest* arch correctly
      under Android's policy.
- [ ] Fallback — if `SECCOMP_MODE_FILTER` cannot be installed, PRoot degrades
      to pure-ptrace tracing (no seccomp accelerator) without breaking.

## Decision record

| Field | Value |
|-------|-------|
| Component | seccomp / syscall trapping |
| Upstream | installs a filter; expects full syscall set available |
| Termux | adapts filter to the Android global policy |
| LinuxDroid | probe filter installability (see selftest), fall back to ptrace-only |
| Reason | Android's global seccomp policy blocks syscalls desktop doesn't |
| Expected behavior | engine works with or without its own seccomp filter |
| Android requirement | Android enforces global seccomp; `SIGSYS` handling needed |
| Test | `tests/seccomp/` suite (Phase 11) |

## Compatibility contract

`linuxdroid-selftest` probes whether a BPF filter is installable
(`SECCOMP_MODE_FILTER` after `PR_SET_NO_NEW_PRIVS`).  If not, LinuxDroid knows
PRoot will run in ptrace-only mode and can surface the degraded capability.
