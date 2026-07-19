# Lambda's Tale — a Bard's Tale-like dungeon-crawler ENGINE for cl-amiga.
# Self-contained subproject: uses the clamiga binary built in the parent
# repo.  Games live in sibling directories (e.g. ../closure) and load
# the engine via src/load.lisp.
#
#   make test    Run the engine's Lisp test suite (host clamiga)
#   make assets  Regenerate the data/gfx wall-piece ILBMs (tools/gen-walls.lisp)
#
# Override the interpreter with CLAMIGA=/path/to/clamiga.

CLAMIGA ?= ../../../build/host/clamiga
HEAP    ?= 16M

.PHONY: test assets clamiga-check

test: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --non-interactive --load tests/run-tests.lisp

assets: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --non-interactive --load tools/make-assets.lisp

clamiga-check:
	@test -x $(CLAMIGA) || { \
	  echo "clamiga not found at $(CLAMIGA)."; \
	  echo "Build it first: make host in the repo root (or set CLAMIGA=/path/to/clamiga)"; \
	  exit 1; }
