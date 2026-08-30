#!/usr/bin/env bash
# tests/memory/run.sh - tracee memory-access regression suite.
#
# Covers the engine's kernel-facing memory boundary
# (src/tracee/mem.c: read_data / write_data / read_string / peek_word /
# poke_word over process_vm_* and PTRACE_PEEKDATA / POKEDATA) and the
# ARM64 tagged-address normalization (native/android/untag.h):
#
#   1. builds the guest probe (guest-mem.c) and runs it under the host
#      engine  -> exercises the process_vm_* fast path (HAVE_PROCESS_VM).
#   2. rebuilds a ptrace-only engine copy with HAVE_PROCESS_VM disabled
#      -> forces the PEEKDATA/POKEDATA fallback paths, and re-runs the
#      same probe under it.
#
# The tagged-pointer check is meaningful on every architecture: on
# aarch64 the engine must normalize and succeed; elsewhere normalization
# is the identity and the invalid pointer must be rejected (upstream
# behavior preserved).  See docs/android-compat/audit-pointer-tagging.md.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROOT="$ROOT/build/host/proot"
GUEST_SRC="$ROOT/tests/memory/guest-mem.c"
GUEST="$ROOT/build/host/guest-mem"
PTREE="$ROOT/build/host-ptrace-only"
FAILS=0

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; FAILS=$((FAILS+1)); }

[[ -x "$PROOT" ]] || { echo "engine not built: run 'make proot' first"; exit 1; }

# ---- build the guest probe -------------------------------------------------
mkdir -p "$ROOT/build/host"
if ! cc -O2 -Wall -Wextra -D_GNU_SOURCE -o "$GUEST" "$GUEST_SRC" 2>/dev/null; then
	if ! gcc -O2 -Wall -Wextra -D_GNU_SOURCE -o "$GUEST" "$GUEST_SRC"; then
		echo "[FAIL] cannot compile guest probe"; exit 1
	fi
fi

run_probe() { # engine, label
	local engine="$1" label="$2"
	local out
	out="$("$engine" -R / "$GUEST" 2>&1)"
	local rc=$?
	echo "$out" | sed "s/^/    [$label] /"
	if [[ $rc -eq 0 && "$out" == *"[FAIL]"* ]]; then rc=1; fi
	if [[ $rc -eq 0 ]]; then pass "memory probe under $label"; else fail "memory probe under $label (rc=$rc)"; fi
}

echo "== memory: guest probe under host engine (process_vm_* fast path) =="
run_probe "$PROOT" "process_vm"

# ---- ptrace-only engine (fallback path coverage) ---------------------------
#
# Rebuild the host tree with HAVE_PROCESS_VM disabled so that every
# read_data/write_data/read_string/peek_word/poke_word call goes through
# PTRACE_PEEKDATA / PTRACE_POKEDATA.  This is a *test-only* variant: the
# feature probes are pinned (empty .res = feature absent) so the
# auto-configuration cannot re-enable process_vm.
echo "== memory: building ptrace-only engine variant (fallback coverage) =="
if [[ ! -x "$PTREE/proot" ]]; then
	rm -rf "$PTREE"
	cp -r "$ROOT/build/host" "$PTREE"
	find "$PTREE" -name '*.o' -delete
	rm -f "$PTREE/proot" "$PTREE/proot.host" "$PTREE/build.h" \
	      "$PTREE/.check_process_vm.c" "$PTREE/.check_process_vm.d" \
	      "$PTREE/.check_seccomp_filter.c" "$PTREE/.check_seccomp_filter.d"
	# Pin the feature-check results: process_vm absent, seccomp kept.
	: > "$PTREE/.check_process_vm"
	touch -d '2000-01-01 00:00:00' "$PTREE/.check_process_vm"
	echo "" > "$PTREE/.check_process_vm.res"
	: > "$PTREE/.check_seccomp_filter"
	touch -d '2000-01-01 00:00:00' "$PTREE/.check_seccomp_filter"
	echo "#define HAVE_SECCOMP_FILTER" > "$PTREE/.check_seccomp_filter.res"
	if ! make -C "$PTREE" -f GNUmakefile \
		CC=gcc STRIP=strip OBJCOPY=objcopy OBJDUMP=objdump \
		CFLAGS="-g -O2 -Wall -Wextra -I$ROOT/third_party/talloc -I$ROOT/native/android" \
		LDFLAGS="-Wl,-z,noexecstack $ROOT/build/talloc/host/libtalloc.a" \
		-j"$(nproc)" proot >"$PTREE/build.log" 2>&1; then
		fail "ptrace-only engine build"
		tail -5 "$PTREE/build.log" >&2
	else
		pass "ptrace-only engine build"
	fi
fi

if [[ -x "$PTREE/proot" ]]; then
	# Sanity: the variant must really lack process_vm (auto-gen header
	# and dynamic relocations).
	if grep -q 'HAVE_PROCESS_VM' "$PTREE/build.h"; then
		fail "ptrace-only sanity: build.h still enables process_vm"
	elif readelf -r "$PTREE/proot" 2>/dev/null | grep -q 'process_vm'; then
		fail "ptrace-only sanity: binary still references process_vm"
	else
		pass "ptrace-only sanity (no process_vm references)"
	fi
	run_probe "$PTREE/proot" "ptrace-fallback"
fi

echo "== memory: $([ $FAILS -eq 0 ] && echo PASS || echo FAIL) ($FAILS failures) =="
exit $FAILS
