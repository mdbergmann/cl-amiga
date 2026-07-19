# Closure — a Bard's Tale-like game built on the Lambda's Tale engine
# (../lambda-tale-engine).  Self-contained subproject: uses the clamiga
# binary built in the parent repo.
#
#   make test    Run the game's Lisp test suite (host clamiga)
#   make run     Play Closure (interactive ASCII walkabout on the host)
#
# Override the interpreter with CLAMIGA=/path/to/clamiga.

CLAMIGA ?= ../../../build/host/clamiga
HEAP    ?= 16M

.PHONY: test run clamiga-check

test: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --non-interactive --load tests/run-tests.lisp

run: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --load src/main.lisp

clamiga-check:
	@test -x $(CLAMIGA) || { \
	  echo "clamiga not found at $(CLAMIGA)."; \
	  echo "Build it first: make host in the repo root (or set CLAMIGA=/path/to/clamiga)"; \
	  exit 1; }
