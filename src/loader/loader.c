/* -*- c-set-style: "K&R"; c-basic-offset: 8 -*-
 *
 * This file is part of PRoot.
 *
 * Copyright (C) 2015 STMicroelectronics
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <stdbool.h>     /* bool, true, false,  */

#define NO_LIBC_HEADER
#include "loader/script.h"
#include "compat.h"
#include "arch.h"

#define GCC_VERSION (__GNUC__ * 10000			\
			+ __GNUC_MINOR__ * 100		\
			+ __GNUC_PATCHLEVEL__)

#if GCC_VERSION < 40500
#define __builtin_unreachable()
#endif

#if defined(ARCH_X86_64)
#    include "loader/assembly-x86_64.h"
#elif defined(ARCH_ARM_EABI)
#    include "loader/assembly-arm.h"
#elif defined(ARCH_X86)
#    include "loader/assembly-x86.h"
#elif defined(ARCH_ARM64)
#    include "loader/assembly-arm64.h"
#else
#    error "Unsupported architecture"
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

#if defined(ARCH_ARM64)
#include <linux/types.h>
#include <asm/signal.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/siginfo.h>
#endif

#if !defined(MMAP_OFFSET_SHIFT)
#    define MMAP_OFFSET_SHIFT 0
#endif

#define FATAL() do {						\
		SYSCALL(EXIT, 1, 182);				\
		__builtin_unreachable();			\
	} while (0)

#define unlikely(expr) __builtin_expect(!!(expr), 0)

/**
 * Clear the memory from @start (inclusive) to @end (exclusive).
 */
static inline void clear(word_t start, word_t end)
{
	byte_t *start_misaligned;
	byte_t *end_misaligned;

	word_t *start_aligned;
	word_t *end_aligned;

	/* Compute the number of mis-aligned bytes.  */
	word_t start_bytes = start % sizeof(word_t);
	word_t end_bytes   = end % sizeof(word_t);

	/* Compute aligned addresses.  */
	start_aligned = (word_t *) (start_bytes ? start + sizeof(word_t) - start_bytes : start);
	end_aligned   = (word_t *) (end - end_bytes);

	/* Clear leading mis-aligned bytes.  */
	start_misaligned = (byte_t *) start;
	while (start_misaligned < (byte_t *) start_aligned)
		*start_misaligned++ = 0;

	/* Clear aligned bytes.  */
	while (start_aligned < end_aligned)
		*start_aligned++ = 0;

	/* Clear trailing mis-aligned bytes.  */
	end_misaligned = (byte_t *) end_aligned;
	while (end_misaligned < (byte_t *) end)
		*end_misaligned++ = 0;
}

/**
 * Return the address of the last path component of @string_.  Note
 * that @string_ is not modified.
 */
static inline word_t basename(word_t string_)
{
	byte_t *string = (byte_t *) string_;
	byte_t *cursor;

	for (cursor = string; *cursor != 0; cursor++)
		;

	for (; *cursor != (byte_t) '/' && cursor > string; cursor--)
		;

	if (cursor != string)
		cursor++;

	return (word_t) cursor;
}

static inline word_t loader_strlen(const char *s)
{
	word_t len = 0;
	if (!s) return 0;
	while (s[len]) len++;
	return len;
}

static inline void loader_write_raw(int fd, const void *buf, word_t len)
{
	if (len > 0) {
		SYSCALL(WRITE, 3, fd, (word_t)buf, len);
	}
}

static int loader_get_log_fd(void)
{
	static int cached_log_fd = -2;
	if (cached_log_fd != -2) return cached_log_fd;

	cached_log_fd = -1;
	int env_fd = SYSCALL(OPENAT, 4, -100 /* AT_FDCWD */, (word_t)"/proc/self/environ", 0 /* O_RDONLY */, 0);
	if ((int)env_fd >= 0) {
		char env_buf[1024];
		long bytes = SYSCALL(READ, 3, env_fd, (word_t)env_buf, sizeof(env_buf) - 1);
		SYSCALL(CLOSE, 1, env_fd);
		if (bytes > 0) {
			env_buf[bytes] = '\0';
			const char key[] = "PROOT_LOG_FILE=";
			for (long i = 0; i + (long)sizeof(key) - 1 < bytes; i++) {
				bool match = true;
				for (word_t k = 0; k < sizeof(key) - 1; k++) {
					if (env_buf[i + k] != key[k]) { match = false; break; }
				}
				if (match) {
					char *path = &env_buf[i + sizeof(key) - 1];
					int log_fd = SYSCALL(OPENAT, 4, -100 /* AT_FDCWD */, (word_t)path, 0x441 /* O_WRONLY|O_CREAT|O_APPEND */, 0644);
					if (log_fd >= 0) {
						cached_log_fd = log_fd;
						break;
					}
				}
			}
		}
	}
	return cached_log_fd;
}

static inline void loader_log_raw(const void *buf, word_t len)
{
	loader_write_raw(2, buf, len);
	int log_fd = loader_get_log_fd();
	if (log_fd >= 0) {
		loader_write_raw(log_fd, buf, len);
	}
}

static inline void loader_log_str(const char *s)
{
	if (!s) return;
	loader_log_raw(s, loader_strlen(s));
}

static inline void loader_log_dec(word_t val)
{
	char buf[32];
	int pos = 0;
	if (val == 0) {
		buf[pos++] = '0';
	} else {
		char tmp[32];
		int tpos = 0;
		while (val > 0) {
			tmp[tpos++] = '0' + (val % 10);
			val /= 10;
		}
		while (tpos > 0) {
			buf[pos++] = tmp[--tpos];
		}
	}
	loader_log_raw(buf, pos);
}

static inline void loader_log_hex(word_t val)
{
	char buf[32];
	buf[0] = '0';
	buf[1] = 'x';
	int pos = 2;
	if (val == 0) {
		buf[pos++] = '0';
	} else {
		char tmp[32];
		int tpos = 0;
		const char hexchars[] = "0123456789abcdef";
		while (val > 0) {
			tmp[tpos++] = hexchars[val & 0xf];
			val >>= 4;
		}
		while (tpos > 0) {
			buf[pos++] = tmp[--tpos];
		}
	}
	loader_log_raw(buf, pos);
}

#if defined(ARCH_ARM64)
static void loader_sigsegv_handler(int sig, siginfo_t *info, void *ucontext_ptr)
{
	word_t pid = SYSCALL_0(GETPID);
	struct ucontext *uc = (struct ucontext *)ucontext_ptr;
	struct sigcontext *sc = uc ? &uc->uc_mcontext : (void *)0;

	loader_log_str("[LOADER_SIGSEGV] pid=");
	loader_log_dec(pid);
	loader_log_str(" signal=");
	loader_log_dec((word_t)sig);
	loader_log_str(" si_code=");
	loader_log_dec(info ? (word_t)info->si_code : 0);
	loader_log_str(" si_addr=");
	loader_log_hex(info ? (word_t)info->si_addr : (sc ? sc->fault_address : 0));
	loader_log_str(" pc=");
	loader_log_hex(sc ? sc->pc : 0);
	loader_log_str(" lr=");
	loader_log_hex(sc ? sc->regs[30] : 0);
	loader_log_str(" sp=");
	loader_log_hex(sc ? sc->sp : 0);

	for (int i = 0; i <= 30; i++) {
		loader_log_str(" x");
		loader_log_dec((word_t)i);
		loader_log_str("=");
		loader_log_hex(sc ? sc->regs[i] : 0);
	}
	loader_log_str("\n");

	/* Reset SIGSEGV to default action so kernel handles subsequent fault */
	struct __kernel_sigaction sa_dfl;
	clear((word_t)&sa_dfl, (word_t)&sa_dfl + sizeof(sa_dfl));
	sa_dfl.sa_handler = (void *)0; /* SIG_DFL */
	SYSCALL(RT_SIGACTION, 4, 11, (word_t)&sa_dfl, 0, 8);

	/* Do not resume execution */
	SYSCALL(EXIT, 1, 128 + 11);
}
#endif

/**
 * Interpret the load script pointed to by @cursor.
 */
void _start(void *cursor)
{
#if defined(ARCH_ARM64)
	/* 15. Install minimal diagnostic signal handler for SIGSEGV */
	struct __kernel_sigaction sa;
	clear((word_t)&sa, (word_t)&sa + sizeof(sa));
	sa.sa_handler = (void *)loader_sigsegv_handler;
	sa.sa_flags = 0x00000004; /* SA_SIGINFO */
	SYSCALL(RT_SIGACTION, 4, 11, (word_t)&sa, 0, 8);
#endif

	word_t pid = 0;
#if defined(GETPID)
	pid = SYSCALL_0(GETPID);
#endif

	/* 16. LOADER_RUNTIME */
	word_t loader_entry = (word_t)&_start;
	word_t loader_base = loader_entry & ~0x3fff;
	while (*(unsigned int *)loader_base != 0x464c457f && loader_base > 0x1000) {
		loader_base -= 0x4000;
		if (loader_entry - loader_base > 0x100000) {
			loader_base = loader_entry & ~0xffff;
			break;
		}
	}
	loader_log_str("[LOADER_RUNTIME] pid=");
	loader_log_dec(pid);
	loader_log_str(" loader_base=");
	loader_log_hex(loader_base);
	loader_log_str(" loader_entry=");
	loader_log_hex(loader_entry);
	loader_log_str("\n");

	/* 11. LOADER_START - Before dereferencing cursor */
	loader_log_str("[LOADER_START] pid=");
	loader_log_dec(pid);
	loader_log_str(" cursor=");
	loader_log_hex((word_t)cursor);
	loader_log_str("\n");
	bool traced = false;
	bool reset_at_base = true;
	word_t at_base = 0;

	word_t fd = -1;
	word_t status;

	while(1) {
		/* Log cursor before accessing stmt->action (Section 11) */
		loader_log_str("[LOADER_STATEMENT] pid=");
		loader_log_dec(pid);
		loader_log_str(" cursor=");
		loader_log_hex((word_t)cursor);

		LoadStatement *stmt = cursor;
		loader_log_str(" action=");
		loader_log_dec(stmt->action);
		loader_log_str(" sizeof_LoadStatement=");
		loader_log_dec(sizeof(LoadStatement));
		loader_log_str("\n");

		switch (stmt->action) {
		case LOAD_ACTION_OPEN_NEXT:
			status = SYSCALL(CLOSE, 1, fd);
			if (unlikely((int) status < 0))
				FATAL();
			/* Fall through.  */

		case LOAD_ACTION_OPEN:
#if defined(OPEN)
			fd = SYSCALL(OPEN, 3, stmt->open.string_address, O_RDONLY, 0);
#else
			fd = SYSCALL(OPENAT, 4, AT_FDCWD, stmt->open.string_address, O_RDONLY, 0);
#endif
			if (unlikely((int) fd < 0))
				FATAL();

			reset_at_base = true;

			cursor += LOAD_STATEMENT_SIZE(*stmt, open);
			break;

		case LOAD_ACTION_MMAP_FILE:
			status = SYSCALL(MMAP, 6, stmt->mmap.addr, stmt->mmap.length,
					stmt->mmap.prot, MAP_PRIVATE | MAP_FIXED, fd,
					stmt->mmap.offset >> MMAP_OFFSET_SHIFT);
			if (unlikely(status != stmt->mmap.addr))
				FATAL();

			if (stmt->mmap.clear_length != 0)
				clear(stmt->mmap.addr + stmt->mmap.length - stmt->mmap.clear_length,
					stmt->mmap.addr + stmt->mmap.length);

			if (reset_at_base) {
				at_base = stmt->mmap.addr;
				reset_at_base = false;
			}

			cursor += LOAD_STATEMENT_SIZE(*stmt, mmap);
			break;

		case LOAD_ACTION_MMAP_ANON:
			status = SYSCALL(MMAP, 6, stmt->mmap.addr, stmt->mmap.length,
					stmt->mmap.prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
			if (unlikely(status != stmt->mmap.addr))
				FATAL();

			cursor += LOAD_STATEMENT_SIZE(*stmt, mmap);
			break;

		case LOAD_ACTION_MAKE_STACK_EXEC:
			SYSCALL(MPROTECT, 3,
				stmt->make_stack_exec.start, 1,
				PROT_READ | PROT_WRITE | PROT_EXEC | PROT_GROWSDOWN);

			cursor += LOAD_STATEMENT_SIZE(*stmt, make_stack_exec);
			break;

		case LOAD_ACTION_START_TRACED:
			traced = true;
			/* Fall through.  */

		case LOAD_ACTION_START: {
			/* 12. LOADER_START_STATEMENT */
			loader_log_str("[LOADER_START_STATEMENT] pid=");
			loader_log_dec(pid);
			loader_log_str(" cursor=");
			loader_log_hex((word_t)cursor);
			loader_log_str(" stack_pointer=");
			loader_log_hex(stmt->start.stack_pointer);
			loader_log_str(" entry_point=");
			loader_log_hex(stmt->start.entry_point);
			loader_log_str(" at_phdr=");
			loader_log_hex(stmt->start.at_phdr);
			loader_log_str(" at_phent=");
			loader_log_dec(stmt->start.at_phent);
			loader_log_str(" at_phnum=");
			loader_log_dec(stmt->start.at_phnum);
			loader_log_str(" at_entry=");
			loader_log_hex(stmt->start.at_entry);
			loader_log_str(" at_execfn=");
			loader_log_hex(stmt->start.at_execfn);
			loader_log_str(" offsetof_action=");
			loader_log_dec(offsetof(LoadStatement, action));
			loader_log_str(" offsetof_start_stack_pointer=");
			loader_log_dec(offsetof(LoadStatement, start.stack_pointer));
			loader_log_str(" offsetof_start_entry_point=");
			loader_log_dec(offsetof(LoadStatement, start.entry_point));
			loader_log_str(" offsetof_start_at_phdr=");
			loader_log_dec(offsetof(LoadStatement, start.at_phdr));
			loader_log_str(" offsetof_start_at_phent=");
			loader_log_dec(offsetof(LoadStatement, start.at_phent));
			loader_log_str(" offsetof_start_at_phnum=");
			loader_log_dec(offsetof(LoadStatement, start.at_phnum));
			loader_log_str(" offsetof_start_at_entry=");
			loader_log_dec(offsetof(LoadStatement, start.at_entry));
			loader_log_str(" offsetof_start_at_execfn=");
			loader_log_dec(offsetof(LoadStatement, start.at_execfn));
			loader_log_str("\n");

			/* 13. LOADER_STACK_BEGIN */
			loader_log_str("[LOADER_STACK_BEGIN] pid=");
			loader_log_dec(pid);
			loader_log_str(" stack_pointer=");
			loader_log_hex(stmt->start.stack_pointer);
			loader_log_str("\n");

			word_t *cursor2 = (word_t *) stmt->start.stack_pointer;
			const word_t argc = cursor2[0];
			const word_t at_execfn = cursor2[1];
			word_t name;

			/* 13. LOADER_STACK */
			loader_log_str("[LOADER_STACK] pid=");
			loader_log_dec(pid);
			loader_log_str(" stack_pointer=");
			loader_log_hex(stmt->start.stack_pointer);
			loader_log_str(" argc=");
			loader_log_dec(argc);
			loader_log_str("\n");

			status = SYSCALL(CLOSE, 1, fd);
			if (unlikely((int) status < 0))
				FATAL();

			/* Right after execve, the stack content is as follow:
			 *
			 *   +------+--------+--------+--------+
			 *   | argc | argv[] | envp[] | auxv[] |
			 *   +------+--------+--------+--------+
			 */

			/* Skip argv[].  */
			cursor2 += argc + 1;

			/* Skip envp[].  */
			do cursor2++; while (cursor2[0] != 0);
			cursor2++;

			/* Adjust auxv[].  */
			do {
				switch (cursor2[0]) {
				case AT_PHDR:
					cursor2[1] = stmt->start.at_phdr;
					break;

				case AT_PHENT:
					cursor2[1] = stmt->start.at_phent;
					break;

				case AT_PHNUM:
					cursor2[1] = stmt->start.at_phnum;
					break;

				case AT_ENTRY:
					cursor2[1] = stmt->start.at_entry;
					break;

				case AT_BASE:
					cursor2[1] = at_base;
					break;

				case AT_EXECFN:
					/* stmt->start.at_execfn can't be used for now since it is
					 * currently stored in a location that will be scratched
					 * by the process (below the final stack pointer).  */
					cursor2[1] = at_execfn;
					break;

				default:
					break;
				}
				cursor2 += 2;
			} while (cursor2[0] != AT_NULL);

			/* Note that only 2 arguments are actually necessary... */
			name = basename(stmt->start.at_execfn);
			SYSCALL(PRCTL, 3, PR_SET_NAME, name, 0);

			/* 14. LOADER_TRANSFER */
			loader_log_str("[LOADER_TRANSFER] pid=");
			loader_log_dec(pid);
			loader_log_str(" stack_pointer=");
			loader_log_hex(stmt->start.stack_pointer);
			loader_log_str(" entry_point=");
			loader_log_hex(stmt->start.entry_point);
			loader_log_str("\n");

			/* 14. GUEST_ENTRY */
			loader_log_str("[GUEST_ENTRY] pid=");
			loader_log_dec(pid);
			loader_log_str(" entry_point=");
			loader_log_hex(stmt->start.entry_point);
			loader_log_str(" stack_pointer=");
			loader_log_hex(stmt->start.stack_pointer);
			loader_log_str("\n");

			if (unlikely(traced))
				SYSCALL(EXECVE, 6, 1,
					stmt->start.stack_pointer,
					stmt->start.entry_point, 2, 3, 4);
			else
				BRANCH(stmt->start.stack_pointer, stmt->start.entry_point);
			FATAL();
		}

		default:
			FATAL();
		}
	}

	FATAL();
}
