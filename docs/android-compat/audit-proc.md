# Audit — Android /proc

PRoot reads `/proc` heavily (to resolve `/proc/self/exe`, build `/proc/<pid>`
views, translate paths, etc.).  Android's `/proc` differs from desktop Linux.

## Differences observed on Android

* `/proc` is mounted by the kernel but apps run in sandboxes; visibility of
  other processes is limited.
* `hidepid`-style restrictions may hide other PIDs (ptrace scope ties in).
* `/proc/self/maps`, `/proc/self/status`, `/proc/self/exe` are the important
  files for PRoot; they must be present and well-formed.
* Mount layout differs (no traditional `/proc/mounts` guarantees; `fstab` and
  `vold` mounts).

## Audit checklist

- [ ] `/proc/self/exe` resolves correctly for the tracee.
- [ ] `/proc/self/status` / `/proc/self/maps` present and parseable.
- [ ] PRoot's `-R` / binding path translation works against Android's mount
      layout.
- [ ] `/proc/<pid>/...` reads used by PRoot (e.g. cwd, exe) succeed on device.
- [ ] Data/app-sandbox paths (`/data/data/<pkg>`, `/sdcard`) bind correctly.

## Decision record

| Field | Value |
|-------|-------|
| Component | filesystem / /proc translation |
| Upstream | assumes standard desktop /proc |
| Termux | adjusts for Android mounts and sandbox paths |
| LinuxDroid | verify /proc primitives via selftest; document mount mapping |
| Reason | Android mount/namespace layout differs from desktop |
| Expected behavior | PRoot path translation works inside Android sandbox |
| Android requirement | app-sandboxed mount namespace |
| Test | `tests/functional/` proc rows; device runs |

## Compatibility contract

`linuxdroid-selftest` checks `/proc/self/status` and `/proc/self/maps`
presence — the minimal gate before launching.
