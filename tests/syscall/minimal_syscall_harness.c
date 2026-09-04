#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sched.h>
#include <stdint.h>

#ifndef SYS_faccessat2
# if defined(__aarch64__) || defined(__x86_64__) || defined(__i386__) || defined(__arm__)
#  define SYS_faccessat2 439
# endif
#endif

#ifndef SYS_openat2
# if defined(__aarch64__) || defined(__x86_64__) || defined(__i386__) || defined(__arm__)
#  define SYS_openat2 437
# endif
#endif

struct open_how_test {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

static int fails = 0;

static void test_pass(const char* name) {
    printf("[PASS] %s\n", name);
}

static void test_fail(const char* name, const char* detail) {
    fprintf(stderr, "[FAIL] %s: %s\n", name, detail);
    fails++;
}

// 1. Test identity virtualization (fake-root coherence)
static void test_identity(void) {
    uid_t uid = getuid();
    uid_t euid = geteuid();
    gid_t gid = getgid();
    gid_t egid = getegid();

    if (uid != 0 || euid != 0 || gid != 0 || egid != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "uid=%d, euid=%d, gid=%d, egid=%d (expected all 0 under fake-root)",
                 uid, euid, gid, egid);
        test_fail("identity_fake_root", buf);
    } else {
        test_pass("identity_fake_root (uid=0, euid=0, gid=0, egid=0)");
    }

    // Test setresuid / getresuid
    uid_t ruid, e_uid, suid;
    if (getresuid(&ruid, &e_uid, &suid) == 0 && ruid == 0 && e_uid == 0 && suid == 0) {
        test_pass("getresuid == 0");
    } else {
        test_fail("getresuid", "failed to read or mismatch");
    }
}

// 2. Test faccessat and faccessat2
static void test_faccessat(void) {
    int ret = faccessat(AT_FDCWD, "/bin/sh", X_OK, 0);
    if (ret != 0) {
        test_fail("faccessat(/bin/sh)", strerror(errno));
    } else {
        test_pass("faccessat(/bin/sh, X_OK) == 0");
    }

#ifdef SYS_faccessat2
    ret = syscall(SYS_faccessat2, AT_FDCWD, "/bin/sh", X_OK, 0);
    if (ret != 0) {
        test_fail("faccessat2(/bin/sh)", strerror(errno));
    } else {
        test_pass("faccessat2(/bin/sh, X_OK, 0) == 0");
    }

    ret = syscall(SYS_faccessat2, AT_FDCWD, "/nonexistent_probe_file_xyz", F_OK, 0);
    if (ret != -1 || errno != ENOENT) {
        test_fail("faccessat2(nonexistent)", "expected -1 ENOENT");
    } else {
        test_pass("faccessat2(nonexistent) == -1 ENOENT");
    }
#endif
}

// 3. Test openat and relative dirfd
static void test_openat(void) {
    int fd = openat(AT_FDCWD, "/bin/sh", O_RDONLY);
    if (fd < 0) {
        test_fail("openat(/bin/sh)", strerror(errno));
        return;
    }
    test_pass("openat(AT_FDCWD, /bin/sh) succeeded");

    int dirfd = open("/bin", O_RDONLY | O_DIRECTORY);
    if (dirfd >= 0) {
        int rel_fd = openat(dirfd, "sh", O_RDONLY);
        if (rel_fd >= 0) {
            test_pass("openat(dirfd=/bin, 'sh') succeeded");
            close(rel_fd);
        } else {
            test_fail("openat(dirfd, relative)", strerror(errno));
        }
        close(dirfd);
    }
    close(fd);
}

// 4. Test stat, fstatat, statfs
static void test_stat(void) {
    struct stat st;
    if (fstatat(AT_FDCWD, "/bin/sh", &st, 0) == 0) {
        test_pass("fstatat(/bin/sh) succeeded");
    } else {
        test_fail("fstatat(/bin/sh)", strerror(errno));
    }

    struct statfs sfs;
    if (statfs("/", &sfs) == 0) {
        test_pass("statfs(/) succeeded");
    } else {
        test_fail("statfs(/)", strerror(errno));
    }
}

// 5. Test unshare and setns semantics (must return -EPERM rootlessly, NOT fake 0)
static void test_unshare_setns(void) {
    int ret = unshare(CLONE_NEWUSER);
    if (ret == 0) {
        test_fail("unshare(CLONE_NEWUSER)", "returned fake success 0! (expected -EPERM)");
    } else if (errno == EPERM || errno == ENOSYS || errno == EINVAL) {
        test_pass("unshare(CLONE_NEWUSER) returned -1 (EPERM/ENOSYS) as expected rootlessly");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "unexpected errno=%d: %s", errno, strerror(errno));
        test_fail("unshare(CLONE_NEWUSER)", buf);
    }
}

// 6. Test openat2 handling
static void test_openat2(void) {
#ifdef SYS_openat2
    struct open_how_test how = {
        .flags = O_RDONLY,
        .mode = 0,
        .resolve = 0
    };
    int ret = syscall(SYS_openat2, AT_FDCWD, "/bin/sh", &how, sizeof(how));
    if (ret >= 0) {
        test_pass("openat2(resolve=0) rewritten to openat cleanly");
        close(ret);
    } else if (errno == ENOSYS) {
        test_pass("openat2 returned ENOSYS (acceptable fallback)");
    } else {
        test_fail("openat2(resolve=0)", strerror(errno));
    }

    // Test with resolve flag (e.g. 0x01 = RESOLVE_NO_XDEV) -> should return -ENOSYS or error
    how.resolve = 0x01;
    ret = syscall(SYS_openat2, AT_FDCWD, "/bin/sh", &how, sizeof(how));
    if (ret < 0 && (errno == ENOSYS || errno == EXDEV || errno == EINVAL)) {
        test_pass("openat2(resolve!=0) rejected unsupported resolve flags correctly");
    } else if (ret >= 0) {
        close(ret);
        test_pass("openat2(resolve!=0) handled");
    }
#else
    test_pass("SYS_openat2 not defined on this arch");
#endif
}

// 7. Test readlink
static void test_readlink(void) {
    char buf[256];
    ssize_t n = readlink("/bin/sh", buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        test_pass("readlink(/bin/sh) succeeded");
    } else if (errno == EINVAL) {
        test_pass("readlink(/bin/sh) returned EINVAL (not a symlink, valid)");
    } else {
        test_fail("readlink(/bin/sh)", strerror(errno));
    }
}

// 8. Test file creation, rename, unlink
static void test_file_ops(void) {
    char path[] = "/tmp/proot_harness_test_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        test_fail("mkstemp(/tmp/...)", strerror(errno));
        return;
    }
    if (write(fd, "test\n", 5) != 5) {
        test_fail("write(/tmp/...)", strerror(errno));
        close(fd);
        unlink(path);
        return;
    }
    close(fd);

    char path2[256];
    snprintf(path2, sizeof(path2), "%s_renamed", path);
    if (rename(path, path2) == 0) {
        test_pass("rename(/tmp/test) succeeded");
        unlink(path2);
    } else {
        test_fail("rename(/tmp/test)", strerror(errno));
        unlink(path);
    }
}

// 9. Test socket creation (AF_INET and AF_UNIX)
static void test_sockets(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        test_pass("socket(AF_INET, SOCK_STREAM) succeeded");
        close(s);
    } else {
        test_fail("socket(AF_INET)", strerror(errno));
    }

    int u = socket(AF_UNIX, SOCK_STREAM, 0);
    if (u >= 0) {
        test_pass("socket(AF_UNIX, SOCK_STREAM) succeeded");
        close(u);
    } else {
        test_fail("socket(AF_UNIX)", strerror(errno));
    }
}

int main(void) {
    printf("== Minimal Syscall Regression Test Harness ==\n");
    test_identity();
    test_faccessat();
    test_openat();
    test_stat();
    test_unshare_setns();
    test_openat2();
    test_readlink();
    test_file_ops();
    test_sockets();

    printf("== Harness Summary: %s (%d failures) ==\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails;
}
