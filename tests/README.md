# tests/ — LinuxDroid-specific test suites

These are the LinuxDroid test suites, distinct from the upstream `test/`
suite (which is retained unmodified).  Each suite is a self-contained harness
that runs the built engine (`build/host/proot`) against a guest program.

| suite | focus | phase |
|-------|-------|-------|
| `functional/` | `/bin/true`, `false`, `echo`, `sh`; then fork/exec/wait, pipes, files, symlinks, /proc, /dev | 8 |
| `seccomp/`    | normal/trapped syscalls, SIGSYS, emulation, ENOSYS, fallback | 11 |
| `process-signal/` | fork, clone, execve, wait, exit, SIGCHLD/SIGTERM/SIGINT/SIGSYS | 12 |
| `pty/`        | stdin/stdout/stderr, resize, signals, interactive shell | 13 |
| `loader/`     | static/dynamic ELF, interpreter, guest libs, missing lib, wrong arch | 10 |

Run a suite:

```sh
make proot                                  # build the engine first
tests/functional/run.sh                     # run one suite
```

Each suite exits 0 on success, non-zero on failure.
