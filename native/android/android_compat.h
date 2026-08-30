/*
 * android_compat.h - Android compatibility layer (additive).
 *
 * The boundary between generic PRoot and Android-specific behavior.  Code in
 * this header is *additive*: it never changes generic PRoot paths; it only
 * provides Android-aware helpers that LinuxDroid's engine build selects via
 * -DLINUXDROID_ANDROID.
 *
 * See docs/architecture.md and docs/android.md.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef LINUXDROID_ANDROID_COMPAT_H
#define LINUXDROID_ANDROID_COMPAT_H

#include "untag.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runtime features the engine cares about on the target.  This is the same
 * surface linuxdroid-selftest reports, so a build can gate on it.
 */
struct linuxdroid_features {
	unsigned has_process_vm;   /* process_vm_readv / writev available   */
	unsigned has_seccomp_filter; /* BPF filter installable              */
	unsigned is_android;        /* running under bionic/Android         */
	unsigned is_aarch64;        /* target architecture is ARM64         */
};

/* Probe the target runtime and fill the struct. */
void linuxdroid_probe_features(struct linuxdroid_features *out);

/* Android executable paths (bionic uses linker64, no /bin/sh). */
const char *linuxdroid_default_shell(void);
const char *linuxdroid_dynamic_linker(void);

#ifdef __cplusplus
}
#endif

#endif /* LINUXDROID_ANDROID_COMPAT_H */
