# Lambda's Tale

A Bard's Tale-style dungeon crawler written in Common Lisp for
[cl-amiga](../../../README.md).  Separate subproject: the game is pure
Lisp and runs on the host clamiga for development; the Amiga front-end
renders in an Intuition window.

## Running

Build clamiga in the repo root first (`make host`), then from this
directory:

```
make test    # run the test suite
make run     # play the demo campaign on data/cellar.map
```

Walkabout keys: `w` forward, `s` back-step (keeps facing), `a`/`d` turn,
`m` full-screen map view (`m`/`Esc` back, `f` toggles the omniscient
debug view there), `1`–`7` open that party member's character sheet
(`1`–`7` switch heroes there, `Esc` back), `S`/`L` save/load
(`tale.sav`), `q` quit.  In combat: `a` attack, `d` defend, `f` flee.

The screen is split Bard's Tale style (see
[specs/ui-and-engine.md](specs/ui-and-engine.md)): the wireframe
first-person view on the left, the active-spells strip next to it
(shield, lamp, ... — fed by `add-effect`/`remove-effect`) with a
compass rose at its foot showing the party's facing, the message
log filling the right column — newest line at the bottom, older lines
scrolling up; each message starts with `>` and long ones word-wrap
onto indented continuation lines — and the status line plus the
numbered party roster (up to 7 rows, each row's number opening its
character sheet) at the bottom.  The automap lives under `m`; levels can be
large (30x30 like Bard's Tale I, up to 128x128).  On the Amiga the
window uses the same PAL 640x256 geometry as the custom screen, so
both displays lay out identically.

On AmigaOS (the repo is mounted as `CLAmiga:` in the FS-UAE setup),
the game opens its **own screen** — nominal PAL 640x256 hires,
16 colors, picked RTG-aware through `graphics.library/BestModeIDA`
(so Picasso96/CyberGraphX/MorphOS promote it to a suitable RTG mode),
with the tile pack's palette and a borderless backdrop window;
Save/Load/Quit sit in the menu strip (right mouse button, GadTools
menus with the usual right-Amiga shortcuts):

```
cd CLAmiga:examples/games/lambda-tale
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp
```

The test suite runs on the Amiga the same way (it is not part of the
parent repo's `test-amiga` run — the game is a separate subproject):

```
cd CLAmiga:examples/games/lambda-tale
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --non-interactive --load tests/run-tests.lisp
```

On AmigaOS the suite additionally runs GUI smoke tests and two
unattended `*autoplay*` sessions (window and `:display :screen`).

For development there is also a window view on the Workbench screen
(no custom palette — the window keeps the Workbench colors):

```lisp
(tale:play-amiga "data/cellar.map" :display :window)
```

## Layout

```
src/package.lisp     package TALE
src/dice.lisp        dice notation ("2d6+1") and the scriptable *RNG*
src/ilbm.lisp        IFF ILBM image reader/writer (pure CL, ByteRun1)
src/map.lisp         dungeon map model + ASCII map parser + story layer
src/knowledge.lisp   the party's automap knowledge (explored cells, seen walls)
src/view.lisp        first-person view geometry (view cone, perspective
                     planes, backend-independent display list)
src/game.lisp        game state, movement, automap observation
src/events.lisp      engine event bus + story flags
src/party.lisp       heroes, classes, xp/levels, party queries
src/combat.lisp      monster types, round-based combat
src/specials.lisp    cell-special interpreter (the story op vocabulary)
src/save.lisp        save games (readable Lisp data, never evaluated)
src/render.lisp      ASCII automap renderer (player view + omniscient debug view)
src/render-fp.lisp   ASCII wireframe first-person renderer
src/amiga-ui.lisp    AmigaOS front-end (Intuition window, graphics.library)
src/main.lisp        host walkabout entry point
src/main-amiga.lisp  Amiga walkabout entry point
data/*.map           maps as ASCII art + story forms
data/campaign.lisp   demo campaign: hero classes, monsters, starting party
data/gfx/*.iff       the demo tile pack: wall pieces + floor/ceiling
                     ILBM assets (regenerate: make assets)
gfx-city-demo/       example custom tile pack (night-sky city palette)
tools/gen-walls.lisp procedural wall-art generator
tests/run-tests.lisp test suite (make test)
specs/               design constraints (UI layout, map scale, screens)
```

The first-person view never looks around corners (Bard's Tale rules):
`compute-view` walks the cells straight ahead, stopping at walls, doors
and the view-depth cap, and `view-display-list` flattens the result into
line/door primitives that both the ASCII renderer and the Amiga
`draw-line` renderer consume.  Walking and turning call `observe`, which
records every wall the party can currently see into the automap.

## Wall graphics

On the Amiga the first-person view is drawn with **blitted wall
graphics** (M3): every wall the view can show falls into a fixed
Bard's Tale-style screen slot (`view-blit-list` — front walls, receding
side walls, walls seen through open sides, each with a door variant, at
four depths), and each slot is filled by a pre-rendered bitmap piece.
The pieces live in `data/gfx/` as **IFF ILBM** files, loaded by the
pure-Lisp reader in `src/ilbm.lisp` and drawn by the procedural
generator in `tools/gen-walls.lisp` (`make assets` regenerates them;
the test suite compares the checked-in files pixel-for-pixel against
the generator, so art and code cannot drift apart).

The rendering path is **RTG-safe** — no chipset or planar assumptions,
so it works unchanged under Picasso96/CyberGraphX/MorphOS: pieces are
uploaded once into `AllocBitMap` bitmaps in the display's native
format (chunky pens through `WriteChunkyPixels`).  Each frame draws the
ceiling/floor backdrop first, then composites the walls back-to-front
over it — the receding side pieces **cookie-cut** through a 1-bit mask
(`BltMaskBitMapRastPort`, transparent where the piece uses pen 0), so
the backdrop shows through the corners they don't cover instead of
black wedges.  When the assets are missing the view falls back to the
wireframe renderer.

## Custom tile packs

The demo art is replaceable: a **tile pack** is a directory of IFF
ILBM files, selected per session with `play-amiga`'s `:gfx-dir`
argument (default `"data/gfx/"`, the demo pack):

```lisp
(tale:play-amiga "data/cellar.map" :display :screen
                                   :gfx-dir "gfx/city/")
```

A pack holds the 40 wall pieces plus optional extras:

- `floor.iff` / `ceiling.iff` — the Bard's Tale split backdrop: the
  ceiling fills the view above the horizon, the floor below, and the
  walls blit on top, carving the perspective (a city pack draws sky
  and street here).  Missing files leave the region black.
- `palette.iff` — any ILBM whose CMAP provides the pack's colors.

`(tale:print-tile-manifest)` prints the full contract — every
filename with its exact pixel size — so custom art can be drawn to
spec; mis-sized pieces are rejected at load time with a message
naming the file and both sizes.

The custom screen is 16 colors: **pens 0–3 are fixed UI colors**
(black, white, grey, amber — text and wireframe stay readable in any
pack), **pens 4–15 belong to the pack**, taken from `palette.iff`'s
CMAP when present, else from `front-0.iff`'s.  Pack colors need
`:display :screen` — a window on the Workbench screen keeps the
Workbench palette.

**Transparency:** in a *wall* piece **pen 0 is transparent** — the
ceiling/floor backdrop shows through it, so the receding side walls
don't stamp black wedges over the sky/floor.  Paint solid black inside
a wall (mortar, joints, door frames) with **pen 4**, not pen 0.  The
walls are composited over the backdrop with `BltMaskBitMapRastPort`
(cookie-cut, RTG-safe); the `ceiling.iff`/`floor.iff` backdrops are
opaque, so pen 0 there is plain black.

`gfx-city-demo/` is a worked example: the demo walls under a night
sky and a tan street with their own `palette.iff` (regenerate with
`gfx-city-demo/make-pack.lisp`, try it with `gfx-city-demo/run.lisp`).
See also the "Backdrop slots" and wall-art sections of
`tests/run-tests.lisp` for executable examples of the contract.

## Engine vs. story

The engine never hard-codes story facts.  It emits events (`:message`,
`:enter-cell`, `:blocked`, `:combat-start`, `:combat-end`, `:hero-died`,
`:party-defeated`, ...) that the front-end and the campaign subscribe to
with `on-event`; story state lives in flags (`set-flag`/`flag`).  A
campaign is pure data on top of the engine: hero classes
(`define-hero-class`), monsters (`define-monster`) and maps with cell
specials.  `data/campaign.lisp` plus `data/cellar.map` form the demo.

## Map format

Maps are ASCII art on a `(2W+1) x (2H+1)` character grid — see the header
of `src/map.lisp` for the exact rules and `data/cellar.map` for an example:

```
+-+-+-+
|@  | |
+ +D+ +
| |  <|
+-+-+-+
```

`-` `|` walls, `D` doors, `@` party start, other cell characters are
features (`>` stairs down, `<` stairs up by convention).  Walls are stored
per cell, so one-way (phantom) walls are expressible; maps can wrap
Bard's Tale-style (`:wrap t`).

After the art a map file may carry its story layer as Lisp data forms
(read with `*read-eval*` bound to `NIL`, never evaluated):

```lisp
(special (1 2)
  (once (message "Something stirs in the darkness...")
        (encounter ("giant rat" "1d3+1"))))
```

The op vocabulary — `message`, `set-flag`/`clear-flag`,
`when-flag`/`unless-flag`, `once`, `teleport`, `spin`, `damage`, `heal`,
`gold`, `encounter`, `event` — is documented in `src/specials.lisp`.

## Party and combat

Heroes have Bard's Tale-ish stats (str/dex/iq/con/lck, descending AC,
hit dice per class) and level up on xp thresholds.  The roster holds
up to 7 members (`join-party`): six regular heroes plus one guest slot
for a summoned monster or story NPC.  Combat is
round-based: the party declares actions (attack/defend, or try to
flee), heroes strike first, then every surviving monster swings at a
random front-rank hero.  All randomness goes through `*rng*`, so the
test suite scripts entire fights deterministically.

Save games (`save-game`/`load-game`) are a single readable Lisp form:
map file reference, position, automap knowledge, story flags and party.

The test suite (`tests/run-tests.lisp`) doubles as the executable
specification for the map model, movement, knowledge tracking,
renderers, events, specials, party, combat and save games.

## Roadmap

- **M0 (done)**: map model, movement, automap knowledge, ASCII map view,
  interactive walkabout on the host
- **M1 (done)**: wireframe first-person view (shared geometry + display
  list, ASCII and Amiga renderers, automap fed by what the party sees);
  the Amiga window front-end still needs an FS-UAE shakedown
- **M2 (done)**: events + cell-specials story layer, party and character
  system, round-based combat, save games, demo campaign
- **M2.5 (done)**: Bard's Tale screen layout (scrolling message log,
  active-spells strip, 7-slot party roster, full map under `m`),
  large maps (30x30 up to 128x128), custom screen via
  `BestModeID` (RTG-aware) — see `specs/ui-and-engine.md`
- **M3 (done)**: ILBM asset loading (`src/ilbm.lisp`), blitted wall
  graphics — RTG-aware (MorphOS / Picasso96 / CyberGraphX): no chipset
  or planar assumptions, bitmaps via `AllocBitMap`, blits through OS
  calls only; procedurally generated wall-piece assets (`make assets`)
- **M3.5 (done)**: swappable tile packs (`:gfx-dir`,
  `print-tile-manifest`), 16-color custom screen with per-pack
  palettes, ceiling/floor backdrop
- **M4 (in progress)**: the game proper, kept simple —
  numbered party roster + character sheet (`1`–`7`, **done**); next
  town (map + location menu) and shops (items + gold); then sound and
  polish.  The Bard's Tale II chrome and day/night sky are parked
  until the game content lands.
