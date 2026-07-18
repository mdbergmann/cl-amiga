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
make run     # play the demo campaign (starts in the town, worlds/closure/town.map)
```

Walkabout keys: `w` forward, `s` back-step (keeps facing), `a`/`d` turn,
`m` full-screen map view (`m`/`Esc` back, `f` toggles the omniscient
debug view there), `1`–`7` open that party member's character sheet
(`1`–`7` switch heroes there, `Esc` back), `S`/`L` save/load
(`tale.sav`), `q` quit.  In combat: `a` attack, `d` defend, `f` flee.
Inside a location (a shop): `1`–`7` pick the shopping hero, `1`–`9`
buy or sell, `s`/`b` flip between the buy and sell pages, `Esc`
back/leave.

The screen is split Bard's Tale style (see
[specs/ui-and-engine.md](specs/ui-and-engine.md)): the first-person
view with the location plaque under it on the left, the message log
filling the right column — newest line at the bottom, older lines
scrolling up; each message starts with `>` and long ones word-wrap
onto indented continuation lines — with the active effects (shield,
lamp, ... — fed by `add-effect`/`remove-effect`) and a compass rose
showing the party's facing in a band at the log's foot, and the
status line plus the numbered party roster (up to 7 rows, each row's
number opening its character sheet) at the bottom.  The automap lives
under `m`; levels can be large (30x30 like Bard's Tale I, up to
128x128).  On the Amiga the window uses the same geometry as the
custom screen, so both displays lay out identically.

On AmigaOS (the repo is mounted as `CLAmiga:` in the FS-UAE setup),
the game opens its **own screen** whose geometry comes from a
**display profile** (`play-amiga`'s `:profile` argument):

- **`:lores`** (the default) — 320x256 PAL lores, **32 colors**, the
  ECS target: half the chip-RAM/DMA cost of hires and near-square
  pixels for the art.
- **`:hires`** — 640x256 PAL hires, 16 colors, the classic
  presentation with the larger 240x130 viewport.

Both are picked RTG-aware through `graphics.library/BestModeIDA` (so
Picasso96/CyberGraphX/MorphOS promote them to a suitable RTG mode),
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

On AmigaOS the suite additionally runs GUI smoke tests (both display
profiles) and four unattended `*autoplay*` sessions (window, town
shopping, `:display :screen`, and the `:hires` profile).

For development there is also a window view on the Workbench screen
(no custom palette — the window keeps the Workbench colors):

```lisp
(tale:play-amiga "worlds/closure/town.map" :display :window)
```

## Layout

```
src/package.lisp     package TALE
src/profiles.lisp    display profiles (:lores / :hires — screen geometry,
                     viewport, tile pack, layout tuning per target)
src/dice.lisp        dice notation ("2d6+1") and the scriptable *RNG*
src/ilbm.lisp        IFF ILBM image reader/writer (pure CL, ByteRun1)
src/map.lisp         dungeon map model + ASCII map parser + story layer
src/knowledge.lisp   the party's automap knowledge (explored cells, seen walls)
src/view.lisp        first-person view geometry (view cone, perspective
                     planes, backend-independent display list)
src/game.lisp        game state, movement, automap observation
src/events.lisp      engine event bus + story flags
src/party.lisp       heroes, classes, xp/levels, party queries
src/items.lisp       item types, packs and equipment
src/combat.lisp      monster types, round-based combat
src/specials.lisp    cell-special interpreter (the story op vocabulary)
src/locations.lisp   locations (shops): mechanics + shared menu model
src/save.lisp        save games (readable Lisp data, never evaluated)
src/render.lisp      ASCII automap renderer (player view + omniscient debug view)
src/render-fp.lisp   ASCII wireframe first-person renderer
src/host-ui.lisp     host front-end (interactive ASCII walkabout, PLAY)
src/amiga-ui.lisp    AmigaOS front-end (Intuition window, graphics.library)
src/main.lisp        host walkabout entry point
src/main-amiga.lisp  Amiga walkabout entry point
worlds/closure/      the demo world: town.map + cellar.map (ASCII art
                     + story forms) and campaign.lisp (hero classes,
                     monsters, items, starting party)
data/gfx/*.iff       the demo tile pack for :lores (wall pieces +
                     floor/ceiling ILBM assets; regenerate: make assets)
data/gfx-hires/*.iff the same pack drawn for the :hires viewport
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
The pieces are **IFF ILBM** files — one pack per display profile,
`data/gfx/` for `:lores` and `data/gfx-hires/` for `:hires` (each
profile's viewport dictates the piece sizes) — loaded by the
pure-Lisp reader in `src/ilbm.lisp` and drawn by the procedural
generator in `tools/gen-walls.lisp` (`make assets` regenerates both;
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
ILBM files.  A **zone declares its own pack** in its map file —
`(zone :kind :city :gfx "gfx-city-demo/")` — and travel swaps packs as
the party crosses zones (the demo town of Closure wears the city pack;
its cellar keeps the default dungeon stone).  The directory resolves
relative to the map file when the pack lives in the world directory,
else relative to the game directory (a shipped pack).  `play-amiga`'s
`:gfx-dir` argument overrides everything for the session; without
either, the active profile's pack applies (`"data/gfx/"` for
`:lores`).  A pack is drawn for one profile's viewport; a mis-sized
pack is rejected at load time and the view falls back to the
wireframe:

```lisp
(tale:play-amiga "worlds/closure/cellar.map" :display :screen
                                             :gfx-dir "gfx/city/")
```

A pack holds the 40 wall pieces plus optional extras:

- `floor.iff` / `ceiling.iff` — the Bard's Tale split backdrop: the
  ceiling fills the view above the horizon, the floor below, and the
  walls blit on top, carving the perspective (a city pack draws sky
  and street here).  Missing files leave the region black.
- `palette.iff` — any ILBM whose CMAP provides the pack's colors.

`(tale:print-tile-manifest)` prints the full contract — every
filename with its exact pixel size for the **active profile** (wrap it
in `tale:with-display-profile` for another one) — so custom art can
be drawn to spec; mis-sized pieces are rejected at load time with a
message naming the file and both sizes.

On the custom screen **pens 0–3 are fixed UI colors** (black, white,
grey, amber — text and wireframe stay readable in any pack); the
remaining pens **belong to the pack** — 4–31 on the 32-color `:lores`
screen, 4–15 on `:hires` — taken from `palette.iff`'s CMAP when
present, else from `front-0.iff`'s.  Pack colors need
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

Lambda's Tale is an **engine**; the shipped campaign is one instance of
it.  The engine never hard-codes story facts.  It emits events
(`:message`, `:enter-cell`, `:enter-zone`, `:enter-location`,
`:blocked`, `:combat-start`, `:combat-end`, `:hero-died`,
`:party-defeated`, ...) that the front-end and the campaign subscribe to
with `on-event`; story state lives in flags (`set-flag`/`flag`).  A
campaign is pure data on top of the engine: hero classes
(`define-hero-class`), monsters (`define-monster`), items
(`define-item`) and maps with cell specials.  `worlds/closure/campaign.lisp` plus
`worlds/closure/town.map` and `worlds/closure/cellar.map` form the demo.

## The world: cities, dungeons, shops

A world is a set of **zones** — ordinary map files linked by travel.
Cities and dungeons are both first-class and both just maps: a
`(zone :kind :city :title "Closure" :gfx "gfx-city-demo/")` form in
the map file says what a zone is (and, optionally, which tile pack it
wears), and the `(travel FILE [X Y] [FACING])` special op links zones
together — city gates, stairs and portals are all map data.  A world
can hold any number of cities and dungeons — every zone is its own
file, and each keeps its own map and automap knowledge alive for the
whole session; save games carry the whole world.

A **location** — a shop, or any enterable building — is the
`(location TITLE KIND ARG...)` special op on a cell.  The engine ships
shop mechanics: items are campaign data (`define-item` — weapons,
armor, shields with prices, damage dice, AC bonuses and class
restrictions), heroes carry up to 8 items and equip one weapon, armor
and shield, combat uses the equipped gear, and shops sell their
`:stock` and buy anything back at half price.  The demo town of
Closure has Wolfgar's equipment shoppe and a tavern whose trapdoor
leads down into the cellar dungeon.

### Building your own world

A world is a **directory**: map files plus a `campaign.lisp` beside
them — `play` and `play-amiga` load the campaign next to whatever map
you start, so your world brings its own classes, monsters and items,
never the demo's.  Put a shop wherever you want it, stocked however
you see fit:

```
mygame/campaign.lisp     (define-item 'rusty-dagger :kind :weapon
                           :price 5 :damage "1d4") ...
mygame/village.map       the art, then:
                         (zone :kind :city :title "Frogmorton"
                               :gfx "gfx/")   ; the world's own pack
                         (special (3 7)
                           (location "Bree's Bargains" :shop
                                     :stock (rusty-dagger torch)))
                         (special (9 2) (travel "warrens.map"))
mygame/warrens.map       (zone :kind :dungeon :title "the warrens") ...
mygame/gfx/*.iff         optional zone tile pack (see the manifest)
```

```lisp
(tale:play "mygame/village.map")
```

Everything is data read with `*read-eval*` bound to `NIL` except
`campaign.lisp`, which is a Lisp file of `define-*` calls (the demo's
`worlds/closure/campaign.lisp` is the template).

## Map format

Maps are ASCII art on a `(2W+1) x (2H+1)` character grid — see the header
of `src/map.lisp` for the exact rules and `worlds/closure/cellar.map` for an example:

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
(zone :kind :dungeon :title "the cellar")

(special (1 2)
  (once (message "Something stirs in the darkness...")
        (encounter ("giant rat" "1d3+1"))))
```

The op vocabulary — `message`, `set-flag`/`clear-flag`,
`when-flag`/`unless-flag`, `once`, `teleport`, `travel`, `location`,
`spin`, `damage`, `heal`, `gold`, `encounter`, `event` — is documented
in `src/specials.lisp`.

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
the current zone's map file, position, every visited zone's automap
knowledge, story flags and the party with packs and equipment.

The test suite (`tests/run-tests.lisp`) doubles as the executable
specification for the map model, movement, knowledge tracking,
renderers, events, specials, zones and travel, party, items, shops,
combat and save games.

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
  `print-tile-manifest`), custom screen with per-pack palettes,
  ceiling/floor backdrop; display profiles (`:profile`) — 32-color
  lo-res ECS default, 16-color hi-res alternative, each with its own
  generated pack
- **M4 (in progress)**: the game proper, kept simple —
  numbered party roster + character sheet (`1`–`7`, **done**);
  the world as first-class data: zones (`(zone ...)`, cities and
  dungeons alike), cross-map travel, locations, items and shops
  (**done** — the demo town of Closure links to the cellar);
  next sound, then polish.  The Bard's Tale II chrome and day/night
  sky are parked until the game content lands.
