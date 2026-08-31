# ---------------------------------------------------------------------------
# LinuxDroid-PRoot - top level build system
#
# The LinuxDroid PRoot fork keeps the upstream PRoot tree (under src/)
# structurally intact and builds it out-of-tree.  Each ABI gets its own
# build directory under build/<abi>/ so the upstream tree is never
# modified and multiple targets can coexist.
#
# Primary artifacts:
#   proot            - the native PRoot engine
#   loader           - the embedded userspace ELF loader (also exported
#                      standalone as a first-class artifact)
#   linuxdroid-selftest
#                    - the LinuxDroid compatibility self-test diagnostic
#
# Targets:
#   make proot               build the desktop-Linux PRoot engine
#   make loader              build + export the userspace loader artifact
#   make selftest            build the self-test diagnostic (host)
#   make android-arm64       cross-compile for aarch64-linux-android (NDK)
#   make android-x86_64      cross-compile for x86_64-linux-android (NDK)
#   make android-release     assemble deterministic Android release artifacts
#   make test                run upstream functional tests on the host
#   make release             assemble the host release artifact tree
#   make clean               remove all build outputs
# ---------------------------------------------------------------------------

SHELL := /bin/bash

# Single authoritative PRoot artifact version. LinuxDroid uses this exact
# semantic version in its runtime compatibility gate; do not introduce a second
# version identity here.
PROOT_VERSION ?= 0.1.0

# Where everything under our control is written.  Never put build output
# inside src/ - that tree is a faithful copy of upstream PRoot.
BUILD_DIR ?= build

# Compiler for the desktop (host) target.
HOST_CC   ?= gcc
HOST_STRIP ?= strip
HOST_OBJCOPY ?= objcopy
HOST_OBJDUMP ?= objdump

# ---------------------------------------------------------------------------
# Vendored talloc
#
# Upstream PRoot links against talloc (Samba) and expects it via pkg-config.
# LinuxDroid-PRoot vendors talloc under third_party/talloc so that builds are
# self-contained on desktop and on the Android NDK (no system libtalloc-dev,
# no pkg-config on the build host).
# ---------------------------------------------------------------------------
TALLOC_SRC     := $(CURDIR)/third_party/talloc
TALLOC_CFLAGS  := -I$(TALLOC_SRC) -std=gnu99 -O2 \
                   -DTALLOC_BUILD_VERSION_MAJOR=2 \
                   -DTALLOC_BUILD_VERSION_MINOR=4 \
                   -DTALLOC_BUILD_VERSION_RELEASE=2

# LinuxDroid Android compatibility boundary (untag.h & friends).  The
# tracee-memory subsystem includes native/android/untag.h for ARM64
# tracee-address normalization.  src/GNUmakefile adds the -I for it (so
# in-tree `make -C src` builds work too); the per-target rules below copy
# it into build/ (like lib/uthash) so out-of-tree builds resolve it.
NATIVE_ANDROID := $(CURDIR)/native/android

.PHONY: _talloc_host
_talloc_host:
	@mkdir -p $(BUILD_DIR)/talloc/host
	$(HOST_CC) -c $(TALLOC_CFLAGS) $(TALLOC_SRC)/talloc.c \
		-o $(BUILD_DIR)/talloc/host/talloc.o
	ar rcs $(BUILD_DIR)/talloc/host/libtalloc.a $(BUILD_DIR)/talloc/host/talloc.o

# ---------------------------------------------------------------------------
# Android NDK toolchain
#
# Location of the Android NDK.  Override with NDK_ROOT, or set
# ANDROID_NDK_ROOT in the environment.  The CI workflow downloads a pinned
# NDK (see .github/workflows/ci.yml).
# ---------------------------------------------------------------------------
ANDROID_API  ?= 28
NDK_ROOT     ?= $(ANDROID_NDK_ROOT)
NDK_HOST_TAG ?= linux-x86_64
NDK_LLVM     := $(NDK_ROOT)/toolchains/llvm/prebuilt/$(NDK_HOST_TAG)

# Runtime guard so the Android targets give a clear message instead of
# cryptic "No such file" errors when the NDK is not installed.  Because this
# is a recipe check (not a parse-time $(error)) it never affects the host
# targets (proot / loader / selftest / test).
.PHONY: .ndk-check
.ndk-check:
	@test -n "$(NDK_ROOT)" || { \
		echo "Error: Android NDK not found."; \
		echo "Set NDK_ROOT=/path/to/android-ndk or ANDROID_NDK_ROOT."; \
		echo "See docs/build.md and .github/workflows/ci.yml for install steps."; \
		exit 1; }; \
	test -x "$(NDK_LLVM)/bin/aarch64-linux-android$(ANDROID_API)-clang" || { \
		echo "Error: NDK clang not found at $(NDK_LLVM)/bin."; \
		exit 1; }

# Android requires position-independent executables (PIE) since API level 21.
ANDROID_PIE := -fPIE -pie

define android_cross_vars
$(eval ABI_CC := $(NDK_LLVM)/bin/$2$(ANDROID_API)-clang)
endef

# ---------------------------------------------------------------------------
# Per-target convenience targets
# ---------------------------------------------------------------------------
.PHONY: all proot loader selftest android-arm64 android-x86_64 android-release test release clean

all: proot

# Build the desktop-Linux PRoot engine and the userspace loader.
proot: $(BUILD_DIR)/host/proot

# Build and export the standalone userspace loader artifact.
loader: $(BUILD_DIR)/host/proot
	@echo "  EXPORT loader -> $(BUILD_DIR)/host/loader.bin"
	@cp $(BUILD_DIR)/host/loader/loader $(BUILD_DIR)/host/loader.bin 2>/dev/null || true

selftest: $(BUILD_DIR)/host/linuxdroid-selftest

# ---------------------------------------------------------------------------
# Desktop (host) build
#
# We copy the upstream tree into build/host and drive the upstream
# GNUmakefile there.  talloc flags are supplied directly instead of through
# pkg-config (which is not required on the build host).
# ---------------------------------------------------------------------------
HOST_GNUMAKE := $(BUILD_DIR)/host/GNUmakefile

$(BUILD_DIR)/host/proot: _talloc_host $(HOST_GNUMAKE)
	@echo "  BUILD host proot"
	@$(MAKE) -C $(BUILD_DIR)/host -f GNUmakefile \
		CC=$(HOST_CC) STRIP=$(HOST_STRIP) OBJCOPY=$(HOST_OBJCOPY) OBJDUMP=$(HOST_OBJDUMP) \
		CFLAGS="-g -O2 -Wall -Wextra -I$(TALLOC_SRC)" \
		LDFLAGS="-Wl,-z,noexecstack $(CURDIR)/$(BUILD_DIR)/talloc/host/libtalloc.a" \
		-j$(shell nproc) proot
	@cp $(BUILD_DIR)/host/proot $(BUILD_DIR)/host/proot.host 2>/dev/null || true
	@echo "  OK host proot -> $(BUILD_DIR)/host/proot"

$(HOST_GNUMAKE): | $(BUILD_DIR)/host
	@cp -r src/. $(BUILD_DIR)/host/
	@mkdir -p $(BUILD_DIR)/lib
	@cp -r lib/uthash $(BUILD_DIR)/lib/uthash
	@mkdir -p $(BUILD_DIR)/native
	@cp -r $(NATIVE_ANDROID) $(BUILD_DIR)/native/android

$(BUILD_DIR)/host:
	@mkdir -p $(BUILD_DIR)/host

# ---------------------------------------------------------------------------
# Host self-test diagnostic
# ---------------------------------------------------------------------------
$(BUILD_DIR)/host/linuxdroid-selftest: tools/selftest/linuxdroid-selftest.c
	@mkdir -p $(dir $@)
	@echo "  CC  selftest (host)"
	@$(HOST_CC) -O2 -Wall -Wextra -D_GNU_SOURCE -o $@ $<

# ---------------------------------------------------------------------------
# Android cross-compilation
#
# Each ABI builds its own talloc and its own copy of the upstream tree,
# then links with -fPIE -pie as required by modern Android.
# ---------------------------------------------------------------------------
ANDROID_ABIS := android-arm64 android-x86_64

define android_build_abi
.PHONY: $1
$1: .ndk-check _talloc_$2 $$(BUILD_DIR)/$2/GNUmakefile
	@echo "  BUILD $1 ($$(shell uname -m) -> $2)"
	@$$(MAKE) -C $$(BUILD_DIR)/$2 -f GNUmakefile \
		CC=$(NDK_LLVM)/bin/$3$(ANDROID_API)-clang \
		STRIP=$(NDK_LLVM)/bin/llvm-strip \
		OBJCOPY=$(NDK_LLVM)/bin/llvm-objcopy \
		OBJDUMP=$(NDK_LLVM)/bin/llvm-objdump \
		CFLAGS="-g -O2 -Wall -Wextra -I$(TALLOC_SRC) $(ANDROID_PIE) -fPIC -DHAVE_PROCESS_VM -DHAVE_SECCOMP_FILTER" \
		LDFLAGS="-Wl,-z,noexecstack -pie $$(CURDIR)/$$(BUILD_DIR)/talloc/$2/libtalloc.a" \
		-j$$(shell nproc) proot
	@mkdir -p $$(BUILD_DIR)/android/$2
	@cp $$(BUILD_DIR)/$2/proot $$(BUILD_DIR)/android/$2/proot
	@cp $$(BUILD_DIR)/$2/loader/loader $$(BUILD_DIR)/android/$2/loader 2>/dev/null || true
	@echo "  CC  linuxdroid-selftest ($1)"
	@$(NDK_LLVM)/bin/$3$(ANDROID_API)-clang -O2 -Wall -Wextra -D_GNU_SOURCE $(ANDROID_PIE) \
		-o $$(BUILD_DIR)/android/$2/linuxdroid-selftest \
		tools/selftest/linuxdroid-selftest.c
	@echo "  CC  guest-mem probe ($1)"
	@$(NDK_LLVM)/bin/$3$(ANDROID_API)-clang -O2 -Wall -Wextra -D_GNU_SOURCE $(ANDROID_PIE) \
		-o $$(BUILD_DIR)/android/$2/guest-mem \
		tests/memory/guest-mem.c
	@echo "  OK $1 -> $$(BUILD_DIR)/android/$2/{proot,loader,linuxdroid-selftest,guest-mem}"
endef

# talloc cross build for an ABI
define android_talloc_abi
.PHONY: _talloc_$1
_talloc_$1:
	@mkdir -p $$(BUILD_DIR)/talloc/$1
	$(NDK_LLVM)/bin/$2$(ANDROID_API)-clang -c $(TALLOC_CFLAGS) -fPIC \
		$(TALLOC_SRC)/talloc.c -o $$(BUILD_DIR)/talloc/$1/talloc.o
	$(NDK_LLVM)/bin/llvm-ar rcs $$(BUILD_DIR)/talloc/$1/libtalloc.a $$(BUILD_DIR)/talloc/$1/talloc.o
endef

$(eval $(call android_talloc_abi,arm64-v8a,aarch64-linux-android))
$(eval $(call android_talloc_abi,x86_64,x86_64-linux-android))

$(eval $(call android_build_abi,android-arm64,arm64-v8a,aarch64-linux-android))
$(eval $(call android_build_abi,android-x86_64,x86_64,x86_64-linux-android))

$(BUILD_DIR)/arm64-v8a/GNUmakefile: | $(BUILD_DIR)/arm64-v8a
	@cp -r src/. $(BUILD_DIR)/arm64-v8a/
	@mkdir -p $(BUILD_DIR)/lib
	@cp -r lib/uthash $(BUILD_DIR)/lib/uthash
	@mkdir -p $(BUILD_DIR)/native
	@cp -r $(NATIVE_ANDROID) $(BUILD_DIR)/native/android

$(BUILD_DIR)/arm64-v8a:
	@mkdir -p $(BUILD_DIR)/arm64-v8a

$(BUILD_DIR)/x86_64/GNUmakefile: | $(BUILD_DIR)/x86_64
	@cp -r src/. $(BUILD_DIR)/x86_64/
	@mkdir -p $(BUILD_DIR)/lib
	@cp -r lib/uthash $(BUILD_DIR)/lib/uthash
	@mkdir -p $(BUILD_DIR)/native
	@cp -r $(NATIVE_ANDROID) $(BUILD_DIR)/native/android

$(BUILD_DIR)/x86_64:
	@mkdir -p $(BUILD_DIR)/x86_64

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
test: proot loader
	@echo "  TEST host (LinuxDroid test suites)"
	@tests/functional/run.sh
	@tests/seccomp/run.sh
	@tests/process-signal/run.sh
	@tests/pty/run.sh
	@tests/loader/run.sh
	@tests/memory/run.sh
	@echo "  TEST all suites: PASS"

# The upstream PRoot suite (test/) additionally requires building a busybox
# rootfs and a network-capable CI runner.  Run it directly when available:
#   make -C test check

# ---------------------------------------------------------------------------
# Release artifact assembly
#
# The manifest is the machine-readable contract consumed by LinuxDroid's
# RuntimeAssetsManager. It is intentionally keyed so a parser can read it
# without depending on prose. Version is the single authoritative
# $(PROOT_VERSION); commit is the exact source tree used for the build.
# ---------------------------------------------------------------------------
define write_manifest
	@printf '%s\n' \
		"LinuxDroid-PRoot v$(PROOT_VERSION)" \
		"commit:  $(shell git rev-parse --short HEAD 2>/dev/null)" \
		"ABI:     $2" \
		"arch:    $3" \
		"cc:      $4" \
		"ndk:     $5" \
		"android: 16+ (API 36)" \
		"sha256:" \
		"  proot:    $$(sha256sum $6 2>/dev/null | cut -d' ' -f1)" \
		"  loader:   $$(sha256sum $7 2>/dev/null | cut -d' ' -f1)" \
		"built:   $$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
		> $1
endef

release: proot loader
	@mkdir -p $(BUILD_DIR)/release
	@cp $(BUILD_DIR)/host/proot $(BUILD_DIR)/release/proot
	@cp $(BUILD_DIR)/host/loader.bin $(BUILD_DIR)/release/loader 2>/dev/null || true
	@$(call write_manifest, \
		$(BUILD_DIR)/release/MANIFEST.txt, \
		host, \
		$(shell uname -m), \
		$(HOST_CC), \
		host, \
		$(BUILD_DIR)/release/proot, \
		$(BUILD_DIR)/release/loader)
	@echo "  OK host release -> $(BUILD_DIR)/release"

# ---------------------------------------------------------------------------
# Deterministic Android release artifact tree
#
#   $(BUILD_DIR)/android-release/
#     arm64-v8a/  proot, loader, MANIFEST.txt
#     x86_64/     proot, loader, MANIFEST.txt
#
# This is the exact tree consumed by LinuxDroid's RuntimeAssetsManager.
# ---------------------------------------------------------------------------
.PHONY: android-release
android-release: android-arm64 android-x86_64
	@set -e; \
	for abi in arm64-v8a x86_64; do \
		mkdir -p "$(BUILD_DIR)/android-release/$$abi"; \
		cp "$(BUILD_DIR)/android/$$abi/proot" "$(BUILD_DIR)/android-release/$$abi/proot"; \
		test -f "$(BUILD_DIR)/android/$$abi/loader" || { \
			echo "Error: $(BUILD_DIR)/android/$$abi/loader is missing"; exit 1; }; \
		cp "$(BUILD_DIR)/android/$$abi/loader" "$(BUILD_DIR)/android-release/$$abi/loader"; \
	done
	@$(call write_manifest, \
		$(BUILD_DIR)/android-release/arm64-v8a/MANIFEST.txt, \
		arm64-v8a, \
		aarch64, \
		$(NDK_LLVM)/bin/aarch64-linux-android$(ANDROID_API)-clang, \
		$(NDK_ROOT), \
		$(BUILD_DIR)/android-release/arm64-v8a/proot, \
		$(BUILD_DIR)/android-release/arm64-v8a/loader)
	@$(call write_manifest, \
		$(BUILD_DIR)/android-release/x86_64/MANIFEST.txt, \
		x86_64, \
		x86_64, \
		$(NDK_LLVM)/bin/x86_64-linux-android$(ANDROID_API)-clang, \
		$(NDK_ROOT), \
		$(BUILD_DIR)/android-release/x86_64/proot, \
		$(BUILD_DIR)/android-release/x86_64/loader)
	@echo "  OK android release -> $(BUILD_DIR)/android-release"

# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------
clean:
	@rm -rf $(BUILD_DIR)
