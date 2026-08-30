/*
 * untag.h - ARM64 top-byte (TBI) address untagging for Android.
 *
 * Single choke point through which all guest addresses must be normalized
 * before they are handed to the kernel (process_vm_readv / writev and
 * PTRACE_PEEKDATA / POKEDATA).  See docs/android-compat/audit-pointer-tagging.md.
 *
 * On ARM64 with TBI (default on Android kernels) a pointer may carry a
 * non-zero top byte that the hardware ignores but which kernel syscall
 * interfaces reject with EINVAL/EFAULT.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LINUXDROID_UNTAG_H
#define LINUXDROID_UNTAG_H

#include <stdint.h>

/* Policy notes (read before touching the mask):
 *
 * 1. What is removed: bits 63:56 only -- the Top-Byte-Ignore / MTE tag
 *    byte (e.g. the 0xb4 in 0xb400007c4165ec40, as handed out by the
 *    scudo allocator and HWASan on Android).  Bits 55:0 are preserved
 *    untouched, so 48-bit, 52-bit (LVA/LPA2) and 56-bit user VAs and
 *    even PAC bits (54:50) pass through verbatim.
 *
 * 2. Why this cannot corrupt a valid address: every user-space mapping
 *    on aarch64 lives in the TTBR0 range, whose top byte is always zero
 *    (even a full 56-bit VA only uses bits 55:0).  A non-zero top byte
 *    is therefore always a tag or an already-invalid address, never a
 *    live address.  On other architectures the helpers are the identity.
 *
 * 3. Tag removal is NOT address canonicalization and NOT validity
 *    checking.  This mask does not decide whether the resulting address
 *    is mapped, canonical or otherwise usable; it only strips the
 *    tagging information the kernel interface must not see.  An invalid
 *    address stays invalid (the kernel still rejects it, now for the
 *    right reason).
 *
 * 4. Scope: apply ONLY to tracee (guest) virtual addresses that are
 *    about to be passed to a kernel interface expecting an untagged
 *    virtual address (ptrace PEEK/POKE, process_vm_*, ...).  Never
 *    apply to host pointers, register values, syscall-argument
 *    integers, sizes, or offsets.
 *
 * 5. Gating: this is a hardware (aarch64) property, not an OS property.
 *    The mask is enabled for every aarch64 build (Android or not) and
 *    is the identity everywhere else, so generic PRoot code can use it
 *    unconditionally without Android-specific ifdef litter.
 */

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
/* Mask off the top byte(s) used for pointer tagging (TBI).  The 56-bit
 * address space is more than sufficient for user-space mappings. */
static inline void *UNTAG_ADDRESS(const void *addr)
{
	return (void *)((uintptr_t)addr & 0x00ffffffffffffffULL);
}

/* Word-sized flavor: the tracee-memory subsystem (src/tracee/mem.c)
 * carries tracee addresses as word_t / uintptr_t, not as pointers. */
static inline uintptr_t UNTAG_WORD(uintptr_t addr)
{
	return addr & 0x00ffffffffffffffULL;
}
#else
#define UNTAG_ADDRESS(addr) ((void *)(addr))

/* Identity on non-aarch64: normalization must be a no-op there. */
static inline uintptr_t UNTAG_WORD(uintptr_t addr)
{
	return addr;
}
#endif

#endif /* LINUXDROID_UNTAG_H */
