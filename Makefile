# CL-Amiga Makefile
# Targets: host (Linux), amiga-m68k, amiga-ppc

CC_HOST     = gcc
CFLAGS_HOST = -std=c99 -Wall -Wextra -Wpedantic -g -O2 -DPLATFORM_POSIX

SRCDIR   = src
BUILDDIR = build/host

# Source files
PLATFORM_SRC = $(SRCDIR)/platform/platform_posix.c
CORE_SRC     = $(SRCDIR)/core/types.c \
               $(SRCDIR)/core/mem.c \
               $(SRCDIR)/core/error.c \
               $(SRCDIR)/core/symbol.c \
               $(SRCDIR)/core/package.c \
               $(SRCDIR)/core/reader.c \
               $(SRCDIR)/core/printer.c \
               $(SRCDIR)/core/env.c \
               $(SRCDIR)/core/compiler.c \
               $(SRCDIR)/core/compiler_special.c \
               $(SRCDIR)/core/compiler_extra.c \
               $(SRCDIR)/core/vm.c \
               $(SRCDIR)/core/builtins.c \
               $(SRCDIR)/core/builtins_arith.c \
               $(SRCDIR)/core/builtins_io.c \
               $(SRCDIR)/core/builtins_mutation.c \
               $(SRCDIR)/core/builtins_strings.c \
               $(SRCDIR)/core/builtins_lists.c \
               $(SRCDIR)/core/repl.c
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
	$(CC_HOST) $(CFLAGS_HOST) -o $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -c -o $@ $<

# Tests
test: $(TEST_BINS)
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
	if [ $$failed -eq 0 ]; then echo "=== All tests passed ==="; \
	else echo "=== Some tests failed ==="; exit 1; fi

$(BUILDDIR)/tests/%: $(TEST_SRCDIR)/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC_HOST) $(CFLAGS_HOST) -I$(SRCDIR) -I$(TEST_SRCDIR) -o $@ $< $(LIB_OBJS)

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

clean:
	rm -rf build
