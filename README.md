# LinuxDroid-PRoot

> Can this PRoot implementation reliably provide a rootless Linux userspace on
> modern Android — particularly **Android 16 / ARM64**?

`LinuxDroid-PRoot` is the native PRoot engine for
[LinuxDroid](https://github.com/InfidelRahul/LinuxDroid_proot). It is a
**separately maintained fork** of [upstream PRoot](https://github.com/proot-me/proot),
kept deliberately independent from the Android application so that:

* the native engine can be developed, tested and released on its own, and
* LinuxDroid simply **consumes versioned release artifacts** (`proot` +
  `loader`) without ever compiling against PRoot internals.

```
┌──────────────────────────────────────────────────────┐
│              LinuxDroid-PRoot repository             │
│                                                      │
│  upstream PRoot reference                            │
│          ↓                                           │
│  LinuxDroid PRoot fork                               │
│          │                                           │
│  ┌───────┴───────────────────────────────────────┐   │
│  │ Native PRoot Engine                           │   │
│  │                                               │   │
│  │ ptrace │ syscall │ tracee │ memory │ execve  │   │
│  │ seccomp │ path │ loader │ signals │ Android  │   │
│  └──────────────────────┬────────────────────────┘   │
│                         │                            │
│                 release artifacts                    │
│                  proot / loader                      │
└─────────────────────────┬────────────────────────────┘
                          │
                          │ versioned release
                          ▼
┌──────────────────────────────────────────────────────┐
│                    LinuxDroid                        │
│                                                      │
│ RuntimeAssetsManager                                 │
│        ↓                                             │
│ downloads / bundles PRoot release                    │
│        ↓                                             │
│ RuntimeLaunchPlan                                    │
│        ↓                                             │
│ RuntimeLauncher                                     │
└──────────────────────────────────────────────────────┘
```

## Repository layout

```
src/                    upstream PRoot source (kept structurally intact)
  cli/                  command-line interface
  execve/               ELF / shebang / loader-handoff handling
  extension/            PRoot extensions (fake_id0, kompat, ...)
  loader/               the userspace ELF loader (first-class artifact)
  path/                 path translation & binding
  ptrace/               ptrace plumbing
  syscall/              syscall emulation + seccomp
  tracee/               tracee model, registers, memory access
test/                   upstream PRoot test-suite (retained, unmodified)
tests/                  LinuxDroid-specific suites (functional, seccomp,
                        process/signal, pty, loader validation)
native/android/         Android compatibility layer (additive; keeps the
                        generic engine untainted)
android/                NDK build support + RuntimeAssets contract
tools/
  selftest/             linuxdroid-selftest diagnostic (compatibility contract)
  android-test/         adb push / run / collect harness
third_party/talloc/     vendored talloc (self-contained builds, no pkg-config)
docs/                   architecture, build, Android, audit documentation
Makefile                top-level build system (Phase 4)
README.md               this file
```

## Design principles

1. **Upstream first.** Before any LinuxDroid modification we establish the exact
   upstream baseline, build it unmodified, run its native tests, cross-compile
   for ARM64, and run it on Android.  Only then do we know *what* needs to
   change — and we can always attribute a defect to:
   * an upstream PRoot problem, or
   * a LinuxDroid modification, or
   * an Android-specific problem.

2. **No source-tree mutation.** All builds happen out-of-tree under `build/`.
   The upstream tree under `src/` is never modified by the build.

3. **Additive Android compatibility.** Android-specific fixes live in
   `native/android/`, not scattered through generic PRoot code.  This keeps the
   engine useful independently of LinuxDroid.

4. **Artifacts, not source, cross the boundary.** LinuxDroid consumes versioned
   `proot` + `loader` releases (see `docs/release-artifacts.md` and
   `docs/version-compatibility.md`).

## Quick start (desktop Linux)

```sh
# build the native engine, the loader, and the self-test
make proot
make loader
make selftest

# binaries
build/host/proot
build/host/loader.bin
build/host/linuxdroid-selftest

# run the compatibility self-test
build/host/linuxdroid-selftest

# run the upstream functional test suite
make test
```

## Quick start (Android ARM64, requires the Android NDK)

```sh
make NDK_ROOT=/path/to/android-ndk android-arm64
# -> build/android/arm64-v8a/proot
# -> build/android/arm64-v8a/loader
```

See `docs/build.md` for detailed instructions and `docs/android.md` for running
on a device via adb.

## Compatibility self-test

`linuxdroid-selftest` is the reliability contract between LinuxDroid and the
engine.  It reports `PASS` / `FAIL` / `SKIP` for the subsystems the engine
depends on:

```
executable / architecture / memory (process_vm) / ptrace / syscall /
seccomp / execve / loader / signals / proc
```

A passing self-test on a device is the gate LinuxDroid uses before launching a
session.  See `tools/selftest/`.

## Supported architectures

| ABI            | Make target        | Status                     |
|----------------|--------------------|----------------------------|
| host x86_64    | `make proot`       | building & verified here   |
| arm64-v8a      | `make android-arm64`  | wired (NDK cross-compile)  |
| x86_64         | `make android-x86_64` | wired (NDK cross-compile)  |

Primary target: **aarch64-linux-android (Android 16+, arm64-v8a)**.

## Phases / roadmap

See `docs/roadmap.md` for the full phased plan (baseline → build → Android
compat → validation → release → integration).

## License

The PRoot engine is GPL v2-or-later (see `COPYING`).  The vendored talloc is
LGPL v3+.  LinuxDroid-PRoot-specific files are GPL v2-or-later.
