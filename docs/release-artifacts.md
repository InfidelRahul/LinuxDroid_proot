# Release artifact system (Phase 14)

Once the engine is stable, every release of LinuxDroid-PRoot publishes
standalone binaries that LinuxDroid consumes.  LinuxDroid does **not** compile
against this source; it downloads versioned artifacts.

## Release layout

```
LinuxDroid-PRoot v0.1.0 (GitHub release)
│
├── arm64-v8a/
│   ├── proot
│   ├── loader
│   └── linuxdroid-selftest
│
└── x86_64/
    ├── proot
    ├── loader
    └── linuxdroid-selftest
```

Each directory also carries a `MANIFEST.txt` with the release metadata.

## Manifest content

```
LinuxDroid-PRoot v0.1.0
commit:  abc123
ABI:     arm64-v8a
arch:    aarch64 (ARM64)
cc:      Clang 18.0.2 (Android NDK r27b)
ndk:     r27b
android: 16+ (API 36)
sha256:
  proot:    <hex>
  loader:   <hex>
  selftest: <hex>
built:   2026-08-30T00:00:00Z
```

`make release` assembles this tree for the host; a release workflow
(`.github/workflows/release.yml`) cross-compiles for `arm64-v8a` and `x86_64`
and attaches them to a GitHub release.

## Version / commit / ABI / compiler / NDK / SHA-256 / min Android

Every release records, per artifact:

| field | meaning |
|-------|---------|
| version | semantic version of LinuxDroid-PRoot (e.g. `v0.1.0`) |
| commit  | git commit the binaries were built from |
| ABI     | `arm64-v8a` or `x86_64` |
| compiler| compiler + version used |
| NDK version | NDK used for Android builds |
| SHA-256 | integrity of each artifact |
| min Android | minimum supported Android version (16+) |

## How LinuxDroid consumes it

```
LinuxDroid
   └─ RuntimeAssetsManager
        └─ downloads / bundles the LinuxDroid-PRoot release
             ├── proot
             └── loader
```

The application verifies the SHA-256, checks the min-Android and ABI fields,
then hands the plan to `RuntimeLaunchPlan` and `RuntimeLauncher`.
