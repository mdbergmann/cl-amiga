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

.PHONY: host test clean verify-amiga

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
	if [ $$failed -eq 0 ]; then echo "=== All tests passed ==="; \
	else echo "=== Some tests failed ==="; exit 1; fi

$(BUILDDIR)/tests/%: $(TEST_SRCDIR)/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -I$(TEST_SRCDIR) -o $@ $< $(LIB_OBJS) -lm

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

clean:
	rm -rf build
