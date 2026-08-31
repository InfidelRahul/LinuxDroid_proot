#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>

#ifndef SYS_faccessat2
# if defined(__aarch64__)
#  define SYS_faccessat2 439
# elif defined(__x86_64__)
#  define SYS_faccessat2 439
# elif defined(__i386__)
#  define SYS_faccessat2 439
# elif defined(__arm__)
#  define SYS_faccessat2 439
# endif
#endif

int main(void) {
    int ret;

    // 1. Test existing executable (/bin/sh) via faccessat2 syscall
    ret = syscall(SYS_faccessat2, AT_FDCWD, "/bin/sh", X_OK, 0);
    if (ret != 0) {
        fprintf(stderr, "FAIL: faccessat2(/bin/sh, X_OK) returned %d (errno=%d: %s)\n",
                ret, errno, strerror(errno));
        return 1;
    }
    printf("[PASS] faccessat2(/bin/sh, X_OK) == 0\n");

    // 2. Test existing executable (/bin/true) via faccessat2 syscall
    ret = syscall(SYS_faccessat2, AT_FDCWD, "/bin/true", X_OK, 0);
    if (ret != 0) {
        fprintf(stderr, "FAIL: faccessat2(/bin/true, X_OK) returned %d (errno=%d: %s)\n",
                ret, errno, strerror(errno));
        return 1;
    }
    printf("[PASS] faccessat2(/bin/true, X_OK) == 0\n");

    // 3. Test nonexistent file via faccessat2 syscall
    ret = syscall(SYS_faccessat2, AT_FDCWD, "/nonexistent_faccessat2_test_file", F_OK, 0);
    if (ret == 0 || errno != ENOENT) {
        fprintf(stderr, "FAIL: faccessat2(nonexistent) returned %d (expected -1, errno=%d: %s)\n",
                ret, errno, strerror(errno));
        return 2;
    }
    printf("[PASS] faccessat2(nonexistent) == -1 (ENOENT)\n");

    // 4. Test flags AT_SYMLINK_NOFOLLOW
    ret = syscall(SYS_faccessat2, AT_FDCWD, "/bin/sh", R_OK, AT_SYMLINK_NOFOLLOW);
    if (ret != 0 && errno != EINVAL) {
        fprintf(stderr, "FAIL: faccessat2(/bin/sh, AT_SYMLINK_NOFOLLOW) returned %d (errno=%d: %s)\n",
                ret, errno, strerror(errno));
        return 3;
    }
    printf("[PASS] faccessat2(/bin/sh, AT_SYMLINK_NOFOLLOW) handled correctly\n");

    return 0;
}
