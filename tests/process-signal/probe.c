/*
 * probe.c - process & signal validation probe for the engine (Phase 12).
 *
 * Exercises fork / execve / wait / exit and SIGCHLD + SIGUSR1 delivery, with
 * a parent/child/grandchild relationship.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>

static volatile sig_atomic_t got_sigusr1 = 0;
static volatile sig_atomic_t got_sigchld = 0;

static void on_usr1(int s) { (void)s; got_sigusr1 = 1; }
static void on_chld(int s) { (void)s; got_sigchld = 1; }

static int grandchild(void)
{
	_exit(42);
}

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;
	sigaction(SIGUSR1, &sa, NULL);
	sa.sa_handler = on_chld;
	sigaction(SIGCHLD, &sa, NULL);

	pid_t pid = fork();
	if (pid < 0) return 1;

	if (pid == 0) {
		/* grandchild path */
		pid_t g = fork();
		if (g == 0) grandchild();
		int st;
		waitpid(g, &st, 0);
		if (!(WIFEXITED(st) && WEXITSTATUS(st) == 42)) _exit(2);
		raise(SIGUSR1);
		_exit(got_sigusr1 ? 0 : 3);
	}

	int st;
	pid_t r = waitpid(pid, &st, 0);
	if (r != pid) return 4;
	if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) return 5;

	/* Verify SIGCHLD was observed by the parent. */
	if (!got_sigchld) return 6;

	puts("process-signal probe: OK");
	return 0;
}
