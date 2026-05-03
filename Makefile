# CL-Amiga Makefile
# Targets: host (Linux), amiga-m68k, amiga-ppc

CC_HOST     = gcc
CFLAGS_HOST = -std=c99 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -g -O3 -flto -DPLATFORM_POSIX -DCL_WIDE_STRINGS $(DEBUG_FLAGS)

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
MAIN_SRC     = $(SRCDIR)/main.c

HOST_SRCS = $(MAIN_SRC) $(PLATFORM_SRC) $(CORE_SRC)
HOST_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(HOST_SRCS))

# Test sources
TEST_SRCDIR = tests
TEST_SRCS   = $(wildcard $(TEST_SRCDIR)/test_*.c)
TEST_BINS   = $(patsubst $(TEST_SRCDIR)/%.c,$(BUILDDIR)/tests/%,$(TEST_SRCS))

# Core sources without main (for linking with tests)
LIB_SRCS = $(PLATFORM_SRC) $(CORE_SRC)
LIB_OBJS = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(LIB_SRCS))

.PHONY: host test linux-test clean verify-amiga

host: $(BUILDDIR)/clamiga

$(BUILDDIR)/clamiga: $(HOST_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -o $@ $^ -lm

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -MMD -MP -c -o $@ $<

# Include auto-generated dependency files
-include $(HOST_OBJS:.o=.d)

# Tests
test: $(TEST_BINS) host
	@echo "=== Running tests ==="
	@failed=0; \
	for t in $(TEST_BINS); do \
		echo "--- $$(basename $$t) ---"; \
		if $$t; then \
			echo "PASS"; \
		else \
			echo "FAIL"; \
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
	echo "--- test_fasl_compat ---"; \
	if sh $(TEST_SRCDIR)/test_fasl_compat.sh $(BUILDDIR)/clamiga; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		failed=1; \
	fi; \
	if [ $$failed -ne 0 ]; then echo "=== Some tests failed ==="; exit 1; fi; \
	echo "--- host-cold-test ---"; \
	if $(MAKE) --no-print-directory host-cold-test; then \
		echo "PASS"; \
	else \
		echo "FAIL"; \
		exit 1; \
	fi; \
	echo "=== All tests passed ==="

$(BUILDDIR)/tests/%: $(TEST_SRCDIR)/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -I$(TEST_SRCDIR) -o $@ $< $(LIB_OBJS) -lm

# Cold-load smoke test: runs the sento test suite end-to-end from an
# empty FASL cache.  Exercises the source-load + auto-cache path that
# carries lib/clos.lisp + lib/asdf.lisp + sento's full dependency tree
# through the compiler and FASL writer in one shot.  Catches regressions
# the C tests don't see — e.g. broken `make install-shims` symlinks
# falling back to upstream trivial-garbage and rejecting cl-amiga
# (caught a stale-symlink regression that hid for ~2 weeks).
#
# Auto-skipped when prerequisites aren't met (no quicklisp install, no
# trunk script, no installed shims) so the target is safe to keep in
# `make test` for contributors without a quicklisp setup.
HOST_COLD_TEST_SCRIPT = trunk/load-and-test-sento-system.lisp
HOST_COLD_TEST_LOG    = $(BUILDDIR)/cold-test.log
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
	if [ ! -L "$(QL_LOCAL_PROJECTS)/trivial-garbage" ]; then \
	  echo "=== host-cold-test: shims not installed (run 'make install-shims') — skipped ==="; \
	  exit 0; \
	fi; \
	echo "=== host-cold-test: clearing FASL cache and running $(HOST_COLD_TEST_SCRIPT) ==="; \
	rm -rf $(HOME)/.cache/common-lisp/cl-amiga-*; \
	mkdir -p $(dir $(HOST_COLD_TEST_LOG)); \
	$(BUILDDIR)/clamiga --heap 384M --load $(HOST_COLD_TEST_SCRIPT) \
	  > $(HOST_COLD_TEST_LOG) 2>&1; rc=$$?; \
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
	$(BUILDDIR)/clamiga --heap 24M \
		--eval '(compile-file "lib/boot.lisp" :output-file "lib/boot.fasl")' \
		--eval '(quit)'
	@echo "=== Compiling clos.lisp → lib/clos.fasl ==="
	$(BUILDDIR)/clamiga --heap 24M \
		--eval '(compile-file "lib/clos.lisp" :output-file "lib/clos.fasl")' \
		--eval '(quit)'

QL_LOCAL_PROJECTS ?= $(HOME)/quicklisp/local-projects

# Install CL-Amiga's shim systems (closer-mop, trivial-cltl2,
# trivial-garbage) into quicklisp's local-projects tree via symlink.
# Needed on dev hosts where quicklisp is installed — these shims are
# NOT required on Amiga when quicklisp isn't in use.  Long-term goal:
# merge the #+clamiga branches upstream so stock packages work out of
# the box, at which point this target becomes obsolete.
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
	@for shim in closer-mop trivial-cltl2 trivial-garbage; do \
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

clean:
	rm -rf build
