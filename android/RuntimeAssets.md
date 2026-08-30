# RuntimeAssets contract (Phase 15)

This is the boundary between LinuxDroid-PRoot and the LinuxDroid application.
LinuxDroid sees **RuntimeAssets**, never PRoot internals, and never compiles
against this source tree.

## What LinuxDroid consumes

A versioned release (see `docs/release-artifacts.md`) containing:

```
arm64-v8a/
  proot
  loader
  linuxdroid-selftest
  MANIFEST.txt
```

## LinuxDroid-side flow

```
RuntimeAssetsManager
   ├─ download / bundle the release for the device ABI
   └─ verify SHA-256 + min-Android + ABI from MANIFEST.txt
          ↓
RuntimeLaunchPlan
   ├─ pick arm64-v8a vs x86_64
   └─ build argv: proot -R <rootfs> -b <data-dir> <cmd>
          ↓
RuntimeLauncher
   └─ exec proot (with loader bundled), attach PTY
```

## The contract surface LinuxDroid depends on

1. A runnable `proot` binary for the device ABI.
2. A `loader` usable by the engine for guest-dynamic executables.
3. A `linuxdroid-selftest` that returns exit 0 only when the device is
   compatible (used as a pre-launch gate).
4. A `MANIFEST.txt` carrying `ABI`, `android` (min version), `commit`,
   `sha256` so the app can validate before launching.

## What LinuxDroid must NOT do

* Compile against `src/` or `native/` source.
* Rely on engine internals or undocumented CLI flags.
