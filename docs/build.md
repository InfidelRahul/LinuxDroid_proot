# Build instructions

All builds are out-of-tree under `build/`.  The upstream PRoot tree under
`src/` is copied into each build directory and is **never modified**.

## Requirements

* `make`, a C compiler (`gcc`), `ar`, `strip`, `objcopy`, `objdump`
* A POSIX shell
* No `pkg-config`, no system `libtalloc` — talloc is vendored under
  `third_party/talloc/`
* For Android cross-compilation: the **Android NDK** (see below)

> The vendored talloc is what upstream normally pulls in via `pkg-config`.
> Vendoring it makes both desktop and NDK builds self-contained and removes
> `libtalloc-dev` as a build dependency.

## Desktop Linux

```sh
# Native engine
make proot                 # -> build/host/proot

# Userspace loader (first-class artifact)
make loader                # -> build/host/loader.bin

# Compatibility self-test diagnostic
make selftest              # -> build/host/linuxdroid-selftest

# Run the upstream functional test-suite
make test

# Assemble a release-style artifact tree
make release               # -> build/release/{proot,loader,MANIFEST.txt}
```

### Verification

```sh
build/host/proot --version
build/host/linuxdroid-selftest      # expect: result: PASS
```

## Android ARM64 (arm64-v8a)

The primary target is `aarch64-linux-android`.

### 1. Install the Android NDK

Download the NDK (Android Studio → SDK Manager → NDK, or the command line):

```sh
curl -s https://dl.google.com/android/repository/android-ndk-r27b-linux.zip \
  -o /tmp/android-ndk.zip
unzip /tmp/android-ndk.zip -d /opt
export NDK_ROOT=/opt/android-ndk-r27b
```

CI installs a pinned NDK automatically — see `.github/workflows/ci.yml`.

### 2. Cross-compile

```sh
make NDK_ROOT=$NDK_ROOT android-arm64
# -> build/android/arm64-v8a/proot
# -> build/android/arm64-v8a/loader
```

For x86_64 Android emulator images:

```sh
make NDK_ROOT=$NDK_ROOT android-x86_64
# -> build/android/x86_64/proot
```

### 3. Verify the ELF

```sh
${NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf -h \
  build/android/arm64-v8a/proot | grep -E "Machine|Class|Type"

file build/android/arm64-v8a/proot
# expected: ELF 64-bit LSB executable, ARM aarch64 ... statically/dynamically linked
```

* **Machine:** `AArch64` (ARM64) — correct architecture
* **Type:** `DYN` / `EXEC` with `-pie` — position-independent executable
  (required by Android since API 21)
* Dynamic dependencies (if any) must resolve against bionic, not glibc.

### 4. How the build works

For each ABI the Makefile:

1. Builds a cross-compiled vendored talloc under `build/talloc/<abi>/`.
2. Copies `src/` and `lib/uthash` into `build/<abi>/`.
3. Drives upstream's `GNUmakefile` in that directory with:

   ```
   CC       = $NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang
   STRIP    = .../llvm-strip
   OBJCOPY  = .../llvm-objcopy
   OBJDUMP  = .../llvm-objdump
   CFLAGS  += -fPIE -fPIC
   LDFLAGS += -pie
   ```

The upstream `GNUmakefile` already compiles the userspace loader
(`src/loader/`) and wraps it into `proot`; we additionally export it as a
standalone artifact.

## Android self-test

The selftest is built for the target as part of the Android build.  On the
device it is the compatibility gate:

```sh
adb push build/android/arm64-v8a/proot /data/local/tmp/
adb push build/android/arm64-v8a/linuxdroid-selftest /data/local/tmp/
adb shell chmod 755 /data/local/tmp/proot /data/local/tmp/linuxdroid-selftest
adb shell /data/local/tmp/linuxdroid-selftest
# expect: result: PASS
```

See `tools/android-test/` for an automated harness.

## Build targets reference

| Target            | Description                                       |
|-------------------|---------------------------------------------------|
| `make proot`      | desktop engine (`build/host/proot`)               |
| `make loader`     | export userspace loader (`build/host/loader.bin`) |
| `make selftest`   | desktop self-test (`build/host/linuxdroid-selftest`) |
| `make test`       | upstream functional test suite                    |
| `make android-arm64` | NDK cross-compile → `build/android/arm64-v8a/` |
| `make android-x86_64`| NDK cross-compile → `build/android/x86_64/`   |
| `make release`    | assemble `build/release/` + `MANIFEST.txt`        |
| `make clean`      | remove `build/`                                    |

## Toolchain recording

For every release we record (in the release `MANIFEST.txt`):

* compiler / `--version`
* NDK version (for Android builds)
* target ABI / architecture
* `git rev-parse HEAD`
* SHA-256 of each artifact
* minimum Android version

This satisfies Phase 1's requirement to *record compiler/toolchain* so that
later defect attribution (upstream vs. LinuxDroid vs. Android) is possible.
