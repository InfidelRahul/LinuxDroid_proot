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

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
/* Mask off the top byte(s) used for pointer tagging (TBI).  The 56-bit
 * address space is more than sufficient for user-space mappings. */
static inline void *UNTAG_ADDRESS(const void *addr)
{
	return (void *)((uintptr_t)addr & 0x00ffffffffffffffULL);
}
#else
#define UNTAG_ADDRESS(addr) ((void *)(addr))
#endif

#endif /* LINUXDROID_UNTAG_H */
