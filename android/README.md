# android/ — NDK support & RuntimeAssets contract

This directory is the Android-facing surface of the repository.

* `ndk-env.sh` — exports the NDK toolchain environment for cross-builds.
* `verify-elf.sh` — Phase 5 ELF / architecture / dynamic-dependency checks.
* `RuntimeAssets.md` — the contract LinuxDroid uses to consume releases.

The **source** that implements Android compatibility lives in
`../native/android/`; the release artifact contract LinuxDroid consumes is
documented in `RuntimeAssets.md`.
