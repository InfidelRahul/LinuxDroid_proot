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

#include <errno.h>  /* errno, */
#include <string.h> /* strerror(3), */
#include <stdarg.h> /* va_*, */
#include <stdio.h>  /* vfprintf(3), */
#include <stdlib.h> /* getenv(3), */
#include <stdbool.h>/* bool, */
#include <limits.h> /* INT_MAX, */

#include "cli/note.h"
#include "tracee/tracee.h"

int global_verbose_level;
const char *global_tool_name;

static FILE *get_log_stream(void)
{
	static FILE *log_file = NULL;
	static bool initialized = false;

	if (!initialized || (log_file == NULL)) {
		const char *log_path = getenv("PROOT_LOG_FILE");
		if (log_path != NULL && log_path[0] != '\0') {
			log_file = fopen(log_path, "a");
			if (log_file != NULL) {
				setvbuf(log_file, NULL, _IOLBF, 0);
				initialized = true;
			}
		} else {
			initialized = true;
		}
	}

	return log_file ?: stderr;
}

/**
 * Print @message to the log stream according to its
 * @severity and @origin. If PROOT_LOG_FILE is set,
 * diagnostics are written to that file; otherwise to stderr.
 */
void note(const Tracee *tracee, Severity severity, Origin origin, const char *message, ...)
{
	const char *tool_name;
	va_list extra_params;
	int verbose_level;
	int saved_errno = errno;

	if (tracee == NULL) {
		verbose_level = global_verbose_level;
		tool_name     = global_tool_name ?: "";
	}
	else {
		verbose_level = tracee->verbose;
		tool_name     = tracee->tool_name;
	}

	if (verbose_level < 0 && severity != ERROR)
		return;

	FILE *out = get_log_stream();

	switch (severity) {
	case WARNING:
		fprintf(out, "%s warning: ", tool_name);
		break;

	case ERROR:
		fprintf(out, "%s error: ", tool_name);
		break;

	case INFO:
	default:
		fprintf(out, "%s info: ", tool_name);
		break;
	}

	if (origin == TALLOC)
		fprintf(out, "talloc: ");

	va_start(extra_params, message);
	vfprintf(out, message, extra_params);
	va_end(extra_params);

	switch (origin) {
	case SYSTEM:
		fprintf(out, ": %s\n", strerror(saved_errno));
		break;

	case TALLOC:
		break;

	case INTERNAL:
	case USER:
	default:
		fprintf(out, "\n");
		break;
	}

	fflush(out);
	return;
}


