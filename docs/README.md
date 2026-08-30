# Documentation index

| doc | covers |
|-----|--------|
| [architecture.md](architecture.md) | module layout, two-repo model, Android boundary |
| [build.md](build.md) | building on desktop + Android NDK cross-compile |
| [android.md](android.md) | running on a device, Android quirks |
| [roadmap.md](roadmap.md) | the phased plan (Phases 0–17) |
| [differential-audit.md](differential-audit.md) | three-way audit vs upstream & Termux |
| [release-artifacts.md](release-artifacts.md) | release artifact system + manifest |
| [version-compatibility.md](version-compatibility.md) | LinuxDroid ↔ engine version contract |
| [android-compat/](android-compat/) | Android compatibility audits (Phase 3) |
| `android-compat/einval-investigation.md` | the previous EINVAL investigation |
| `android-compat/audit-pointer-tagging.md` | ARM64 TBI / UNTAG_ADDRESS() |
| `android-compat/audit-ptrace-memory.md` | ptrace + process_vm + PEEK/POKE |
| `android-compat/audit-seccomp.md` | Android seccomp interaction |
| `android-compat/audit-proc.md` | Android /proc |
| `android-compat/audit-signals.md` | Android signal behavior |
