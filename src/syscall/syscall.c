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

#include <assert.h>      /* assert(3), */
#include <limits.h>      /* PATH_MAX, */
#include <string.h>      /* strlen(3), */
#include <errno.h>       /* errno(3), E* */

#include "syscall/syscall.h"
#include "syscall/chain.h"
#include "extension/extension.h"
#include "tracee/tracee.h"
#include "tracee/reg.h"
#include "tracee/mem.h"
#include "cli/note.h"

/**
 * Copy in @path a C string (PATH_MAX bytes max.) from the @tracee's
 * memory address space pointed to by the @reg argument of the
 * current syscall.  This function returns -errno if an error occured,
 * otherwise it returns the size in bytes put into the @path.
 */
int get_sysarg_path(const Tracee *tracee, char path[PATH_MAX], Reg reg)
{
	int size;
	word_t src;

	src = peek_reg(tracee, CURRENT, reg);
	src = UNTAG_ADDRESS(src);

	/* Check if the parameter is not NULL. Technically we should
	 * not return an -EFAULT for this special value since it is
	 * allowed for some syscall, utimensat(2) for instance. */
	if (src == 0) {
		path[0] = '\0';
		return 0;
	}

	/* Get the path from the tracee's memory space. */
	size = read_path(tracee, path, src);
	if (size < 0)
		return size;

	path[size] = '\0';
	return size;
}

/**
 * Copy @size bytes of the data pointed to by @tracer_ptr into a
 * @tracee's memory block and make the @reg argument of the current
 * syscall points to this new block.  This function returns -errno if
 * an error occured, otherwise 0.
 */
static int set_sysarg_data(Tracee *tracee, const void *tracer_ptr, word_t size, Reg reg)
{
	word_t tracee_ptr;
	int status;

	/* Allocate space into the tracee's memory to host the new data. */
	tracee_ptr = alloc_mem(tracee, size);
	if (tracee_ptr == 0) {
		note(tracee, ERROR, INTERNAL, "set_sysarg_data: alloc_mem(%zd) failed (stack underflow)", (size_t) size);
		return -EFAULT;
	}

	/* Copy the new data into the previously allocated space. */
	status = write_data(tracee, tracee_ptr, tracer_ptr, size);
	if (status < 0) {
		note(tracee, ERROR, SYSTEM, "set_sysarg_data: write_data(0x%lx, %zd) failed: %s", tracee_ptr, (size_t) size, strerror(-status));
		return status;
	}

	/* Make this argument point to the new data. */
	poke_reg(tracee, reg, tracee_ptr);

	return 0;
}

/**
 * Copy @path to a @tracee's memory block and make the @reg argument
 * of the current syscall points to this new block.  This function
 * returns -errno if an error occured, otherwise 0.
 */
int set_sysarg_path(Tracee *tracee, const char path[PATH_MAX], Reg reg)
{
	return set_sysarg_data(tracee, path, strlen(path) + 1, reg);
}

void translate_syscall(Tracee *tracee)
{
	const bool is_enter_stage = IS_IN_SYSENTER(tracee);
	int status;

	assert(tracee->exe != NULL);

	status = fetch_regs(tracee);
	if (status < 0)
		return;

	if (is_enter_stage) {
		/* Never restore original register values at the end
		 * of this stage.  */
		tracee->restore_original_regs = false;

		print_current_regs(tracee, 3, "sysenter start");

		/* Translate the syscall only if it was actually
		 * requested by the tracee, it is not a syscall
		 * chained by PRoot.  */
		if (tracee->chain.syscalls == NULL) {
			save_current_regs(tracee, ORIGINAL);
			status = translate_syscall_enter(tracee);
			save_current_regs(tracee, MODIFIED);
		}
		else {
			status = notify_extensions(tracee, SYSCALL_CHAINED_ENTER, 0, 0);
			tracee->restart_how = PTRACE_SYSCALL;
		}

		/* Remember the tracee status for the "exit" stage and
		 * avoid the actual syscall if an error was reported
		 * by the translation/extension. */
		if (status < 0) {
			Sysnum orig_sysnum = get_sysnum(tracee, ORIGINAL);
			set_sysnum(tracee, PR_void);
			poke_reg(tracee, SYSARG_RESULT, (word_t) status);
			tracee->status = status;
			if (tracee->seccomp == ENABLED) {
				tracee->restart_how = PTRACE_SYSCALL;
				tracee->sysexit_pending = true;
			}
			note(tracee, INFO, INTERNAL, "[SYSCALL_ENTER_ERR] pid=%d: sysnum=%ld (%s) status=%d -> PR_void, restart_how=%d, sysexit_pending=%d",
				tracee->pid, (long)orig_sysnum, stringify_sysnum(orig_sysnum), status, tracee->restart_how, tracee->sysexit_pending);
		}
		else
			tracee->status = 1;

		/* Restore tracee's stack pointer now if it won't hit
		 * the sysexit stage (i.e. when seccomp is enabled and
		 * there's nothing else to do).  */
		if (tracee->restart_how == PTRACE_CONT) {
			tracee->status = 0;
			poke_reg(tracee, STACK_POINTER, peek_reg(tracee, ORIGINAL, STACK_POINTER));
		}
	}
	else {
		/* By default, restore original register values at the
		 * end of this stage.  */
		tracee->restore_original_regs = true;

		print_current_regs(tracee, 5, "sysexit start");

		int prev_status = tracee->status;
		Sysnum orig_sysnum = get_sysnum(tracee, ORIGINAL);

		/* Translate the syscall only if it was actually
		 * requested by the tracee, it is not a syscall
		 * chained by PRoot.  */
		if (tracee->chain.syscalls == NULL)
			translate_syscall_exit(tracee);
		else
			(void) notify_extensions(tracee, SYSCALL_CHAINED_EXIT, 0, 0);

		word_t final_result = peek_reg(tracee, CURRENT, SYSARG_RESULT);
		if ((int)final_result < 0 || prev_status < 0) {
			word_t orig_arg1 = peek_reg(tracee, ORIGINAL, SYSARG_1);
			word_t orig_arg2 = peek_reg(tracee, ORIGINAL, SYSARG_2);
			word_t orig_arg3 = peek_reg(tracee, ORIGINAL, SYSARG_3);
			word_t orig_arg4 = peek_reg(tracee, ORIGINAL, SYSARG_4);
			word_t curr_arg1 = peek_reg(tracee, CURRENT, SYSARG_1);
			word_t curr_arg2 = peek_reg(tracee, CURRENT, SYSARG_2);

			char guest_path[PATH_MAX] = {0};
			char host_path[PATH_MAX] = {0};

			switch (orig_sysnum) {
			case PR_openat:
			case PR_faccessat:
			case PR_faccessat2:
			case PR_fstatat64:
			case PR_newfstatat:
			case PR_readlinkat:
			case PR_unlinkat:
			case PR_mkdirat:
			case PR_mknodat:
				(void) read_path(tracee, guest_path, orig_arg2);
				(void) read_path(tracee, host_path, curr_arg2);
				break;
			case PR_open:
			case PR_access:
			case PR_stat:
			case PR_stat64:
			case PR_lstat:
			case PR_lstat64:
			case PR_statfs:
			case PR_statfs64:
			case PR_chdir:
			case PR_rmdir:
			case PR_unlink:
			case PR_readlink:
			case PR_execve:
			case PR_execveat:
				(void) read_path(tracee, guest_path, orig_arg1);
				(void) read_path(tracee, host_path, curr_arg1);
				break;
			default:
				break;
			}

			if (guest_path[0] != '\0' || host_path[0] != '\0') {
				note(tracee, INFO, INTERNAL, "[SYSCALL_EXIT_ERR] pid=%d: sysnum=%ld (%s) result=%ld (errno=%d), dirfd=%ld, guest_path='%s', host_path='%s', orig_args=(0x%lx, 0x%lx, 0x%lx, 0x%lx)",
					tracee->pid, (long)orig_sysnum, stringify_sysnum(orig_sysnum),
					(long)final_result, (int)(-((int)final_result)),
					(long)orig_arg1, guest_path, host_path,
					orig_arg1, orig_arg2, orig_arg3, orig_arg4);
			} else {
				note(tracee, INFO, INTERNAL, "[SYSCALL_EXIT_ERR] pid=%d: sysnum=%ld (%s) result=%ld (errno=%d), orig_args=(0x%lx, 0x%lx, 0x%lx, 0x%lx), curr_args=(0x%lx, 0x%lx)",
					tracee->pid, (long)orig_sysnum, stringify_sysnum(orig_sysnum),
					(long)final_result, (int)(-((int)final_result)),
					orig_arg1, orig_arg2, orig_arg3, orig_arg4,
					curr_arg1, curr_arg2);
			}
		}

		/* Reset the tracee's status. */
		tracee->status = 0;

		/* Insert the next chained syscall, if any.  */
		if (tracee->chain.syscalls != NULL)
			chain_next_syscall(tracee);
	}

	(void) push_regs(tracee);

	if (is_enter_stage)
		print_current_regs(tracee, 5, "sysenter end" );
	else
		print_current_regs(tracee, 4, "sysexit end");
}
