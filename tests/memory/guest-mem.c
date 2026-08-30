/*
 * guest-mem.c - guest-side tracee-memory probe for LinuxDroid-PRoot.
 *
 * This program is meant to be run UNDER the PRoot engine (host Linux or
 * Android).  Every check below is an ordinary guest syscall whose
 * pointer arguments the engine must read from / write into tracee
 * memory through src/tracee/mem.c (read_data / write_data / read_string
 * / peek_word / poke_word, process_vm_* fast path with ptrace PEEK/POKE
 * fallback).  It therefore regression-tests the engine's whole memory
 * boundary, including the ARM64 tagged-pointer normalization: the
 * "tagged" check passes a pointer with a top-byte tag (as the scudo
 * allocator / HWASan produce on Android) to openat(2) and expects the
 * engine to read the path at the untagged address.
 *
 * Expected results:
 *   - on aarch64 (TBI tagging is an ARM64 property): the tagged openat
 *     must SUCCEED after normalization (the engine substitutes its own
 *     untagged buffer for the syscall, so success does not depend on
 *     kernel TBI support).
 *   - on other architectures normalization is the identity and the
 *     kernel must reject the invalid pointer: the tagged openat fails
 *     with an errno, which is the correct, upstream-preserving result.
 *
 * Output is human- and machine-readable "[PASS]/[FAIL] name detail".
 * Exit status: 0 = all pass, 1 = at least one failure.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>

/* Same tag byte as the original Android ARM64 EINVAL report
 * (ptrace(PTRACE_PEEKDATA, pid, 0xb400007c4165ec40, ...) -> EINVAL). */
#define TAG_BYTE 0xb4

static int fails = 0;

static void ok(const char *name, const char *detail)
{
	printf("[PASS] %-28s %s\n", name, detail ? detail : "");
}

static void bad(const char *name, const char *detail)
{
	printf("[FAIL] %-28s %s\n", name, detail ? detail : "");
	fails++;
}

static const char *shell(void)
{
#if defined(__ANDROID__)
	return "/system/bin/sh";
#else
	return "/bin/sh";
#endif
}

static int is_aarch64(void)
{
#if defined(__aarch64__)
	return 1;
#else
	return 0;
#endif
}

int main(void)
{
	char buf[PATH_MAX];
	struct utsname uts;
	ssize_t n;
	int fd;
	long r;

	/* ---- 1. write_data: getcwd writes into guest memory ---- */
	if (getcwd(buf, sizeof(buf)) != NULL)
		ok("getcwd (write_data)", buf);
	else
		bad("getcwd (write_data)", strerror(errno));

	/* ---- 2. write_data: readlink writes into guest memory ---- */
	n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		ok("readlink (write_data)", buf);
	} else {
		bad("readlink (write_data)", strerror(errno));
	}

	/* ---- 3. read_string: plain untagged path stays unchanged ---- */
	fd = open("/proc/self/exe", O_RDONLY);
	if (fd >= 0) {
		close(fd);
		ok("open untagged (read_string)", "normal address unchanged");
	} else {
		bad("open untagged (read_string)", strerror(errno));
	}

	/* ---- 4. read_string through an explicitly TAGGED pointer ---- */
	{
		char path[] = "/proc/self/exe";	/* stable guest address */
		uintptr_t tagged = (uintptr_t)path
			| ((uintptr_t)TAG_BYTE << 56);

		r = syscall(SYS_openat, AT_FDCWD, (const char *)tagged,
			    O_RDONLY, 0);
		if (is_aarch64()) {
			/* ARM64: engine must normalize and succeed. */
			if (r >= 0) {
				close((int)r);
				ok("open tagged (normalization)",
				   "0xb4-tagged path read via untagged address");
			} else {
				char d[128];
				snprintf(d, sizeof(d),
					 "tagged path failed: %s", strerror(errno));
				bad("open tagged (normalization)", d);
			}
		} else {
			/* Non-ARM64: normalization is the identity here,
			 * the kernel must reject the invalid pointer. */
			if (r < 0) {
				ok("open tagged (non-ARM64)",
				   "invalid pointer rejected as expected");
			} else {
				close((int)r);
				bad("open tagged (non-ARM64)",
				    "unexpected success: pointer was altered?");
			}
		}
	}

	/* ---- 5. read_data/write_data: execve of argv/envp ---- */
	{
		pid_t pid = fork();
		if (pid == 0) {
			execl(shell(), "sh", "-c", "exit 0", (char *)NULL);
			_exit(127);
		}
		int status = 0;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			ok("execve argv/envp (read/write)", shell());
		else
			bad("execve argv/envp (read/write)", "child did not exit 0");
	}

	/* ---- environment report (useful on device) ---- */
	if (uname(&uts) == 0)
		printf("# guest: %s %s %s\n", uts.machine, uts.sysname,
		       uts.release);

	return fails == 0 ? 0 : 1;
}
