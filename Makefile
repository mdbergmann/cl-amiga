# CL-Amiga Makefile
# Targets: host (Linux), amiga-m68k, amiga-ppc

CC_HOST     = gcc

# libffi + dlopen: the host FFI engine (foreign-funcall / dlsym / callbacks).
# pkg-config locates libffi on both macOS (SDK) and Linux; fall back to -lffi.
UNAME_S    := $(shell uname -s)
FFI_CFLAGS := $(shell pkg-config --cflags libffi 2>/dev/null)
FFI_LIBS   := $(shell pkg-config --libs libffi 2>/dev/null || echo -lffi)
ifneq ($(UNAME_S),Darwin)
FFI_LIBS   += -ldl
# glibc < 2.34 (e.g. Debian 11 / older Raspberry Pi OS) ships libpthread as a
# separate library, so the pthread_* symbols in platform_thread_posix.c need
# -pthread at compile AND link time (newer glibc and macOS resolve them from
# libc/libSystem, which is why the omission only surfaced there).
PTHREAD_FLAGS = -pthread
endif

CFLAGS_HOST = -std=c99 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -g -O3 -flto -DPLATFORM_POSIX -DCL_WIDE_STRINGS $(PTHREAD_FLAGS) $(FFI_CFLAGS) $(DEBUG_FLAGS)
HOST_LIBS   = -lm $(PTHREAD_FLAGS) $(FFI_LIBS)

# Test builds deliberately drop -flto and use -O1 instead of -O3.  The shipped
# clamiga binary is built once at -O3 -flto, but the ~50 unit-test binaries each
# link the entire runtime, and with -flto every one of those links re-runs
# whole-program LTO over the whole runtime — that link step, repeated per test
# binary, dominates `make test` wall-clock.  -O1 + no LTO compiles fast and runs
# fast enough for the suite (gc-stress, which needs the optimized binary, builds
# its own -O3 clamiga separately and is unaffected).  Test objects live in their
# own tree so they never clash with the -O3 -flto objects linked into clamiga.
CFLAGS_TEST = -std=c99 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -g -O1 -DPLATFORM_POSIX -DCL_WIDE_STRINGS $(PTHREAD_FLAGS) $(FFI_CFLAGS) $(DEBUG_FLAGS)

SRCDIR   = src
BUILDDIR = build/host

# Source files
PLATFORM_SRC = $(SRCDIR)/platform/platform_posix.c \
               $(SRCDIR)/platform/platform_thread_posix.c
CORE_SRC     = $(SRCDIR)/core/types.c \
               $(SRCDIR)/core/mem.c \
               $(SRCDIR)/core/error.c \
               $(SRCDIR)/core/symbol.c \
               $(SRCDIR)/core/package.c \
               $(SRCDIR)/core/reader.c \
               $(SRCDIR)/core/readtable.c \
               $(SRCDIR)/core/printer.c \
               $(SRCDIR)/core/env.c \
               $(SRCDIR)/core/compiler.c \
               $(SRCDIR)/core/compiler_special.c \
               $(SRCDIR)/core/compiler_extra.c \
               $(SRCDIR)/core/peephole.c \
               $(SRCDIR)/core/vm.c \
               $(SRCDIR)/core/builtins.c \
               $(SRCDIR)/core/builtins_arith.c \
               $(SRCDIR)/core/builtins_io.c \
               $(SRCDIR)/core/builtins_format.c \
               $(SRCDIR)/core/builtins_mutation.c \
               $(SRCDIR)/core/builtins_strings.c \
               $(SRCDIR)/core/builtins_lists.c \
               $(SRCDIR)/core/builtins_hashtable.c \
               $(SRCDIR)/core/builtins_sequence.c \
               $(SRCDIR)/core/builtins_sequence2.c \
               $(SRCDIR)/core/builtins_type.c \
               $(SRCDIR)/core/builtins_condition.c \
               $(SRCDIR)/core/builtins_package.c \
               $(SRCDIR)/core/builtins_struct.c \
               $(SRCDIR)/core/builtins_array.c \
               $(SRCDIR)/core/builtins_stream.c \
               $(SRCDIR)/core/builtins_random.c \
               $(SRCDIR)/core/builtins_bitvector.c \
               $(SRCDIR)/core/builtins_pathname.c \
               $(SRCDIR)/core/builtins_describe.c \
               $(SRCDIR)/core/builtins_inspect.c \
               $(SRCDIR)/core/builtins_thread.c \
               $(SRCDIR)/core/builtins_ffi.c \
               $(SRCDIR)/core/builtins_amiga.c \
               $(SRCDIR)/core/stream.c \
               $(SRCDIR)/core/bignum.c \
               $(SRCDIR)/core/ratio.c \
               $(SRCDIR)/core/float.c \
               $(SRCDIR)/core/float_math.c \
               $(SRCDIR)/core/debugger.c \
               $(SRCDIR)/core/repl.c \
               $(SRCDIR)/core/fasl.c \
               $(SRCDIR)/core/color.c \
               $(SRCDIR)/core/thread.c \
               $(SRCDIR)/core/string_utils.c
# Portable JIT pieces (no m68k codegen — those live only in
# Makefile.cross).  Compiled into the host build so unit tests can
# exercise code-buffer mechanics.
JIT_SRC      = $(SRCDIR)/jit/codebuf.c
MAIN_SRC     = $(SRCDIR)/main.c

HOST_SRCS = $(MAIN_SRC) $(PLATFORM_SRC) $(CORE_SRC) $(JIT_SRC)
HOST_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(HOST_SRCS))

# Test sources
TEST_SRCDIR = tests
TEST_SRCS   = $(wildcard $(TEST_SRCDIR)/test_*.c)
TEST_BINS   = $(patsubst $(TEST_SRCDIR)/%.c,$(BUILDDIR)/tests/%,$(TEST_SRCS))

# Core sources without main (for linking with tests)
LIB_SRCS = $(PLATFORM_SRC) $(CORE_SRC) $(JIT_SRC)
LIB_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIB_SRCS))

# Separate, non-LTO object tree for the test binaries (see CFLAGS_TEST above).
TESTOBJDIR    = $(BUILDDIR)/test-obj
LIB_TEST_OBJS = $(patsubst $(SRCDIR)/%.c,$(TESTOBJDIR)/%.o,$(LIB_SRCS))

.PHONY: host test test-fast test-plus test-extra linux-test clean verify-amiga install-hooks docs-check docs-update test-gc-stress test-mt-thread-exit-race

host: $(BUILDDIR)/clamiga

# Keep the docs/*.md package symbol lists in sync with the real exports.
# docs-check (CI/pre-commit) fails if any documented extension package's export
# set drifts from docs/package-symbols.txt, or if docs/clamiga.md references a
# CLAMIGA symbol that is no longer exported.  docs-update regenerates the
# snapshot after you have updated the prose.  See tools/docs/package-symbols.sh.
docs-check: host
	@sh tools/docs/package-symbols.sh check $(BUILDDIR)/clamiga

docs-update: host
	@sh tools/docs/package-symbols.sh generate $(BUILDDIR)/clamiga > docs/package-symbols.txt
	@echo "docs-update: regenerated docs/package-symbols.txt"

$(BUILDDIR)/clamiga: $(HOST_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ $^ $(HOST_LIBS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -MMD -MP -c -o $@ $<

# Include auto-generated dependency files
-include $(HOST_OBJS:.o=.d)

# Per-test-binary wall-clock watchdog (seconds).  Belt-and-suspenders on top
# of the in-process SIGALRM watchdog in tests/test.h: catches a hang that
# happens outside a RUN() (e.g. in setup) so a deadlock fails the CI job in
# minutes instead of blocking until GitHub's job timeout.  Used only when a
# timeout/gtimeout binary is on PATH.
TEST_BIN_TIMEOUT ?= 300

# Tests
test-fast: $(TEST_BINS) host
	@echo "=== Running tests (fast tier: skips sento/host-cold-test) ==="
	@export CLAMIGA_NO_USERINIT=1; \
	failed=0; \
	tmo=$$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true); \
	for t in $(TEST_BINS); do \
		echo "--- $$(basename $$t) ---"; \
		rc=0; \
		if [ -n "$$tmo" ]; then \
			$$tmo $(TEST_BIN_TIMEOUT) $$t </dev/null || rc=$$?; \
		else \
			$$t </dev/null || rc=$$?; \
		fi; \
		if [ $$rc -eq 0 ]; then \
			echo "PASS"; \
		else \
			if [ $$rc -eq 124 ]; then \
				echo "FAIL ($$(basename $$t) TIMED OUT after $(TEST_BIN_TIMEOUT)s — likely deadlock)"; \
			else \
				echo "FAIL"; \
			fi; \
			failed=1; \
		fi; \
	done; \
	echo "--- test_batch ---"; \
	if sh $(TEST_SRCDIR)/test_batch.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_boot_log ---"; \
	if sh $(TEST_SRCDIR)/test_boot_log.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_fasl_compat ---"; \
	if sh $(TEST_SRCDIR)/test_fasl_compat.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_boot_fasl_recovery ---"; \
	if sh $(TEST_SRCDIR)/test_boot_fasl_recovery.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_load_exit ---"; \
	if sh $(TEST_SRCDIR)/test_load_exit.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_gray_streams_reload ---"; \
	if sh $(TEST_SRCDIR)/test_gray_streams_reload.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_gc_stream_finalize ---"; \
	if sh $(TEST_SRCDIR)/test_gc_stream_finalize.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_print_controls ---"; \
	if sh $(TEST_SRCDIR)/test_mt_print_controls.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_gc_regression ---"; \
	if sh $(TEST_SRCDIR)/test_mt_gc_regression.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_gc_compact_hang ---"; \
	if sh $(TEST_SRCDIR)/test_mt_gc_compact_hang.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_dispatch_addmethod_race ---"; \
	if sh $(TEST_SRCDIR)/test_mt_dispatch_addmethod_race.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_dispatch_cache_race ---"; \
	if sh $(TEST_SRCDIR)/test_mt_dispatch_cache_race.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_thread_exit_gc ---"; \
	if sh $(TEST_SRCDIR)/test_mt_thread_exit_gc.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_thread_identity ---"; \
	if sh $(TEST_SRCDIR)/test_mt_thread_identity.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_intern_stw ---"; \
	if sh $(TEST_SRCDIR)/test_mt_intern_stw.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_stream_close_race ---"; \
	if sh $(TEST_SRCDIR)/test_mt_stream_close_race.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_interrupt_parked ---"; \
	if sh $(TEST_SRCDIR)/test_mt_interrupt_parked.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_lock_contention_throughput ---"; \
	if sh $(TEST_SRCDIR)/test_mt_lock_contention_throughput.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_mt_print_stress ---"; \
	if sh $(TEST_SRCDIR)/test_mt_print_stress.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_load_keywords ---"; \
	if sh $(TEST_SRCDIR)/test_load_keywords.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_userinit ---"; \
	if sh $(TEST_SRCDIR)/test_userinit.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_compile_file_package ---"; \
	if sh $(TEST_SRCDIR)/test_compile_file_package.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_fasl_cache_dir ---"; \
	if sh $(TEST_SRCDIR)/test_fasl_cache_dir.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_make_load_form ---"; \
	if sh $(TEST_SRCDIR)/test_make_load_form.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_struct_slot_access ---"; \
	if sh $(TEST_SRCDIR)/test_struct_slot_access.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_defconstant_fasl ---"; \
	if sh $(TEST_SRCDIR)/test_defconstant_fasl.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_peephole_diff ---"; \
	if sh $(TEST_SRCDIR)/test_peephole_diff.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_defvar_special_fasl ---"; \
	if sh $(TEST_SRCDIR)/test_defvar_special_fasl.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	echo "--- test_test_extra ---"; \
	if sh $(TEST_SRCDIR)/test_test_extra.sh; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	if [ $$failed -ne 0 ]; then echo "=== Some tests failed ==="; exit 1; fi; \
	echo "=== Fast tests passed ==="

# `make test` runs the fast tier only (the everyday gate).
test: test-fast

# `make test-gc-stress` builds a dedicated DEBUG_GC_STRESS binary (forces a
# compacting GC before every allocation) and runs the GC-stress regression
# suite against it.  Kept out of the fast tier because it needs a separate,
# slow build; run it after touching GC, the FASL reader/writer, the compiler,
# or any C builtin that holds CL_Obj values across allocating calls.
GC_STRESS_BUILDDIR = build/host-gcstress
test-gc-stress:
	@$(MAKE) --no-print-directory host \
		BUILDDIR=$(GC_STRESS_BUILDDIR) \
		DEBUG_FLAGS="-DDEBUG_GC_STRESS"
	@echo "--- test_gc_stress_regression ---"
	@sh $(TEST_SRCDIR)/test_gc_stress_regression.sh $(GC_STRESS_BUILDDIR)/clamiga
	@echo "--- test_mt_intern_stw (CLAMIGA_GC_STRESS=1) ---"
	@CLAMIGA_GC_STRESS=1 sh $(TEST_SRCDIR)/test_mt_intern_stw.sh $(GC_STRESS_BUILDDIR)/clamiga
	@echo "--- test_peephole_diff (CLAMIGA_GC_STRESS=1, forced compaction) ---"
	@CLAMIGA_GC_STRESS=1 sh $(TEST_SRCDIR)/test_peephole_diff.sh $(GC_STRESS_BUILDDIR)/clamiga

# `make test-mt-thread-exit-race` builds a dedicated DEBUG_THREAD_RACE_HOOKS
# binary whose sole purpose (see the constructor in src/core/thread.c) is to
# deterministically force the exact STW-hang-on-thread-exit race window that
# cl_thread_unregister's gc_condvar broadcast fixes, instead of relying on
# scheduler timing to hit it.  Kept out of the fast tier because it needs a
# separate build; run it after touching thread registration, safepoints, or
# stop-the-world coordination in thread.c.
RACE_BUILDDIR = build/host-race
test-mt-thread-exit-race: host
	@$(MAKE) --no-print-directory host \
		BUILDDIR=$(RACE_BUILDDIR) \
		DEBUG_FLAGS="-DDEBUG_THREAD_RACE_HOOKS"
	@echo "--- test_mt_thread_exit_gc (deterministic race) ---"
	@sh $(TEST_SRCDIR)/test_mt_thread_exit_gc.sh $(BUILDDIR)/clamiga $(RACE_BUILDDIR)/clamiga

# `make test-plus` adds the host-cold-test (sento cold-load smoke test) on top
# of the fast tier.
test-plus: test-fast
	@export CLAMIGA_NO_USERINIT=1; \
	echo "--- host-cold-test ---"; \
	if $(MAKE) --no-print-directory host-cold-test; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		exit 1; \
	fi; \
	echo "=== All tests passed ==="

$(TESTOBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_TEST) -I$(SRCDIR) -MMD -MP -c -o $@ $<

# Without this, make treats the test objects as intermediate files (they are
# only reached through the pattern rule above) and deletes them after each
# build — forcing a full recompile of the runtime on every `make test`.
# .SECONDARY keeps them so incremental test builds are fast.
.SECONDARY: $(LIB_TEST_OBJS)

-include $(LIB_TEST_OBJS:.o=.d)

$(BUILDDIR)/tests/%: $(TEST_SRCDIR)/%.c $(TEST_SRCDIR)/test.h $(LIB_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_TEST) -I$(SRCDIR) -I$(TEST_SRCDIR) -o $@ $< $(LIB_TEST_OBJS) $(HOST_LIBS)

# Cold-load smoke test: runs the sento test suite end-to-end from an
# empty FASL cache.  Exercises the source-load + auto-cache path that
# carries lib/clos.lisp + lib/asdf.lisp + sento's full dependency tree
# through the compiler and FASL writer in one shot.  Catches regressions
# the C tests don't see — e.g. a missing/stock trivial-garbage backend
# (upstream trivial-garbage rejects cl-amiga as an unsupported Lisp,
# so the CL-Amiga fork must be present in local-projects).
#
# Auto-skipped when prerequisites aren't met (no quicklisp install, no
# trunk script, no CL-Amiga library forks in local-projects) so the
# target is safe to keep in `make test` for contributors without a
# quicklisp setup.
HOST_COLD_TEST_SCRIPT  = trunk/load-and-test-sento-system.lisp
HOST_COLD_TEST_LOG     = $(BUILDDIR)/cold-test.log
# Wall-clock watchdog (seconds). Matches the cold-sento headroom in
# trunk/run-load-and-test-all.sh so a genuine in-suite hang fails loudly
# instead of blocking `make test` forever.
HOST_COLD_TEST_TIMEOUT = 1800
host-cold-test: host
	@set -e; \
	if [ ! -f "$(HOME)/quicklisp/setup.lisp" ]; then \
	  echo "=== host-cold-test: ~/quicklisp not installed — skipped ==="; \
	  exit 0; \
	fi; \
	if [ ! -f "$(HOST_COLD_TEST_SCRIPT)" ]; then \
	  echo "=== host-cold-test: $(HOST_COLD_TEST_SCRIPT) missing — skipped ==="; \
	  exit 0; \
	fi; \
	if [ ! -d "$(QL_LOCAL_PROJECTS)/trivial-garbage" ]; then \
	  echo "=== host-cold-test: CL-Amiga library forks not in local-projects — skipped ==="; \
	  exit 0; \
	fi; \
	echo "=== host-cold-test: clearing FASL cache and running $(HOST_COLD_TEST_SCRIPT) ==="; \
	rm -rf $(HOME)/.cache/common-lisp/cl-amiga-*; \
	mkdir -p $(dir $(HOST_COLD_TEST_LOG)); \
	tmo=$$(command -v timeout 2>/dev/null || command -v gtimeout 2>/dev/null || true); \
	if [ -n "$$tmo" ]; then \
	  runner="$$tmo $(HOST_COLD_TEST_TIMEOUT)"; \
	else \
	  runner=""; \
	  echo "=== host-cold-test: no timeout/gtimeout on PATH — running without watchdog ==="; \
	fi; \
	rc=0; \
	$$runner $(BUILDDIR)/clamiga --no-userinit --heap 384M --non-interactive --load $(HOST_COLD_TEST_SCRIPT) \
	  </dev/null > $(HOST_COLD_TEST_LOG) 2>&1 || rc=$$?; \
	if [ $$rc -eq 124 ]; then \
	  echo "=== FAIL: host-cold-test timed out after $(HOST_COLD_TEST_TIMEOUT)s (see $(HOST_COLD_TEST_LOG)) ==="; \
	  tail -30 $(HOST_COLD_TEST_LOG); \
	  exit 1; \
	fi; \
	if grep -q "FATAL Signal" $(HOST_COLD_TEST_LOG); then \
	  echo "=== FAIL: clamiga crashed during cold load (see $(HOST_COLD_TEST_LOG)) ==="; \
	  grep -B2 -A1 "FATAL Signal" $(HOST_COLD_TEST_LOG) | head -20; \
	  exit 1; \
	fi; \
	if ! grep -qE "Did [0-9]+ checks" $(HOST_COLD_TEST_LOG); then \
	  echo "=== FAIL: sento test suite never reported its result line ==="; \
	  tail -30 $(HOST_COLD_TEST_LOG); \
	  exit 1; \
	fi; \
	if [ $$rc -ne 0 ]; then \
	  echo "=== FAIL: clamiga exited rc=$$rc ==="; \
	  tail -30 $(HOST_COLD_TEST_LOG); \
	  exit 1; \
	fi; \
	checks=$$(grep -oE "Did [0-9]+ checks" $(HOST_COLD_TEST_LOG) | head -1); \
	pass=$$(grep -oE "Pass: [0-9]+" $(HOST_COLD_TEST_LOG) | head -1); \
	fail=$$(grep -oE "Fail: [0-9]+" $(HOST_COLD_TEST_LOG) | head -1); \
	echo "=== host-cold-test: PASS ($$checks; $$pass; $$fail) ==="

# Run all trunk/load-and-test-*.lisp integration scripts and print an
# aggregate pass/fail tally.  Requires quicklisp and installed shims.
# NOT wired into 'make test' — heavyweight, needs quicklisp/ansi-tests.
# Set COLD=1 to clear the FASL cache before each script (cold-boot mode).
test-extra: host
	@export CLAMIGA_NO_USERINIT=1; \
	sh trunk/run-load-and-test-all.sh $(if $(filter 1,$(COLD)),--cold)

# Run host build + tests inside an Ubuntu container (matches GitHub Actions
# `ubuntu-latest`).  Requires a working `docker` CLI (Docker Desktop, OrbStack,
# Colima, ...).  Mounts the working tree read-write so build artifacts land in
# build/host inside the container — wipe with `make clean` afterwards if
# needed since they are Linux ELFs, not host binaries.
LINUX_TEST_IMAGE ?= ubuntu:24.04
linux-test:
	@command -v docker >/dev/null 2>&1 || { \
	  echo "docker CLI not found — install Docker Desktop, OrbStack, or Colima"; \
	  exit 1; \
	}
	docker run --rm -v "$(CURDIR)":/work -w /work $(LINUX_TEST_IMAGE) bash -c '\
	  set -e; \
	  export DEBIAN_FRONTEND=noninteractive; \
	  apt-get update -qq >/dev/null; \
	  apt-get install -y -qq build-essential >/dev/null; \
	  make clean >/dev/null; \
	  make host && make test'

# Verify Amiga test results (after FS-UAE run)
verify-amiga:
	@if [ ! -f build/amiga/test-results.log ]; then \
		echo "No test results found. Run FS-UAE first."; \
		exit 1; \
	fi
	@echo "=== Amiga Test Results ==="
	@cat build/amiga/test-results.log
	@echo ""
	@if grep -q "ALL TESTS PASSED" build/amiga/test-results.log; then \
		echo "=== Amiga verification PASSED ==="; \
	else \
		echo "=== Amiga verification FAILED ==="; \
		exit 1; \
	fi

# Pre-compile boot files to FASL for faster startup
fasl: $(BUILDDIR)/clamiga
	@echo "=== Compiling boot.lisp → lib/boot.fasl ==="
	$(BUILDDIR)/clamiga --no-userinit --heap 24M \
		--eval '(compile-file "lib/boot.lisp" :output-file "lib/boot.fasl")' \
		--eval '(quit)'
	@echo "=== Compiling clos.lisp → lib/clos.fasl ==="
	$(BUILDDIR)/clamiga --no-userinit --heap 24M \
		--eval '(compile-file "lib/clos.lisp" :output-file "lib/clos.fasl")' \
		--eval '(quit)'

QL_LOCAL_PROJECTS ?= $(HOME)/quicklisp/local-projects

# Install CL-Amiga's `swank` stub system into quicklisp's local-projects
# tree via symlink.  Needed on dev hosts where quicklisp is installed —
# this stub is NOT required on Amiga when quicklisp isn't in use.
#
# The closer-mop / trivial-cltl2 / introspect-environment / trivial-garbage
# systems are NO LONGER shims: they are maintained CL-Amiga library forks
# that carry #+cl-amiga / #+clamiga support directly, installed by cloning
# them into local-projects (see the Quicklisp section of README.md).  Only
# the `swank` stub — which has no upstream to fork — is symlinked here.
#
# Symlink handling rules:
#   - Broken symlink (dangling target):     re-create pointing at $$src
#   - Correct symlink (already points at $$src): leave alone
#   - Symlink to somewhere else / real dir:      warn and skip
#   - Missing:                                    create
#
# The "broken symlink" case used to be handled wrong: an existence check
# (`[ -e "$$dst" ]`) returns false for a dangling symlink, but a
# `[ -L "$$dst" ]` (or just an unguarded `ln -s`) ran into the existing
# symlink anyway.  This silently left broken links pointing to old paths
# after the project was moved, causing quicklisp to fall back to upstream
# packages and reject cl-amiga as an "unsupported Lisp".
install-shims:
	@mkdir -p $(QL_LOCAL_PROJECTS)
	@for shim in swank; do \
	  src="$(CURDIR)/contrib/shims/$$shim"; \
	  dst="$(QL_LOCAL_PROJECTS)/$$shim"; \
	  if [ -L "$$dst" ]; then \
	    target=$$(readlink "$$dst"); \
	    if [ "$$target" = "$$src" ]; then \
	      echo "=> $$dst already correct"; \
	    elif [ -e "$$dst" ]; then \
	      echo "=> WARNING: $$dst is a symlink to $$target (not $$src) — leaving alone"; \
	    else \
	      rm "$$dst" && ln -s "$$src" "$$dst" && \
	        echo "=> repaired broken symlink $$dst -> $$src (was -> $$target)"; \
	    fi; \
	  elif [ -e "$$dst" ]; then \
	    echo "=> WARNING: $$dst exists as a regular file/dir — leaving alone"; \
	  else \
	    ln -s "$$src" "$$dst" && echo "=> linked $$dst -> $$src"; \
	  fi; \
	done

# Activate the auto-review git hook for this clone. Sets a RELATIVE core.hooksPath
# (githooks) so it survives the repo being moved and works the same in every clone.
# Run once after cloning. See scripts/review/README.md.
install-hooks:
	@git config core.hooksPath githooks
	@chmod +x githooks/* 2>/dev/null || true
	@echo "=> auto-review hook activated (core.hooksPath=githooks)"
	@echo "   bypass one commit with 'git commit --no-verify'; disable with CLAUDE_AUTO_REVIEW=0"

clean:
	rm -rf build
