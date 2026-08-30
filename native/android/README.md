# native/android — Android compatibility layer

This is the one place where Android-specific PRoot behavior lives.  It is
**additive** by design: the generic engine under `src/` stays untainted, and
the LinuxDroid build selects these helpers with `-DLINUXDROID_ANDROID`.

```
AndroidCompat
│
├── untag.h             UNTAG_ADDRESS() - ARM64 TBI untagging choke point
├── android_compat.h    feature probe + path helpers (public surface)
├── android_compat.c    implementation
└── README.md           this file
```

## What belongs here

Anything that differs on Android and must be applied at the engine boundary:

* pointer tagging / `UNTAG_ADDRESS()` (ARM64 TBI)
* ptrace & memory accessor selection (`process_vm_*` vs `PTRACE_PEEK/POKE`)
* seccomp interop with Android's global policy
* path defaults (`/system/bin/sh`, `linker64`)
* /proc and mount-namespace handling

See `docs/android-compat/` for the full audit of each subsystem.

## Build

The helpers are compiled into the engine only for Android builds:

```sh
make NDK_ROOT=/path/to/ndk android-arm64
```

They are also built into `linuxdroid-selftest`, which is the runtime gate.
