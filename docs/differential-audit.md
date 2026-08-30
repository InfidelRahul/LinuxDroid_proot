# Differential audit: LinuxDroid-PRoot vs upstream vs Termux

Termux has maintained a PRoot fork for years and is the reference for what a
long-lived Android PRoot fork has to look like.  We treat it as a **reference,
not a clone** — a source of engineering decisions we can learn from and then
record our own.

For each important subsystem we record a decision table.  This gives us an
*engineering history* rather than a pile of random patches.

## Decision record format

| Field | Meaning |
|-------|---------|
| Component | subsystem under audit (e.g. `ARM64 memory`) |
| Upstream | what upstream PRoot does |
| Termux | what the Termux PRoot fork does |
| LinuxDroid | what we do |
| Reason | why we chose it |
| Expected behavior | what should happen at runtime |
| Android requirement | which Android fact forces the decision |
| Test | which test validates it |

## Example row

```
Component:          ARM64 memory
Upstream:           raw addresses passed to process_vm_*/PTRACE_PEEK
Termux:             untags top-byte addresses (TBI) for ARM64
LinuxDroid:         untag via UNTAG_ADDRESS() on ARM64
Reason:             Android 16 kernels expose top-byte-ignore (TBI);
                    tagged addresses fail with EINVAL/EFAULT
Expected behavior:  all ptrace/process_vm addresses are untagged before use
Android requirement:Android kernel TBI; Android 16+
Test:               tests/functional/memory-tagged-addr
```

> The phrase "retain Z because Android 16 requires…" is the kind of decision we
> want to be able to write for every subsystem.

## Subsystem list to audit

- ptrace
- memory (process_vm_readv / writev, PTRACE_PEEKDATA / POKEDATA)
- syscall (seccomp, ENOSYS, architecture detection)
- seccomp (Android global seccomp policy interaction)
- loader
- execve (ELF, shebang, interpreter hand-off to `linker64`)
- signals (incl. SIGSYS, SIGSEGV with tagged addresses)
- path translation
- Android compatibility (PIE, bionic, /proc, data dirs)

## Where the data lives

Each audit result is written to `docs/android-compat/` (Android-specific) and
this file tracks the three-way comparison.  Updates should always preserve the
`Reason / Expected behavior / Android requirement / Decision / Test` shape so
the history stays readable.
