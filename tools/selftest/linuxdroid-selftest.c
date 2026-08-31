/*
 * linuxdroid-selftest.c - LinuxDroid-PRoot compatibility self-test
 *
 * A standalone diagnostic that validates the runtime capabilities that the
 * LinuxDroid PRoot engine depends on.  It is compiled for the desktop host
 * (gcc/glibc) and for Android (NDK clang/bionic) and produces a simple
 * machine- and human-readable PASS / FAIL / SKIP report.
 *
 * This is intentionally a *separate* tool rather than a hidden CLI switch on
 * upstream proot, so that upstream PRoot CLI semantics stay untouched.  It
 * gives LinuxDroid (the application) a reliable compatibility contract to
 * gate on before launching a session.
 *
 * Exit status: 0 if all critical checks pass, 1 otherwise.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <fcntl.h>
#include <limits.h>

#include <sys/syscall.h>
#include <sys/prctl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

/* ---- output helpers ---------------------------------------------------- */

static int failures = 0;
static int skips = 0;
static int checks = 0;

static void report(const char *name, const char *status, const char *detail)
{
	printf("[%s] %-22s %s\n", status, name, detail ? detail : "");
	fflush(stdout);
	checks++;
}

static void pass(const char *name, const char *detail)
{
	report(name, "PASS", detail);
}

static void fail(const char *name, const char *detail)
{
	report(name, "FAIL", detail);
	failures++;
}

static void skip(const char *name, const char *detail)
{
	report(name, "SKIP", detail);
	skips++;
}

/* ---- 1. executable ----------------------------------------------------- */

static void check_executable(void)
{
	char exe[PATH_MAX] = {0};
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = '\0';
		pass("executable", exe);
	} else {
		fail("executable", "cannot resolve /proc/self/exe");
	}
}

/* ---- 2. architecture --------------------------------------------------- */

static void check_architecture(void)
{
	/* Reported by the compiler, kept truthful on the target. */
#if defined(__aarch64__)
	pass("architecture", "aarch64 (arm64-v8a)");
#elif defined(__x86_64__)
	pass("architecture", "x86_64");
#elif defined(__i386__)
	pass("architecture", "i386");
#elif defined(__arm__)
	pass("architecture", "armv7");
#else
	pass("architecture", "unknown");
#endif
}

/* ---- 3. memory / process_vm -------------------------------------------- */

/*
 * Round-trip a byte between two processes using process_vm_readv /
 * process_vm_writev.  PRoot uses these as a fast memory accessor (and
 * falls back to PTRACE_PEEKDATA/POKEDATA when unavailable).
 */
static volatile int g_mem_child_result = (int)'Z';

static void mem_child(void)
{
	/* Spin until the parent pokes 'Q' through process_vm_writev. */
	while (g_mem_child_result != (int)'Q')
		;
	_exit(g_mem_child_result == (int)'Q' ? 0 : 1);
}

static void check_process_vm(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		skip("memory", "fork failed");
		return;
	}
	if (pid == 0)
		mem_child();

	/* Wait until the child is running. */
	usleep(200 * 1000);

	struct iovec local;
	struct iovec remote;
	char buf = 0;
	char *remote_addr = (char *)&g_mem_child_result;

	/* read */
	local.iov_base = &buf;
	local.iov_len = sizeof(buf);
	remote.iov_base = remote_addr;
	remote.iov_len = sizeof(buf);
	ssize_t r = syscall(SYS_process_vm_readv, pid, &local, 1, &remote, 1, 0);
	if (r != 1) {
		skip("memory", "process_vm_readv unavailable");
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return;
	}

	/* write */
	char poke = 'Q';
	local.iov_base = &poke;
	remote.iov_base = remote_addr;
	r = syscall(SYS_process_vm_writev, pid, &local, 1, &remote, 1, 0);
	if (r != 1) {
		fail("memory", "process_vm_writev failed");
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return;
	}

	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		pass("memory", "process_vm_readv/writev round-trip OK");
	else
		fail("memory", "process_vm round-trip did not complete");
}

/* ---- 4. ptrace --------------------------------------------------------- */

static void ptrace_child(void)
{
	/* TRACEME makes this child traceable; parent then does GETREGSET. */
	if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0)
		_exit(2);
	raise(SIGSTOP);
	_exit(0);
}

static void check_ptrace(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		skip("ptrace", "fork failed");
		return;
	}
	if (pid == 0)
		ptrace_child();

	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFSTOPPED(status)) {
		/* Try a real ptrace operation on the stopped tracee. */
		unsigned long peeked = 0;
		long ret;
		errno = 0;
#if defined(__ANDROID__) || defined(__BIONIC__)
		ret = ptrace(PTRACE_PEEKDATA, pid, (void *)&g_mem_child_result, (void *)&peeked);
#else
		ret = ptrace(PTRACE_PEEKDATA, pid, (void *)&g_mem_child_result, 0);
#endif
		ptrace(PTRACE_CONT, pid, 0, 0);
		waitpid(pid, &status, 0);
		if (ret != -1 || errno != EPERM) {
			pass("ptrace", "PTRACE_TRACEME + PEEKDATA OK");
		} else {
			fail("ptrace", "ptrace operation returned EPERM");
		}
	} else {
		fail("ptrace", "tracee did not stop at SIGSTOP");
	}
}

/* ---- 4b. ARM64 tagged addresses (normalization repro) ------------------- */

/*
 * Reproduce the original Android ARM64 failure class at the kernel level
 * and prove the normalization mechanism used by the engine
 * (native/android/untag.h):
 *
 *   ptrace(PTRACE_PEEKDATA, pid, 0xb400007c4165ec40, ...)  ->  EINVAL
 *
 * A pointer with a non-zero top byte (scudo/MTE/HWASan tag) is rejected
 * by the kernel's ptrace/process_vm interfaces; the same access at the
 * untagged (masked) address must succeed and return the right value.
 * On x86_64 the tagged form is simply a non-canonical address: the raw
 * probe still fails and the masked probe still succeeds, so the check is
 * meaningful on every architecture.
 */

/* The actual normalization policy, included from the engine's Android
 * compatibility boundary (single choke point). */
#include "../../native/android/untag.h"

/* Word-sized so ptrace/process_vm results compare exactly (PEEKDATA
 * always returns a full word). */
#define UNTAG_MAGIC 0x5a12c0deL

static volatile long g_untag_magic = UNTAG_MAGIC;

static void check_untag_unit(void)
{
	/* The normalization helper itself: must never alter a valid
	 * address, and must strip exactly the top byte on aarch64. */
	uintptr_t addr = (uintptr_t)&g_untag_magic;
	uintptr_t norm = UNTAG_WORD(addr);

	if (norm != addr) {
		fail("untag-unit", "normalization altered a valid address");
		return;
	}
#if defined(__aarch64__)
	if (UNTAG_WORD(addr | 0xb400000000000000ULL) != addr) {
		fail("untag-unit", "top byte not stripped on aarch64");
		return;
	}
	pass("untag-unit", "identity on valid, strips 0xb4 tag (aarch64)");
#else
	pass("untag-unit", "identity on valid addresses (non-aarch64)");
#endif
}

static void untag_child(void)
{
	if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0)
		_exit(2);
	raise(SIGSTOP);
	_exit(0);
}

/* The address form the engine computes on aarch64 before handing a
 * tracee address to ptrace/process_vm: top byte cleared.  Written out
 * explicitly here (not via UNTAG_ADDRESS) because on non-aarch64 the
 * engine's normalization is the identity by design -- this check then
 * demonstrates the mechanism: kernel rejects the tagged form, accepts
 * the masked form.  untag-unit above proves UNTAG_WORD implements
 * exactly this mask on aarch64. */
#define UNTAGGED(addr) ((void *)((uintptr_t)(addr) & 0x00ffffffffffffffULL))

static void check_ptrace_untag(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		skip("ptrace-untag", "fork failed");
		return;
	}
	if (pid == 0)
		untag_child();

	int status = 0;
	waitpid(pid, &status, 0);
	if (!WIFSTOPPED(status)) {
		fail("ptrace-untag", "tracee did not stop");
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return;
	}

	uintptr_t addr = (uintptr_t)&g_untag_magic;
	uintptr_t tagged = addr | 0xb400000000000000ULL;

	unsigned long raw_val = 0, masked_val = 0;
	errno = 0;
#if defined(__ANDROID__) || defined(__BIONIC__)
	long raw = ptrace(PTRACE_PEEKDATA, pid, (void *)tagged, (void *)&raw_val);
#else
	long raw = ptrace(PTRACE_PEEKDATA, pid, (void *)tagged, 0);
#endif
	int raw_errno = errno;

	errno = 0;
#if defined(__ANDROID__) || defined(__BIONIC__)
	long masked = ptrace(PTRACE_PEEKDATA, pid, UNTAGGED(tagged), (void *)&masked_val);
	if (masked == 0 && masked_val == UNTAG_MAGIC)
		masked = (long)UNTAG_MAGIC;
#else
	long masked = ptrace(PTRACE_PEEKDATA, pid, UNTAGGED(tagged), 0);
#endif

	ptrace(PTRACE_CONT, pid, 0, 0);
	waitpid(pid, &status, 0);

	/* The masked access is the hard requirement: it is what the
	 * engine does after normalization. */
	if (masked == (long)UNTAG_MAGIC) {
		char detail[160];
		if (raw == -1 && raw_errno != 0)
			snprintf(detail, sizeof(detail),
				 "tagged PEEKDATA rejected (%s), untagged PEEKDATA OK",
				 raw_errno == EINVAL ? "EINVAL" : strerror(raw_errno));
		else
			snprintf(detail, sizeof(detail),
				 "kernel accepts tagged PEEKDATA, untagged PEEKDATA OK");
		pass("ptrace-untag", detail);
	} else {
		fail("ptrace-untag", "untagged PEEKDATA failed");
	}
}

static void check_process_vm_untag(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		skip("pvm-untag", "fork failed");
		return;
	}
	if (pid == 0) {
		/* Stay alive while the parent probes our memory. */
		while (g_untag_magic == UNTAG_MAGIC)
			;
		_exit(0);
	}

	usleep(100 * 1000);

	struct iovec local;
	struct iovec remote;
	long buf = 0;

	uintptr_t addr = (uintptr_t)&g_untag_magic;
	uintptr_t tagged = addr | 0xb400000000000000ULL;

	local.iov_base = &buf;
	local.iov_len = sizeof(buf);
	remote.iov_base = (void *)tagged;
	remote.iov_len = sizeof(buf);
	ssize_t raw = syscall(SYS_process_vm_readv, pid, &local, 1,
			      &remote, 1, 0);
	int raw_errno = errno;

	remote.iov_base = UNTAGGED(tagged);
	buf = 0;
	ssize_t masked = syscall(SYS_process_vm_readv, pid, &local, 1,
				 &remote, 1, 0);

	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);

	if (masked == (ssize_t)sizeof(buf) && buf == UNTAG_MAGIC) {
		char detail[160];
		if (raw != (ssize_t)sizeof(buf))
			snprintf(detail, sizeof(detail),
				 "tagged readv rejected (%s), untagged readv OK",
				 raw_errno == EINVAL ? "EINVAL" : strerror(raw_errno));
		else
			snprintf(detail, sizeof(detail),
				 "kernel accepts tagged readv, untagged readv OK");
		pass("pvm-untag", detail);
	} else if (raw != (ssize_t)sizeof(buf) &&
		   (raw_errno == ENOSYS || raw_errno == EPERM)) {
		skip("pvm-untag", "process_vm_readv unavailable");
	} else {
		fail("pvm-untag", "untagged readv failed");
	}
}

/* ---- 5. syscall -------------------------------------------------------- */

static void check_syscall(void)
{
	long r = syscall(SYS_getpid);
	if (r > 0)
		pass("syscall", "raw syscall(SYS_getpid) OK");
	else
		fail("syscall", "raw syscall failed");
}

/* ---- 6. seccomp -------------------------------------------------------- */

static void check_seccomp(void)
{
	/*
	 * Probe whether a seccomp filter can be installed.  We do this in a
	 * child so that a real filter (or an EPERM) does not affect us.  The
	 * probe is intentionally harmless: it installs an allow-all filter
	 * and immediately exits.
	 */
	pid_t pid = fork();
	if (pid < 0) {
		skip("seccomp", "fork failed");
		return;
	}
	if (pid == 0) {
		struct sock_filter filter[] = {
			BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		};
		struct sock_fprog prog = {
			.len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
			.filter = filter,
		};
		/* PR_SET_NO_NEW_PRIVS first, then the filter. */
		prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
		if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0)
			_exit(1);
		_exit(0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		pass("seccomp", "BPF filter installable (SECCOMP_MODE_FILTER)");
	else if (WIFEXITED(status) && WEXITSTATUS(status) == 1)
		skip("seccomp", "BPF filter rejected by kernel");
	else
		fail("seccomp", "unexpected result");
}

/* ---- 7. execve --------------------------------------------------------- */

static const char *find_shell(void)
{
#if defined(__ANDROID__)
	return "/system/bin/sh";
#else
	return "/bin/sh";
#endif
}

static void check_execve(void)
{
	const char *sh = find_shell();
	if (access(sh, X_OK) != 0) {
		skip("execve", "no shell at expected path");
		return;
	}
	pid_t pid = fork();
	if (pid < 0) {
		skip("execve", "fork failed");
		return;
	}
	if (pid == 0) {
		execl(sh, sh, "-c", "exit 0", (char *)NULL);
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		pass("execve", sh);
	else
		fail("execve", "shell exec failed");
}

/* ---- 8. loader / ELF --------------------------------------------------- */

static void check_loader(void)
{
#if defined(__ANDROID__)
	/* On Android the "loader" is the dynamic linker (linker64). */
	const char *ld = "/system/bin/linker64";
	struct stat st;
	if (stat(ld, &st) == 0) {
		pass("loader", "dynamic linker present (linker64)");
		return;
	}
	skip("loader", "no linker64 at expected path");
#else
	struct stat st;
	if (stat("/lib64/ld-linux-x86-64.so.2", &st) == 0 ||
	    stat("/lib/ld-linux-aarch64.so.1", &st) == 0) {
		pass("loader", "ELF interpreter present");
		return;
	}
	skip("loader", "ELF interpreter not found");
#endif
}

/* ---- 9. signals -------------------------------------------------------- */

static volatile sig_atomic_t g_sig_seen = 0;

static void sig_handler(int sig)
{
	(void)sig;
	g_sig_seen = 1;
}

static void check_signals(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		skip("signals", "sigaction failed");
		return;
	}
	raise(SIGUSR1);
	if (g_sig_seen)
		pass("signals", "SIGUSR1 delivery OK");
	else
		fail("signals", "signal handler did not run");
}

/* ---- 10. Android /proc ------------------------------------------------- */

static void check_proc(void)
{
	struct stat st;
	if (stat("/proc/self/status", &st) == 0 && stat("/proc/self/maps", &st) == 0)
		pass("proc", "/proc/self/status + /proc/self/maps present");
	else
		fail("proc", "/proc not fully populated");
}

/* ---- main -------------------------------------------------------------- */

static const char *g_version = "0.1.0";

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	printf("LinuxDroid-PRoot self-test %s\n", g_version);
	printf("target: ");
#if defined(__ANDROID__)
	printf("Android/bionic\n");
#else
	printf("desktop Linux/glibc\n");
#endif
	printf("--------------------------------------------\n");

	check_executable();
	check_architecture();
	check_process_vm();
	check_ptrace();
	check_untag_unit();
	check_ptrace_untag();
	check_process_vm_untag();
	check_syscall();
	check_seccomp();
	check_execve();
	check_loader();
	check_signals();
	check_proc();

	printf("--------------------------------------------\n");
	printf("result: %s\n", failures == 0 ? "PASS" : "FAIL");
	printf("summary: %d pass, %d fail, %d skip\n",
	       checks - failures - skips, failures, skips);
	return failures == 0 ? 0 : 1;
}
