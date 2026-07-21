# Lambda's Tale — a Bard's Tale-like dungeon-crawler ENGINE for cl-amiga.
# Self-contained subproject: uses the clamiga binary built in the parent
# repo.  Games live in sibling directories (e.g. ../closure) and load
# the engine via src/load.lisp.
#
#   make test    Run the engine's Lisp test suite (host clamiga)
#   make assets  Regenerate the data/gfx wall-piece ILBMs (tools/gen-walls.lisp)
#   make pack    Build a tile pack from one facade picture:
#                  make pack ART=art/house.iff OUT=data/gfx-town/
#   make preview Composite a pack into a picture you can look at:
#                  make preview PACK=data/gfx-town/ [OUT=preview.iff]
#
# Override the interpreter with CLAMIGA=/path/to/clamiga.

CLAMIGA ?= ../../../build/host/clamiga
HEAP    ?= 16M
OUT     ?=

.PHONY: test assets pack preview clamiga-check

test: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --non-interactive --load tests/run-tests.lisp

assets: clamiga-check
	$(CLAMIGA) --heap $(HEAP) --non-interactive --load tools/make-assets.lisp

# ART is the flat, front-on wall picture (any size, any depth); OUT the
# pack directory, defaulting to the active profile's own.  A world with
# more to say than one image writes its own script instead — see
# ../closure/worlds/closure/gfx/make-pack.lisp.
pack: clamiga-check
	@test -n "$(ART)" || { \
	  echo "usage: make pack ART=<facade.iff> [OUT=<dir/>]"; exit 1; }
	$(CLAMIGA) --heap $(HEAP) --non-interactive \
	  --load src/load.lisp \
	  --load tools/gen-walls.lisp \
	  --load tools/gen-pack-from-art.lisp \
	  --eval '(tale::generate-pack-from-art "$(ART)" :out $(if $(OUT),"$(OUT)",nil))'

preview: clamiga-check
	@test -n "$(PACK)" || { \
	  echo "usage: make preview PACK=<dir/> [OUT=<file.iff>]"; exit 1; }
	$(CLAMIGA) --heap $(HEAP) --non-interactive \
	  --load src/load.lisp \
	  --load tools/preview-view.lisp \
	  --eval '(tale::preview-pack "$(PACK)" "$(if $(OUT),$(OUT),preview.iff)")'
	@echo "wrote $(if $(OUT),$(OUT),preview.iff)"

clamiga-check:
	@test -x $(CLAMIGA) || { \
	  echo "clamiga not found at $(CLAMIGA)."; \
	  echo "Build it first: make host in the repo root (or set CLAMIGA=/path/to/clamiga)"; \
	  exit 1; }
