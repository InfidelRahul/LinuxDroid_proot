# LinuxDroid-PRoot architecture

This document describes how the LinuxDroid PRoot fork is structured and how it
relates to both upstream PRoot and the LinuxDroid Android application.

## The one-question test

The repository answers a single question:

> Can this PRoot implementation reliably provide a rootless Linux userspace on
> modern Android, particularly **Android 16 / ARM64**?

Every structural decision below serves that question.  The Android application
is a *consumer* of this repository, never a part of it.

## Two-repository model

```
GitHub
          │
  ┌───────┴───────────┐
  │                   │
  ▼                   ▼
 ┌──────────────────┐    ┌──────────────────┐
 │ LinuxDroid-PRoot │    │    LinuxDroid    │
 │                  │    │                  │
 │ Native engine    │    │ Android app      │
 │ ptrace           │    │ Runtime          │
 │ syscall          │    │ Rootfs           │
 │ seccomp          │    │ Distribution     │
 │ loader           │    │ PTY              │
 │ Android compat   │    │ Wayland          │
 │                  │    │ Desktop          │
 └────────┬─────────┘    └────────┬─────────┘
          │                       │
          │ release               │
          └───────────►───────────┘
                    artifacts
```

The boundary is **versioned release artifacts** (`proot` + `loader`), not
source code.  LinuxDroid sees `RuntimeAssets`, never PRoot internals.

## Engine module boundaries

PRoot's existing source is grouped under `src/`.  We retain upstream's grouping
(no reorganization purely for aesthetics), but we document the logical modules:

```
PRoot
│
├── Core                 src/cli, src/tracee (tracee model, event loop)
├── Tracee               src/tracee (tracee.c, event.c)
├── ptrace               src/ptrace (ptrace.c, wait.c, user.c)
├── Memory               src/tracee/mem.c  (memory read/write primitives)
├── Syscall              src/syscall (syscall.c, enter.c, exit.c, chain.c,
│                                   sysnum.c, socket.c, heap.c, rlimit.c)
├── Path Translation     src/path (binding.c, glue.c, canon.c, path.c,
│                              proc.c, temp.c)
├── Execve               src/execve (enter.c, exit.c, shebang.c, elf.c,
│                                   ldso.c, auxv.c, aoxp.c)
├── Loader               src/loader (loader.c + per-arch assembly)
├── Signals              src/tracee/event.c (signal forwarding),
│                                   handled in syscall/ptrace
├── Seccomp              src/syscall/seccomp.c
├── Extensions           src/extension (fake_id0, kompat, link2symlink,
│                                   portmap, care, python)
└── Android Compatibility native/android (see below)
```

The **new, important boundary** is `native/android/`.  It contains
Android-specific compatibility code *additively*, rather than contaminating
generic PRoot wherever possible.

## Android compatibility layer

```
AndroidCompat  (native/android/)
│
├── architecture        pointer tagging, UNTAG_ADDRESS(), ABI detection
├── ptrace              ptrace address handling, PTRACE_PEEK/POKEDATA
├── memory              process_vm_readv / process_vm_writev paths
├── process             Android process / /proc quirks
├── signals             Android signal behavior (incl. SIGSYS, SIGSEGV tags)
├── seccomp             seccomp + Android's seccomp policy interaction
├── filesystem          /proc, /dev, sdcard / app data dirs
└── execution           execve / dynamic linker (linker64) on Android
```

Every Android-specific workaround is documented under `docs/android-compat/`
with its *reason*, *expected behavior* and *Android requirement*.  This turns
one-off hacks into an engineering history.

## Build model

All builds are **out-of-tree** under `build/`.  Each ABI (`host`, `arm64-v8a`,
`x86_64`) gets its own directory:

```
build/
  talloc/
    host/       vendored talloc for the desktop build
    arm64-v8a/  vendored talloc cross-compiled with the NDK
    x86_64/
  host/                 copied upstream tree + built proot / loader / selftest
  arm64-v8a/            copied upstream tree, cross-compiled
  x86_64/
  android/              release-style layout of the Android binaries
  release/              assembled release artifacts + MANIFEST.txt
```

`src/` is copied (not symlinked) into each build dir so the upstream tree is
never modified and multiple ABIs can be built and cleaned independently.

## Release pipeline (engine side)

```
upstream PRoot
   → LinuxDroid PRoot fork
   → build proot + loader for arm64-v8a / x86_64
   → assemble build/release with MANIFEST.txt (version, commit, ABI,
     compiler, NDK version, SHA-256, minimum Android version)
   → GitHub release (see .github/workflows/release.yml)
```

LinuxDroid then:
```
RuntimeAssetsManager → downloads/bundles the release
RuntimeLaunchPlan    → picks the ABI + version
RuntimeLauncher      → runs proot with the loader
```
