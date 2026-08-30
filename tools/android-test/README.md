# tools/android-test — standalone device harness (Phase 6)

Automates `adb push proot → chmod → run → collect` so the engine can be
validated on a real device before any LinuxDroid integration.

## Usage

```sh
make NDK_ROOT=/path/to/ndk android-arm64      # build the artifacts first
tools/android-test/adb-test.sh arm64-v8a      # run against the attached device
```

With a specific device:

```sh
tools/android-test/adb-test.sh arm64-v8a <adb-serial>
```

## What it does

1. Pushes `proot`, `loader`, `linuxdroid-selftest` to `/data/local/tmp`.
2. `chmod 755` each.
3. Runs `linuxdroid-selftest` (the compatibility gate) and captures output.
4. Runs a `proot --version` smoke test.

## Collecting results

The harness prints the selftest `result: PASS/FAIL` line.  For CI, the exit
status of the selftest can be propagated so a device test failure fails the
job.  See `.github/workflows/ci.yml` for the intended device-test job.
