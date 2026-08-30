# Audit — Android signal behavior

PRoot must forward and synthesize signals correctly so guest Linux user-space
keeps normal semantics.  Android adds two wrinkles:

1. The global seccomp policy can deliver **SIGSYS** for a blocked syscall;
   PRoot must distinguish "the guest got SIGSYS from Android's policy" from
   "the guest asked for a syscall we need to emulate."
2. On ARM64 TBI, a **SIGSEGV** from a tagged address may be reported; signal
   handlers and si_addr reporting interact with untagging.

## Audit checklist

- [ ] Signal forwarding (SIGCHLD, SIGTERM, SIGINT) preserved for the tracee.
- [ ] SIGSYS handling is correct under Android's seccomp policy.
- [ ] SIGSEGV with tagged address (ARM64 TBI) is reported/handled sensibly.
- [ ] Signal masks and dispositions forwarded to the tracee.
- [ ] PRoot's own signal handling (during tracing) does not mask tracee signals.

## Decision record

| Field | Value |
|-------|-------|
| Component | signals |
| Upstream | standard Linux signal semantics |
| Termux | adjusts SIGSYS/SIGSEGV handling for Android policy |
| LinuxDroid | audit + tests; distinguish Android-policy SIGSYS |
| Reason | Android seccomp can raise SIGSYS; TBI can raise tagged SIGSEGV |
| Expected behavior | guest sees correct signals; engine does not swallow them |
| Android requirement | Android seccomp policy; ARM64 TBI |
| Test | `tests/process-signal/` and `tests/seccomp/` (Phases 11–12) |

## Compatibility contract

`linuxdroid-selftest` verifies a SIGUSR1 handler runs and delivery works —
the baseline for signal forwarding on the target.
