# Version compatibility (Phase 16)

LinuxDroid and LinuxDroid-PRoot evolve at different cadences.  This document
defines the contract between them so that a given LinuxDroid release always
pairs with a compatible engine release.

## Compatibility chain

```
LinuxDroid Runtime
        │
        ▼
PRoot API/CLI contract      (the surface LinuxDroid calls)
        │
        ▼
LinuxDroid-PRoot version    (the artifact that implements it)
```

## The PRoot API/CLI contract

LinuxDroid launches the engine through a *stable CLI surface*.  The contract is
the set of `proot` command-line options and behaviors that LinuxDroid relies on
(e.g. `-R <rootfs>`, binding `-b`, the loader hand-off, `--kernel-release`,
etc.).  This contract is what version compatibility actually means.

## Example pairing

```
LinuxDroid 0.5   requires   LinuxDroid-PRoot >= 0.3
```

A LinuxDroid release declares the minimum engine version it is tested against.
The engine's own version number is bumped when the CLI contract changes.

## Runtime metadata

The engine (and the selftest) expose runtime metadata so LinuxDroid can make a
launch decision:

| field | example |
|-------|---------|
| PRoot version | `v0.3.0` |
| loader version | embedded loader build |
| build commit | `abc123` |
| ABI | `arm64-v8a` |
| features | `process_vm`, `seccomp_filter` |
| min Android | `16+` |

The `linuxdroid-selftest` output and the release `MANIFEST.txt` both carry this
information.

## Decision rule

- If `LinuxDroid-PRoot < required`, LinuxDroid fetches a newer release before
  launching (via `RuntimeAssetsManager`).
- If the CLI contract changed incompatibly, LinuxDroid is pinned to a minimum
  engine version that implements it.

This keeps the two repositories independently releasable.
