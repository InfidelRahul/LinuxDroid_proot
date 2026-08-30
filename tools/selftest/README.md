# tools/selftest — LinuxDroid compatibility self-test

`linuxdroid-selftest` is a standalone diagnostic (Phase 7) that validates the
runtime capabilities the LinuxDroid PRoot engine depends on.  It is a
**separate tool** rather than a hidden CLI switch on upstream proot, so
upstream CLI semantics stay untouched.

## Checks

```
[PASS] executable   /proc/self/exe resolves
[PASS] architecture aarch64 / x86_64 ...
[PASS] memory       process_vm_readv / process_vm_writev round-trip
[PASS] ptrace       PTRACE_TRACEME + PEEKDATA
[PASS] syscall      raw syscall() invocation
[PASS] seccomp      BPF filter installable (SECCOMP_MODE_FILTER)
[PASS] execve       shell spawn
[PASS] loader       ELF interpreter / linker64 present
[PASS] signals      SIGUSR1 delivery
[PASS] proc         /proc/self/status + /proc/self/maps
```

## Build & run

```sh
make selftest                    # -> build/host/linuxdroid-selftest
build/host/linuxdroid-selftest   # expect: result: PASS
```

For Android, the selftest is cross-compiled as part of `make android-arm64`
and pushed via `tools/android-test/adb-test.sh`.

## Contract

Exit status `0` ⇔ all critical checks pass.  This is the gate LinuxDroid uses
before launching a session — a **reliable compatibility contract** between the
application and the engine.
