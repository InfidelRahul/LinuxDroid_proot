# Roadmap

The LinuxDroid-PRoot repository is built in phases.  Each phase produces a
verifiable increment, and the ordering is deliberate: **establish the upstream
baseline and reproduce Android failures before making any LinuxDroid
modification**, so we always know whether a defect is upstream, a LinuxDroid
change, or Android-specific.

Status legend: `[x]` done, `[ ]` not started, `[~]` in progress.

## Phase 0 — Create the repository

- [x] Create the repository (this repo, `LinuxDroid-PRoot` lineage).
- [x] Import the chosen upstream PRoot baseline (`src/`).
- [x] Preserve upstream history (git history retained; `src/` is a faithful
      copy; `test/` upstream suite kept unmodified).
- [x] Establish LinuxDroid branch (`master` of the fork carries upstream +
      LinuxDroid work).
- [x] Add project README (`README.md`).
- [x] Add architecture documentation (`docs/architecture.md`).
- [x] Add build instructions (`docs/build.md`).
- [x] Add Android-specific documentation (`docs/android.md`).
- [x] Add CI foundation (`.github/workflows/ci.yml`).
- [x] Define supported architectures (host x86_64, arm64-v8a, x86_64).

Target tree:

```
LinuxDroid-PRoot/
├── src/            (upstream, retained)
├── loader/         (upstream, under src/loader; also exported standalone)
├── extensions/     (upstream, under src/extension)
├── tests/          (LinuxDroid-specific suites)
├── tools/          (selftest, android-test)
├── android/        (NDK support, RuntimeAssets contract)
├── docs/
├── native/android/ (Android compatibility layer)
├── Makefile
└── README.md
```

## Phase 1 — Establish upstream baseline

- [x] Identify exact upstream commit (`704a4ab` baseline in `src/`).
- [x] Build unmodified upstream PRoot on desktop Linux (verified).
- [x] Record compiler/toolchain (reported in release `MANIFEST.txt`).
- [ ] Build ARM64 (via NDK cross-compile; `make android-arm64`).
- [ ] Run on Android and record failures.
- [ ] Push baseline commit (after this repo is pushed).
- [ ] Document upstream version (this README + `docs/`).
- [x] Run existing upstream tests (`make test` runs `test/`).
- [ ] Record failures on Android.

> The key discipline: reproduce `upstream → build → Android execution →
> failure` before modifying, so we know precisely what LinuxDroid needs to
> change.

## Phase 2 — Define LinuxDroid PRoot architecture

- [x] Explicit modules documented (`docs/architecture.md`).
- [x] New `native/android/` boundary for Android compatibility.

## Phase 3 — Android compatibility layer

- [ ] Audit ARM64 pointer tagging (`docs/android-compat/audit-pointer-tagging.md`).
- [ ] Audit `UNTAG_ADDRESS()` (same file).
- [ ] Audit all ptrace addresses (`audit-ptrace-memory.md`).
- [ ] Audit memory reads / writes (`audit-ptrace-memory.md`).
- [ ] Audit `process_vm_readv` / `process_vm_writev` (same).
- [ ] Audit `PTRACE_PEEKDATA` / `PTRACE_POKEDATA` (same).
- [ ] Audit Android `/proc` (`audit-proc.md`).
- [ ] Audit Android signal behavior (`audit-signals.md`).
- [ ] Audit Android seccomp (`audit-seccomp.md`).
- [ ] Document every Android-specific workaround.

> This is where the previous **EINVAL** investigation belongs
> (`docs/android-compat/einval-investigation.md`).

## Phase 4 — Build system

- [x] `make proot`
- [x] `make loader`
- [x] `make android-arm64` / `make android-x86_64`
- [x] `make test`
- [x] `./build/android/arm64-v8a/proot` layout

## Phase 5 — Android ARM64 build

- [x] Establish NDK build (`Makefile`, CI).
- [x] Build PRoot / loader (cross-compile wired).
- [x] Produce standalone executable.
- [ ] Verify ELF / architecture / dynamic deps (scripted in CI).
- [ ] Push artifacts to CI.
- [ ] Test directly with adb.

> Do not package into LinuxDroid yet.  First make the standalone binary work.

## Phase 6 — Standalone Android test harness

- [x] `tools/android-test/` harness (push → run → collect).
- [ ] Automated device tests in CI.

## Phase 7 — PRoot self-test

- [x] `linuxdroid-selftest` diagnostic (`tools/selftest/`).
- [x] Covers: executable, architecture, ptrace, memory, syscall, seccomp,
      execve, loader, signals, proc.
- [x] Reports PASS/FAIL/SKIP; exit status usable as a gate.

> A separate tool, not a hidden CLI switch, so upstream CLI semantics stay
> untouched.

## Phase 8 — Functional Linux tests

- [x] `tests/functional/` harness (run `/bin/true`, `false`, `echo`, `sh`).
- [ ] fork / exec / wait / signals / pipes / files / dirs / symlinks.
- [ ] `/proc`, `/dev`.
- [ ] Dynamic binaries (hand-off to guest interpreter + libraries).

## Phase 9 — Differential testing

- [x] Framework documented (`docs/differential-audit.md`).
- [ ] Compare: LinuxDroid-PRoot vs upstream PRoot vs Termux PRoot.
- [ ] Per-subsystem record: Difference / Reason / Expected behavior /
      Android requirement / Decision / Test.

## Phase 10 — Loader validation

- [x] Loader treated as first-class artifact (exported + in release).
- [ ] Test suite: static ELF, dynamic ELF, ELF interpreter, guest libraries,
      missing library, incorrect architecture (`tests/loader/`).
- [ ] Validate pipeline: PRoot → guest execve → ELF → guest interpreter →
      guest libraries → program.

## Phase 11 — Seccomp validation

- [ ] Dedicated `tests/seccomp/` suite:
      normal syscall, trapped syscall, SIGSYS, emulation, ENOSYS,
      architecture detection, fallback.

## Phase 12 — Process / signal testing

- [ ] `tests/process-signal/`: fork, clone, execve, wait, exit,
      SIGCHLD / SIGTERM / SIGINT / SIGSYS.

## Phase 13 — PTY testing

- [ ] `tests/pty/`: stdin/stdout/stderr, terminal resize, signals,
      interactive shell.
- [ ] Note: PRoot does not own the Android UI; LinuxDroid's Kotlin layer owns
      PTY lifecycle later.

## Phase 14 — Release artifact system

- [x] Release assembly target (`make release`, `docs/release-artifacts.md`).
- [x] Artifacts: arm64-v8a `proot` + `loader`; x86_64 `proot` + `loader`.
- [x] Each release carries version, commit, architecture, compiler, NDK
      version, SHA-256, minimum Android version.

## Phase 15 — LinuxDroid integration contract

- [x] Contract documented (`android/` RuntimeAssets + `docs/version-compatibility.md`).
- [ ] LinuxDroid consumes releases (not this source).

## Phase 16 — Version compatibility

- [x] Contract documented (`docs/version-compatibility.md`).
- [ ] Runtime metadata: PRoot version, loader version, build commit, ABI,
      features.

## Phase 17 — CI

- [x] CI foundation (`.github/workflows/ci.yml`).
- [x] Desktop build + selftest.
- [ ] ARM64 cross-compile.
- [ ] Native unit tests.
- [ ] Static analysis.
- [ ] Android device tests (long-term, most valuable).

## Development order (the plan)

```
STEP 1  Create LinuxDroid-PRoot
STEP 2  Import upstream baseline
STEP 3  Build unmodified PRoot
STEP 4  Build ARM64 Android binary
STEP 5  Create Android test harness
STEP 6  Fix/implement Android compatibility
STEP 7  Validate ptrace + memory
STEP 8  Validate seccomp
STEP 9  Validate loader + execve
STEP 10 Validate process/signals
STEP 11 Validate PTY
STEP 12 Differential audit against Termux/upstream
STEP 13 Create release artifacts
STEP 14 Integrate release into LinuxDroid
STEP 15 Continue LinuxDroid runtime migration
```

## The one decision that guides everything

> Do **not** start modifying the PRoot fork immediately.  First establish
> `exact upstream baseline → build → Android execution → failure reproduction`.
> Then we know precisely what LinuxDroid needs to change — and the Android
> engine, once stable, is useful independently of LinuxDroid (LinuxDroid is
> simply its first consumer).
