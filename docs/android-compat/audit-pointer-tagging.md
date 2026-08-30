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

- [x] Identify where PRoot obtains guest addresses (register values, iovecs).
- [x] Identify every `process_vm_readv` / `process_vm_writev` call.
- [x] Identify every `PTRACE_PEEKDATA` / `PTRACE_POKEDATA` call.
- [x] Identify the address macro / helper used for guest memory access.
- [x] On ARM64, strip the top byte before every such call.
- [x] Keep a *single* `UNTAG_ADDRESS(addr)` helper so the policy lives in one
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

## Implementation record (tracee address normalization)

**2026 audit finding:** `UNTAG_ADDRESS()` existed in
`native/android/untag.h` but was referenced nowhere in the engine.  The
kernel-facing memory operations in `src/tracee/mem.c` received raw tracee
addresses, which is exactly how a tagged pointer such as
`0xb400007c4165ec40` reached `ptrace(PTRACE_PEEKDATA, ...)` and failed
with `EINVAL`.

### Where normalization occurs

One boundary, no scattering:

```
PRoot callers (syscall/, execve/, path/, loader/, extension/, ptrace/)
        │  word_t tracee address
        ▼
src/tracee/mem.h:  normalize_tracee_address()   ← the single boundary
        │  implemented with native/android/untag.h: UNTAG_WORD()
        ▼
read_data / write_data / read_string / peek_word / poke_word / writev_data
        │  kernel-safe address (fast path AND ptrace fallback share it)
        ▼
process_vm_readv / process_vm_writev / PTRACE_PEEKDATA / PTRACE_POKEDATA
```

The ptrace emulator (`src/ptrace/ptrace.c`) reuses the same helper for
the guest's own `PTRACE_PEEK/POKE(TEXT|DATA)` requests, where the
address argument is a ptracee memory address taken from `SYSARG_3`.

### Coverage

| Operation | Normalized? | Mechanism |
|-----------|-------------|-----------|
| `process_vm_readv`  | YES | `normalize_tracee_address()` at entry of `read_data` / `read_string` / `peek_word` |
| `process_vm_writev` | YES | at entry of `write_data` / `writev_data` / `poke_word` |
| `PTRACE_PEEKDATA`   | YES | same entries; every fallback loop uses the normalized base |
| `PTRACE_POKEDATA`   | YES | same entries |
| `read_data` / `write_data` / `read_string` / `peek_word` / `poke_word` | YES | the boundary itself |
| ptrace-emulator `PEEK/POKE(TEXT,DATA)` | YES | `src/ptrace/ptrace.c` |
| guest's own syscalls | NO (intentional) | executed by the tracee; kernel-side untagging policy is the tracee/kernel business, upstream behavior preserved |
| `PTRACE_GET/SETREGS`, `GET/SETREGSET` | NO (not tracee addresses) | register buffers are host memory; `address` is `NT_*` (an integer) |
| `PTRACE_PEEKUSER/POKEUSER` | NO (intentional) | USER-area byte offset, not a virtual address |
| `PTRACE_SET_SYSCALL` | NO (intentional) | `address` is the syscall number |
| `alloc_mem` / register values | NO (intentional) | stack pointer comes from and returns to the register file; upstream semantics |

### What the mask does (and does not) do

`UNTAG_WORD()`/`UNTAG_ADDRESS()` clear **bits 63:56 only**.  This is tag
removal, not address canonicalization and not validity checking: bits
55:0 (48-bit VA, 52-bit LVA/LPA2, 56-bit VA, PAC bits 54:50) pass through
untouched, and an invalid address stays invalid.  The mask can never
corrupt a live address: every aarch64 user-space mapping lives in the
TTBR0 range whose top byte is always zero, so a non-zero top byte is
always a tag (TBI/MTE/scudo/HWASan) or an already-invalid address.  On
non-aarch64 builds the helpers are the identity — generic PRoot behavior
is bit-for-bit unchanged.

### How the tests reproduce the problem

* `linuxdroid-selftest` rows `ptrace-untag` / `pvm-untag`: a raw
  `0xb4`-tagged address is passed to `ptrace(PTRACE_PEEKDATA)` /
  `process_vm_readv` (kernel rejects — the original `EINVAL` class),
  then the same access at the top-byte-masked address succeeds and
  returns the expected word.  `untag-unit` proves the helper never
  alters a valid address and strips exactly the tag byte on aarch64.
* `tests/memory/` runs a guest probe **under the engine** that passes a
  `0xb4`-tagged path pointer to `openat(2)`: on aarch64 the engine must
  normalize and the syscall must succeed; on other architectures
  normalization is the identity and the invalid pointer must be
  rejected (upstream behavior preserved).  The suite runs the probe
  twice — once under the normal engine (process_vm fast path) and once
  under a test-only engine variant built with `HAVE_PROCESS_VM` disabled
  (pure `PTRACE_PEEK/POKEDATA` fallback).

### How to run the regression

```sh
make proot            # host engine
make selftest         # kernel-level tagged-address repro rows
tests/memory/run.sh   # engine-level memory boundary (fast path + fallback)
make test             # everything
# device (Android ARM64, requires adb + NDK build):
make NDK_ROOT=/path/to/ndk android-arm64
tools/android-test/adb-test.sh arm64-v8a
```

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
