# Audit — ARM64 pointer tagging & UNTAG_ADDRESS()

## Why this matters on Android

On ARM64 with TBI (Top-Byte-Ignore) enabled (default on many Android kernels),
addresses may have a non-zero top byte used as a tag (e.g. by the allocator or
by `-fsanitize=hwaddress`).  The **hardware** ignores the top byte for memory
access, but **kernel syscall interfaces do not**: passing a tagged pointer to
`process_vm_readv`, `process_vm_writev`, or ptrace yields `EINVAL`/`EFAULT`.

PRoot is a ptrace tracer that reads and writes the tracee's memory with raw
pointers taken from the tracee's register file.  If it forwards a tagged
address verbatim, the operation fails.

## Audit checklist

- [ ] Identify where PRoot obtains guest addresses (register values, iovecs).
- [ ] Identify every `process_vm_readv` / `process_vm_writev` call.
- [ ] Identify every `PTRACE_PEEKDATA` / `PTRACE_POKEDATA` call.
- [ ] Identify the address macro / helper used for guest memory access.
- [ ] On ARM64, strip the top byte before every such call.
- [ ] Keep a *single* `UNTAG_ADDRESS(addr)` helper so the policy lives in one
      place.

## Decision record

| Field | Value |
|-------|-------|
| Component | ARM64 memory / ptrace addressing |
| Upstream | passes raw addresses to process_vm/ptrace |
| Termux | untags top-byte addresses for ARM64 |
| LinuxDroid | `UNTAG_ADDRESS()` applied to all guest addresses on ARM64 |
| Reason | tagged pointers cause `EINVAL`/`EFAULT` from the kernel on TBI |
| Expected behavior | all ptrace/process_vm addresses are untagged before use |
| Android requirement | Android kernel TBI; Android 16+ |
| Test | `linuxdroid-selftest` memory/ptrace rows; tagged-address repro |

## Untagging policy

```c
/* native/android/untag.h */
#if defined(__aarch64__)
static inline void *UNTAG_ADDRESS(void *addr)
{
    /* drop the top byte used for pointer tagging (TBI) */
    return (void *)((uintptr_t)addr & 0x00ffffffffffffffULL);
}
#else
#define UNTAG_ADDRESS(addr) (addr)
#endif
```

The helper must be the single choke point through which all guest addresses
are normalized before reaching the kernel.
