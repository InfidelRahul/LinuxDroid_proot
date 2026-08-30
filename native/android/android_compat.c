/*
 * android_compat.c - Android compatibility layer implementation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "android_compat.h"

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/syscall.h>
#endif

#if defined(__ANDROID__)
#define LINUXDROID_IS_ANDROID 1
#else
#define LINUXDROID_IS_ANDROID 0
#endif

static int probe_process_vm(void)
{
#if defined(SYS_process_vm_readv) && defined(__linux__)
	/* A read of pid 1 from an unprivileged caller returns EPERM/ESRCH on
	 * real kernels, but ENOSYS/EINVAL if the syscall is absent/blocked.
	 * Absence/blocking is what we care about for capability detection. */
	struct iovec iov;
	void *p = (void *)0x1;
	iov.iov_base = (void *)&iov;
	iov.iov_len = sizeof(iov);
	long r = syscall(SYS_process_vm_readv, (pid_t)1, &iov, 1,
			 &(struct iovec){ .iov_base = p, .iov_len = 1 }, 1, 0);
	if (r == -1 && (errno == ENOSYS || errno == EINVAL))
		return 0;
	return 1;
#else
	return 0;
#endif
}

static int probe_seccomp_filter(void)
{
#if defined(__linux__) && defined(SECCOMP_MODE_FILTER)
	pid_t pid = fork();
	if (pid < 0)
		return 0;
	if (pid == 0) {
		struct sock_filter filter[] = {
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		};
		struct sock_fprog prog = {
			.len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
			.filter = filter,
		};
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0)
			_exit(1);
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
#else
	return 0;
#endif
}

void linuxdroid_probe_features(struct linuxdroid_features *out)
{
	if (!out)
		return;
	out->is_android = LINUXDROID_IS_ANDROID;
#if defined(__aarch64__)
	out->is_aarch64 = 1;
#else
	out->is_aarch64 = 0;
#endif
	out->has_process_vm = probe_process_vm();
	out->has_seccomp_filter = probe_seccomp_filter();
}

const char *linuxdroid_default_shell(void)
{
#if defined(__ANDROID__)
	return "/system/bin/sh";
#else
	return "/bin/sh";
#endif
}

const char *linuxdroid_dynamic_linker(void)
{
#if defined(__aarch64__)
	return "/system/bin/linker64";
#elif defined(__x86_64__)
	return "/system/bin/linker64";
#else
	return "/system/bin/linker";
#endif
}
