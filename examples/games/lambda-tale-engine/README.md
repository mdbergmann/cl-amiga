# Lambda's Tale (engine)

A Bard's Tale-style dungeon-crawler **engine** written in Common Lisp
for [cl-amiga](../../../README.md).  Separate subproject: the engine
is pure Lisp and runs on the host clamiga for development; the Amiga
front-end renders in an Intuition window or on an own custom screen.

The engine ships no story: a **game** is a sibling directory holding
its worlds (maps + campaign) and loading the engine — the town of
[Closure](../closure/README.md) next door is the playable example.
The engine's own test suite plays a minimal fixture world
(`tests/world/`).

## Using the engine

Build clamiga in the repo root first (`make host`), then from this
directory:

```
make test    # run the engine test suite (plays tests/world/)
make assets  # regenerate the default tile packs (data/gfx*, tools/gen-walls.lisp)
```

A game loads the engine from wherever it lives and names its starting
map — the engine is **self-locating** (it finds its own sources and
default tile packs through `*load-truename*`, never through the
working directory; the working directory belongs to the game):

```lisp
(load "../lambda-tale-engine/src/load.lisp")   ; host; on AmigaOS use a
                                               ; volume path or self-locate
                                               ; via *LOAD-TRUENAME* like
                                               ; Closure's src/load.lisp
(tale:play "mygame/village.map")               ; host front-end
(tale:play-amiga "mygame/village.map")         ; AmigaOS front-end
```

The test suite runs on the Amiga the same way (it is not part of the
parent repo's `test-amiga` run — the engine is a separate subproject):

```
cd CLAmiga:examples/games/lambda-tale-engine
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --non-interactive --load tests/run-tests.lisp
```

On AmigaOS the suite additionally runs GUI smoke tests (both display
profiles) and three unattended `*autoplay*` sessions through the
fixture world.

## Layout

```
src/package.lisp     package TALE
src/profiles.lisp    display profiles (:lores / :hires — screen geometry,
                     viewport, tile pack, layout tuning per target) and
                     the self-located *ENGINE-DIR* / ENGINE-PATH
src/dice.lisp        dice notation ("2d6+1") and the scriptable *RNG*
src/ilbm.lisp        IFF ILBM image reader/writer (pure CL, ByteRun1)
src/map.lisp         dungeon map model + ASCII map parser + story layer
src/knowledge.lisp   the party's automap knowledge (explored cells, seen walls)
src/view.lisp        first-person view geometry (view cone, perspective
                     planes, backend-independent display list)
src/time.lisp        the game clock: day/night, darkness, timed effects
src/game.lisp        game state, movement, automap observation
src/events.lisp      engine event bus + story flags
src/party.lisp       heroes, classes, xp/levels, party queries
src/items.lisp       item types, packs and equipment
src/spells.lisp      spell types, spell points, casting + the cast menu
src/combat.lisp      monster types, round-based combat
src/specials.lisp    cell-special interpreter (the story op vocabulary)
src/locations.lisp   locations (shops): mechanics + shared menu model
src/save.lisp        save games (readable Lisp data, never evaluated)
src/save-menu.lisp   named saves: the saves/ dir + the slot-picker menu
src/render.lisp      ASCII automap renderer (player view + omniscient debug view)
src/render-fp.lisp   ASCII wireframe first-person renderer
src/host-ui.lisp     host front-end (interactive ASCII walkabout, PLAY)
src/amiga-ui.lisp    AmigaOS front-end (Intuition window, graphics.library)
data/gfx/*.iff       the default tile pack for :lores (wall pieces +
                     floor/ceiling ILBM assets; regenerate: make assets)
data/gfx-hires/*.iff the same pack drawn for the :hires viewport
tools/gen-walls.lisp procedural wall-art generator
tests/run-tests.lisp engine test suite (make test)
tests/world/         the minimal fixture world the suite plays (a keep,
                     a dark crypt, a 30-line campaign)
specs/               design constraints (UI layout, map scale, screens)
```

Both front-ends draw the Bard's Tale split screen (see
[specs/ui-and-engine.md](specs/ui-and-engine.md)): the first-person
view with the location plaque under it on the left, the scrolling
message log on the right (in the engine's compact 5x7 microfont on
the Amiga) with a slim strip of active-effect icons below it, laid
out in effect order — an effect granting a compass shows the live
rose in its slot — and the numbered party roster (`# CHARACTER AC
HIT PTS SPL PTS CL`, two-letter class codes) at the bottom.
Locations (shops, taverns) and the character sheet **take over the
message area**: their menu renders at the top of the log page with
the trailing log lines still scrolling underneath, while the view
column shows the location's picture or the hero's portrait when the
campaign ships one (`(location ... :image FILE)`,
`define-hero-class ... :image FILE` — resolved relative to the map
file like effect icons; without one the live first-person view stays).
A location also has a face **from the street**: facing its door puts
its facade in the view column before the party ever steps in, so a
city's houses have faces (the Bard's Tale building-front look).  An
optional `:facade FILE` names that street face; without one the
`:image` picture shows from the street too — so a location can pair
an exterior with a distinct interior, or ship one picture for both.
The cast/use/sing menus and the save picker draw as an overlay page
over the view column instead, keeping the log readable during combat.
The key reference lives on the help page under `h`/`?`.  The full
automap lives under `m` — black ink on the grey page, doors and the
party amber, with a legend beside the map listing the special places
the party has found (shops, taverns and the like, each marker also
drawn on its map cell — plain houses are scenery and carry no marker,
or a city's front doors would bury the places that matter) and a
footer showing the zone, position and game clock; maps can be large
(30x30 like Bard's Tale I, up to 128x128).  The overlay page and the
whole map page render in the microfont too, so the Amiga UI carries
one type size throughout.  The key bindings are listed in
the [Closure README](../closure/README.md).  On the Amiga the window
uses the same geometry as the custom screen, so both displays lay out
identically; the custom screen's geometry comes from a **display
profile** (`play-amiga`'s `:profile` argument):

- **`:lores`** (the default) — 320x256 PAL lores, **32 colors**, the
  ECS target: half the chip-RAM/DMA cost of hires and near-square
  pixels for the art.
- **`:hires`** — 640x256 PAL hires, 16 colors, the classic
  presentation with the larger 240x130 viewport.

### Draw distance (slower machines)

The first-person view draws up to `+view-depth+` (4) distance levels
ahead.  `play-amiga`'s **`:draw-depth`** argument (1-4) trades some of
that distance for frames on a slower machine:

```lisp
(tale:play-amiga "mygame/village.map" :draw-depth 2)
```

Each level dropped is up to three fewer wall blits per frame, and ten
fewer piece images (plus their style variants) decoded into bitmaps at
load time; the corridor then ends that much nearer, fading into the
ceiling/floor backdrop.  Note that the far levels are the *small* ones
— the win is mostly in per-blit overhead, not blitter time, so measure
on the target rather than assuming.

**A tile pack is unaffected**: it must always ship the full set for
all four depths (`print-tile-manifest` still lists every piece, and
the loader still errors on a missing one), because a pack is data that
has to work on any machine — the draw distance of whoever *built* it
must not shape it.  Draw depth decides which of those images are
*loaded*, never which must be *provided*.

It is a **rendering** cap only: the automap still records everything
the party could see, and darkness (a `(zone :dark N)` or nightfall)
still shortens the view further when it is the tighter of the two.
Each display profile carries a default, since a profile describes a
screen while draw distance tracks the CPU: `:lores` draws the full 4,
`:hires` draws 3 — it blits roughly twice the pixel area per frame,
and the deepest level spends a blit on an 8x8 far wall.  `:draw-depth`
overrides the default either way (`:draw-depth 4` buys the last level
back on a hires machine that can afford it).

Note that `:draw-depth` is the only way in: binding `tale:*draw-depth*`
around `play-amiga` has no effect, because `with-display-profile`
rebinds it from the profile on the way in.

Both profiles give the first-person view about **2/5** of the screen
and the message log the other **3/5** — the text carries the game.
The split is a profile knob, not engine code: the view column is
exactly the profile's `fp-width` and the log takes the remainder, so
a custom target with a different balance is a new profile plus a
matching tile pack (see `print-tile-manifest`).

Both are picked RTG-aware through `graphics.library/BestModeIDA` (so
Picasso96/CyberGraphX/MorphOS promote them to a suitable RTG mode),
with the tile pack's palette and a borderless backdrop window;
Save/Load/Quit sit in the menu strip (right mouse button, GadTools
menus with the usual right-Amiga shortcuts).  For development there is
also a window view on the Workbench screen (no custom palette):
`(tale:play-amiga "mygame/village.map" :display :window)`.

The whole game also **plays by mouse**: clicking the first-person view
walks (left/right quarters turn, the middle steps forward, its bottom
band steps back), clicking a roster row opens that character sheet,
clicking a menu's numbered rows or its `[s] sell`-style footer hints
acts as those keys, and the map/help/sheet pages close on a click
elsewhere.  Menu option rows carry their pick key (`menu-option` /
`menu-numbered` in `src/events.lisp`), so front-ends map clicks to
keys without parsing the text.  A menu list deeper than a page — a
big shop stock, a full pack on the sell page or the character sheet,
a fat spell book — **scrolls**: `u`/`d` (or clicking the `^ more` /
`v more` marker rows) move the window and digits pick within it, so
every item stays reachable with single-digit keys (`menu-window` in
`src/events.lisp`; the scroll walks live in the model tests in
`tests/run-tests.lisp`).  The pointer is an **open hand** that
turns into a **pointing finger** whenever it rests on something
clickable; over the first-person view's walk zones it becomes the
**arrow of the move a click would make** — left/right turn arrows on
the side quarters, an up arrow on the middle, a down arrow on its
back-step band (all campaign-replaceable — see `pointer.iff` and
friends under "Custom tile packs"),
and a busy hourglass shows during the loads that take real seconds at
14MHz: tile packs, save games, first-sight location pictures and
icons.  See the hotspot tests in `tests/run-tests.lisp` (`amiga-ui
autoplay drives the game by mouse clicks`) for the full contract.

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

The default art is replaceable: a **tile pack** is a directory of IFF
ILBM files.  A **zone declares its own pack** in its map file —
`(zone :kind :city :gfx "gfx/")` — and travel swaps packs as the
party crosses zones.  The directory resolves relative to the map file
when the pack lives in the world directory, else relative to the game
directory (a pack the game ships beside its worlds).  `play-amiga`'s
`:gfx-dir` argument overrides everything for the session; without
either, the active profile's pack applies (the engine's own
`data/gfx/` for `:lores`).  A pack is drawn for one profile's
viewport; a mis-sized pack is rejected at load time and the view
falls back to the wireframe.

The generator ships two wall styles: the dungeon brick the default
packs are drawn with (`draw-wall-piece`), and a **city house style**
(`draw-city-wall-piece` in `tools/gen-walls.lisp`) that renders every
piece as a timber-framed house — thatch roof band, plaster with dark
framing, lit amber windows, stone foundation — so a city street reads
as rows of houses, Bard's Tale style.  A city pack using it must
carry `*house-colors*` in pens 7–9 of its palette; Closure's
`worlds/closure/gfx/make-pack.lisp` is the worked example.

A pack holds the 40 wall pieces plus optional extras:

- `front-0-v1.iff`, `side-2-l-v2.iff`, ... — **per-building style
  variants** of any wall piece, probed in order (`-v1`, `-v2`, ...)
  until one is missing.  The view deals them out deterministically
  **per building** — one walled-in mass of cells is one house and
  wears one look all the way along its front — so a street reads as
  a row of *different* houses instead of one repeated front; a
  `(location ... :style N)` op anywhere in the mass pins that
  building's look explicitly (Closure matches each block's street
  pieces to its houses' facade pictures this way).  A pack
  ships as many looks — for as many pieces — as it wants to pay
  load time for: variants are per-piece, so trimming the far
  depths' files is a valid budget cut.  `draw-city-wall-piece`'s
  `:style` argument (`:timber`/`:stone`/`:townhouse`) draws the
  engine's three house looks.
- `floor.iff` / `ceiling.iff` — the Bard's Tale split backdrop: the
  ceiling fills the view above the horizon, the floor below, and the
  walls blit on top, carving the perspective (a city pack draws sky
  and street here).  Missing files leave the region black.  The
  default packs paint the floor as one flat color and the ceiling as
  solid distance bands darkening toward the horizon, split at the
  perspective-plane rows so each band lines up with a corridor depth —
  a pack can use the same trick, since the bands sit at fixed screen
  rows.
- `palette.iff` — any ILBM whose CMAP provides the pack's colors.  Only
  the pens a pack owns are read from it (see
  [the pen contract](#the-pen-contract)); entries for the engine's pens
  are ignored, so a stale palette cannot recolor the UI or the monsters.
- `pointer.iff` / `pointer-click.iff` / `pointer-forward.iff` /
  `pointer-back.iff` / `pointer-turn-left.iff` /
  `pointer-turn-right.iff` — optional mouse-pointer art: the neutral
  pointer, the one shown over a click target, and the four
  directional cursors shown over the first-person view's walk zones.
  At most 16 pixels wide (a hardware sprite), pens 0–3 only — pen 0
  transparent, pens 1–3 show as screen colors 17–19, taken from
  `pointer.iff`'s CMAP entries 1–3 (the sprite has one palette; all
  pointers share it).  The hot spot is the topmost-leftmost inked
  pixel.  Missing files get the engine's built-in art — an open hand,
  a pointing finger and four arrows (`*hand-pointer-art*` and
  friends in `src/ilbm.lisp`).

`(tale:print-tile-manifest)` prints the full contract — every
filename with its exact pixel size for the **active profile** (wrap it
in `tale:with-display-profile` for another one) — so custom art can
be drawn to spec; mis-sized pieces are rejected at load time with a
message naming the file and both sizes.

### The pen contract

The game runs on one screen with **one palette**, and walking into
another zone swaps the tile pack under it.  A bitmap, though, is
nothing but pen indices — so an image loaded in one zone and still
cached in the next re-colors the moment the new pack's CMAP lands.
The screen's pens are therefore split in two, and
[`src/palette.lisp`](src/palette.lisp) is where the line is drawn:

| pens | owner | |
|---|---|---|
| 0 | engine | transparent key in wall pieces |
| 1–3 | engine | UI white, grey, amber |
| 4 | engine | opaque black |
| 5–6 | **pack** | sky / ceiling, ground / floor |
| 7–16 | **pack** | art |
| 17–19 | engine | mouse-pointer sprite registers |
| 20–23 | **pack** | art |
| 24–31 | engine | the shared **figure core** |

Engine pens hold the same color in every zone.  Pack pens are the
pack's own and change under the player's feet on zone travel, which is
the point: the night street and the cellar are the same pens in
different colors.  Only pack pens are loaded from a pack's
`palette.iff` — a stale or hand-edited palette therefore cannot recolor
the UI text, the mouse pointer, or the monsters standing in front of
its walls.

**A fixed pen is shared, not lost.**  A wall may paint in the figure
core freely — the quantizer is offered it — it just cannot *re-color*
it.  So a pack has 16 pens of its own plus 12 more to draw with.

`:hires` is 16 colors, has no pen 24, and so has no figure core and no
pointer pens: 0–4 engine, 5–15 pack, exactly as before.  It is a
**wall-pack target only**.  `:lores` is where the contract lives and
where new art should be drawn.

Pack colors need `:display :screen` — a window on the Workbench screen
keeps the Workbench palette.

**Transparency:** in a *wall* piece **pen 0 is transparent** — the
ceiling/floor backdrop shows through it, so the receding side walls
don't stamp black wedges over the sky/floor.  Paint solid black inside
a wall (mortar, joints, door frames) with **pen 4**, not pen 0.  The
walls are composited over the backdrop with `BltMaskBitMapRastPort`
(cookie-cut, RTG-safe); the `ceiling.iff`/`floor.iff` backdrops are
opaque, so pen 0 there is plain black.

The Closure game ships two worked examples, both declared map-relative
and both regenerated by a `make-pack.lisp` beside the art:
[`worlds/closure/gfx/`](../closure/worlds/closure/gfx) — the default
walls under a night sky and a tan street — and
[`worlds/closure/gfx-cellar/`](../closure/worlds/closure/gfx-cellar) —
the same walls over a packed-earth floor and a ceiling that darkens to
black, for the cellar below the tavern.  See also the "Backdrop slots"
and wall-art sections of `tests/run-tests.lisp` for executable
examples of the contract.

### Packs from one hand-drawn facade

Drawing 40 pieces to spec by hand is a lot of work, and the procedural
generators only make the looks they were coded for.  The third route is
`tools/gen-pack-from-art.lisp`: give it **one flat, front-on picture of
a wall** and it derives the whole pack.

```
make pack ART=art/house.iff OUT=data/gfx-town/
make preview PACK=data/gfx-town/ OUT=street.iff
```

The source can be an IFF ILBM of any size and any depth — indexed art
is expanded through its CMAP, 24/32-bit "deep" ILBMs are read directly
— or a **binary PPM (P6)**, the bridge for art that was never an Amiga
file:

```
ffmpeg -i house.png -pix_fmt rgb24 house.ppm
```

The format is sniffed, not guessed from the name.  `write-deep-ilbm`
turns any of them back into a 24-bit IFF, so art that arrived as a PNG
becomes a source you can keep editing in DPaint.

The natural size is the viewport (120×112 at `:lores`, 240×130 at
`:hires`), which is exactly one wall cell at the nearest plane, so
every piece comes out of a downscale.  The perspective is pure
geometry: front and flank slots are rectangles cut at the front slot's
scale, side slots are the trapezoid between two perspective planes with
the wall compressed into each column's visible span and the
ceiling/floor corners left transparent.

**More than one house.**  `:variants` takes further wall pictures, each
a whole extra look, written as the `-v1`, `-v2`, … files the view deals
out *per building* — so the street becomes a row of different houses
rather than one facade repeated.  `:style N` on a location pins a
building's look, counting the base source as 0:

```lisp
(generate-pack-from-art "art/house-1.iff"
  :out "worlds/closure/gfx/"
  :variants '("art/house-2.ppm" "art/house-3.ppm" "art/house-4.ppm")
  :pictures '(("art/house-1.iff" . "house-1.iff")))
```

Because a pack is **one shared CMAP** and pictures blit with the live
screen palette rather than their own, every source above — base,
variants and pictures — is quantized *together*, which is what makes a
shop's takeover art belong to the same street it stands in.  It also
means the pens are a budget: four looks sharing 22 colors get noticeably
less each than one look with all of them.

The pen layout follows [the contract above](#the-pen-contract), with
the pointer's pens and the figure core held back — **14 art colors at
`:lores`**, 9 at `:hires`.  The core is not a loss on top of that: the
quantizer matches against those eight guaranteed colors too, it just
may not redefine them.

Quantization works on the **12-bit grid the screen can actually show**
(`set-rgb4`, four bits a channel, on RTG as much as on ECS), and no two
art pens are allowed to land on the same screen color or duplicate a
fixed one — otherwise pens are spent on colors the machine cannot tell
apart.

Every pack gets a **`palette.gpl`** beside its `palette.iff`: the same
colors as a GIMP palette, which GIMP, Aseprite, Krita and Inkscape all
import, each entry named with its pen number and role.  That closes the
loop — draw the next house *against* the pack's palette, then pass the
pack's `palette.iff` back as `:palette-source` and quantization becomes
a lossless lookup instead of a re-derivation:

```lisp
(generate-pack-from-art "art/house-5.ppm"
  :out "worlds/closure/gfx/"
  :palette-source "worlds/closure/gfx/palette.iff")
```

Every piece kind has a `-door-` twin.  By default both come from the
same source, which suits a dense street whose facade already has a door
in it; pass `:door-source` to give the door pieces their own art as
soon as the player needs to *see* which walls can be entered.

`tools/preview-view.lisp` composites a pack the way the Amiga front end
does — same blit list, same order, same cookie-cut rule — and hands
back an ILBM, so art can be judged on the host without booting an
emulator.  `make preview` renders a fixture street that shows every
piece kind at more than one depth.  It is a preview, not a second
renderer: where it and the Amiga disagree, the Amiga is right.

The art-pack tests in `tests/run-tests.lisp` are the executable spec —
deep-ILBM reading, the box filter, median cut, the pen contract, and a
whole pack built, reloaded through the loader's own size checks and
composited.

### Figures: art that travels between zones

A monster sprite, a hero portrait or an effect icon is **not part of
any pack**.  It is cached by the path it was loaded from and keeps
rendering after the player walks into a zone with a different palette,
so it must be drawn in pens no pack can move: **pen 0** for
transparency, plus **1–4 and 24–31** — twelve solid colors and a
cookie-cut key, which is a Bard's Tale bestiary's worth.

`generate-figure` is the build step, and it *enforces* that rather
than trusting it:

```lisp
(tale::generate-figure "art/skeleton.png" "worlds/closure/gfx/skeleton.iff"
                       :transparent '(255 0 255))
```

`:transparent` names the source color meaning "nothing here"; those
pixels become pen 0.  Every other pixel is quantized into the figure
palette, and the written image is then audited pen by pen — a pixel on
a pack pen is an **error** naming the file, the pen, its role and the
coordinate.  The check runs on the host, where a per-pixel scan is
free; a 68020 doing it at load time would not be.

The figure core is eight hand-picked constants, deliberately **not**
derived from the art that uses it: a core computed over the bestiary
would mean monster #17 changes the core and every pack in every world
goes stale.  Fixed means a new monster is a new file and nothing else
moves.  Given white, grey, amber and black come free from the UI pens,
the core covers what hangs off a figure — a three-step flesh ramp
(also wood and leather), two steels (armour and cold shadow), a red
and a green (cloth, blood, scales, slime), and a bone highlight.

Every pack's `palette.gpl` marks each entry `[FIXED]` or as the pack's
own, so this contract is visible in the file an artist opens.

### How a pack loads

Pack art is plain IFF ILBM — planar, ByteRun1-compressed, exactly what
Deluxe Paint and friends read and write, so pieces can be edited in
any Amiga paint program.

On the Amiga the pieces load **without ever becoming chunky**: an ILBM
plane row and an Amiga BitMap plane row have the same layout, so the
rows are poked straight into a scratch planar BitMap and the blitter
moves them into the piece's own display-format bitmap
(`tale:*wall-load-planar*`, on by default; bind it to `NIL` for the
portable chunky path through `WriteChunkyPixels`).  The cookie-cut
mask comes from the same rows — for the usual pen-0 transparent key it
is just the OR of the planes, folded once and reused both to decide
whether a piece needs a mask at all and as the chip-RAM mask plane.
Location pictures, portraits and effect icons load through the same
planar recipe.  All the per-byte and per-row work runs at C speed on
clamiga: the file arrives through the bulk `read-sequence` path, the
whole `BODY` is decoded in a single `ext:unpack-byterun1` call, each
plane's rows are gathered out of that interleaved buffer with one
`ext:copy-rows` call per plane (the pure-CL loops are kept as the
portable fallbacks), and the mask fold is a `map-into #'logior` over
the packed plane bytes.

`read-ilbm` (chunky pens) remains the general reader and is what the
host renderer, the pointer sprites and the asset generator use;
`read-ilbm-planar` is the Amiga load path.  The two are cross-checked
pen for pen in `tests/run-tests.lisp`, and the Amiga suite blits a
loaded piece back off the screen to confirm it carries the pens its
ILBM declares.

### Keeping packs loaded

Loading a pack decodes a directory of ILBMs into offscreen bitmaps —
real seconds on a 14MHz 68020 — so a zone boundary between two
packs (a town and its cellar) pays for the swap each way.
`tale:*gfx-cache-packs*` trades memory for that time by keeping the
pack the party just left, so walking back is instant:

| value | behavior |
|---|---|
| `0` | never cache; reload on every swap (smallest footprint) |
| `N` | keep up to N inactive packs, least-recently-used evicted first |
| `:auto` (default) | keep one, but drop the cache when free memory falls below `tale:*gfx-cache-min-free*` (1 MB) |

A `:lores` pack is roughly 40K of bitmaps plus 8K of chip-RAM masks,
so `:auto` caches freely on a big machine and reloads on a small one.
The cache lives inside a `play-amiga` session and is freed with it.
Set it in a campaign, or bind it around the call:

```lisp
(let ((tale:*gfx-cache-packs* 0))          ; a tight machine: never cache
  (tale:play-amiga "worlds/closure/town.map"))
```

## Engine vs. story

Lambda's Tale is an **engine**; a game is one instance of it.  The
engine never hard-codes story facts.  It emits events (`:message`,
`:enter-cell`, `:enter-zone`, `:enter-location`, `:blocked`,
`:combat-start`, `:combat-end`, `:hero-died`, `:party-defeated`, ...)
that the front-end and the campaign subscribe to with `on-event`;
story state lives in flags (`set-flag`/`flag`).  A campaign is pure
data on top of the engine: hero classes (`define-hero-class`),
monsters (`define-monster`), items (`define-item`), spells
(`define-spell`) and maps with cell specials.  The
[Closure game](../closure/README.md) is the shipped example; the
engine suite's `tests/world/` is the minimal one.

## The world: cities, dungeons, shops

A world is a set of **zones** — ordinary map files linked by travel.
Cities and dungeons are both first-class and both just maps: a
`(zone :kind :city :title "Frogmorton" :gfx "gfx/")` form in the map
file says what a zone is (and, optionally, which tile pack it wears),
and the `(travel FILE [X Y] [FACING])` special op links zones
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
`:stock` and buy anything back at half price.  Gear is managed from
the character sheet: `e` opens the hero's **gear page**, where a digit
puts a pack item on or takes it off again; items a hero's class cannot
use are marked `(unfit)` there, on the sheet and in the shop — the
shop still sells them (another hero may carry them), the marker just
warns before the gold is gone.  An item can also be
**usable** (`:use` — a torch, a potion): using it (`u`, the use menu)
heals a chosen hero or installs a timed effect from the same
vocabulary spells speak, a `:consumed` item is spent on use, and
`:image` gives the effect its band icon.  See the "Usable items" test
section of `tests/run-tests.lisp` for the exact rules.

A location may also name a **picture** — `(location ... :image
"gfx/shop.iff")` — shown in the view column while its menu is up,
plus an optional street-facing **facade** — `:facade
"gfx/house-0.iff"` — shown instead of the `:image` when the party
faces the location's door from outside (see the "Facades from the
street" test section); and a hero class a **portrait** —
`(define-hero-class ... :image "gfx/hero-warrior.iff")` — shown
beside the character sheet.  All of them resolve relative to the map
file, like effect icons and zone tile packs, so a world directory
carries its own art.
`tools/gen-walls.lisp` draws placeholder scenes and portraits
(`draw-location-scene`, `draw-portrait`); Closure's
`worlds/closure/gfx/make-pack.lisp` shows how a world generates and
ships them.

### Building your own world

A world is a **directory**: map files plus a `campaign.lisp` beside
them — `play` and `play-amiga` load the campaign next to whatever map
you start, so your world brings its own classes, monsters and items.
Put a shop wherever you want it, stocked however you see fit:

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
`campaign.lisp`, which is a Lisp file of `define-*` calls (Closure's
`worlds/closure/campaign.lisp` is the template; the engine suite's
`tests/world/campaign.lisp` is the smallest possible one).

## Map format

Maps are ASCII art on a `(2W+1) x (2H+1)` character grid — see the header
of `src/map.lisp` for the exact rules and `tests/world/crypt.map` for a
small example:

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
(zone :kind :dungeon :title "the cellar" :dark 3)

(special (1 2)
  (once (message "Something stirs in the darkness...")
        (encounter ("giant rat" "1d3+1"))))
```

The `zone` form's keywords are `:kind`, `:title`, `:wrap`,
`:start-facing`, `:gfx` (the tile pack), `:dark` (see above), and
`:sky` / `:ground` — the outdoor noon colours the day-band tint works
from (see "The day-bands and the sky").

The op vocabulary — `message`, `set-flag`/`clear-flag`,
`when-flag`/`unless-flag`, `at-night`/`at-day`, `once`, `teleport`,
`travel`, `location`, `spin`, `damage`, `heal`, `gold`, `encounter`,
`event` — is documented in `src/specials.lisp`.

The character-by-character art parse scales with map area (a 30x30
city costs real seconds at 14MHz), so a successful parse writes a
binary sidecar next to the map (`town.map` → `town.mapc`) holding the
art-derived grid; while the sidecar is newer than the map it loads in
one bulk read, and the story forms are read from the map file itself.
Editing the map transparently reparses and rewrites the sidecar —
delete `.mapc` files freely, they are pure cache (and the format is
big-endian, so a sidecar written on the host works on the Amiga).
See the map-cache tests in `tests/run-tests.lisp` for the contract.

## Time, day and night

The game has a clock (shown in the map view's footer): every step, turn and
combat round costs a minute, a fresh game starts at day 1, 08:00, and
daylight runs 06:00–20:00 — `:sunrise`/`:sunset` events fire at the
boundaries and the `at-night`/`at-day` special ops make map encounters
time-dependent.

**Darkness and light.**  A zone declared `(zone ... :dark t)` — a
dungeon or cellar — is **completely dark**: the party sees (and maps)
one cell ahead until a light effect burns.  `:dark N` (a positive
integer) keeps it dark but grants N cells of sight there.  **Outdoors at
night** there is no sun but there is a moon: sight falls to
**`*moonlight-depth*`** cells (a few — dimmer than the daytime
`+view-depth+`, but not the blind one of the underground).  A light
effect always restores the full depth.  `*moonlight-depth*` is a plain
special (default 3, capped at `+view-depth+`); set it to 1 for
pitch-black nights.  Active effects can carry durations on the clock and
wear off with a message.  See the "Game time" sections of
`tests/run-tests.lisp` for the exact rules.

### The day-bands and the sky

The clock also names five **bands** of the day — morning, noon,
afternoon, evening, night — and the map view prints the current one
("It's Morning.").  Each band turn is **announced in the message log**
("The sun rises.", "The sun climbs high.", "The afternoon wears on.",
"Dusk gathers.", "Night falls." — see `*time-band-messages*`), so the
day reads as passing whether the party walks or stands.  Outdoors, the
first-person **sky and ground take a different colour in each band**: a
bright blue that lifts toward dawn, softens through the afternoon, warms
at dusk and sinks to near-black at night.  It is a palette-only effect —
two colour registers reloaded when the band turns (`:time-band` event),
no new art and no redraw — so it is free even on a 14 MHz 020.

Every zone can declare its own colours:

```lisp
(zone :kind :city :title "Closure" :gfx "gfx/"
      :sky (102 170 204) :ground (110 96 74))
```

`:sky` and `:ground` are `(R G B)` triples (a `#(...)` vector works
too) giving the zone's **noon** colour; the engine derives the other
bands by tinting that base, so a zone that paints a red alien sky still
goes dark at nightfall.  A zone that declares neither uses the engine
defaults (`*default-sky*` / `*default-ground*`).  Indoor zones — any
`(zone ... :dark ...)` — are left alone: there is no sky underground,
so the cellar keeps its own stone colours whatever the hour.  The tint
tables (`*sky-band-tints*` / `*ground-band-tints*`) and the pure
`sky-color-for` / `ground-color-for` functions live in
`src/palette.lisp`; the "Day-time sky and ground colour" section of
`tests/run-tests.lisp` is the spec.

### The living-world clock (time passes while you stand)

Classic Bard's Tale moves the clock only on an action, so standing
still freezes the sky.  The Amiga front-end instead **drips time forward
while the party stands idle** in free exploration (not in combat, a
location, a menu, or the map/help pages), on the window's idle
heartbeat: the sky cycles, casters slowly regain magic outdoors, and
timed effects burn down whether or not you walk.  It is the same
`advance-time`, so every consequence — spell-point regen, effect
expiry, `:sunrise`/`:sunset`, `:time-band` — fires exactly as a step's
would.

The pace is the special variable **`*idle-clock-rate*`** — game-minutes
per real second — read on every tick, so a campaign, a display profile
or the REPL can rebind or disable it live:

```lisp
(setf tale:*idle-clock-rate* 4)    ; brisk (default): a day in ~6 real min
(setf tale:*idle-clock-rate* 1)    ; ambient: a day in ~24 real min
(setf tale:*idle-clock-rate* 20)   ; demo: a day in ~72 real sec
(setf tale:*idle-clock-rate* nil)  ; off: classic, time only moves on an action
```

The whole-minute/remainder arithmetic (`idle-minutes-elapsed` /
`idle-minutes-cost`, driven off `get-internal-real-time` so the pace is
independent of tick jitter and no sub-minute time is lost) is host
tested in the "living-world idle clock" section of
`tests/run-tests.lisp`.

## Party and combat

Heroes have Bard's Tale-ish stats (str/dex/iq/con/lck, descending AC,
hit dice per class) and level up on xp thresholds.  The roster holds
up to 7 members (`join-party`): six regular heroes plus one guest slot
for a summoned monster or story NPC.  Combat is
round-based, Bard's Tale style: every living hero picks an action in
turn on the **round-orders page** (attack, defend, cast a spell, play
a song; `Esc` undoes the previous pick, `f` flees party-level), then
the round runs — heroes strike first, then every surviving monster
swings at a random front-rank hero.  Each round opens with a
`-- Round N --` line and its transcript plays out one message at a
time; `+`/`-` set the pace (5 speeds, from a second per line to
instant).  All randomness goes through
`*rng*`, so the test suite scripts entire fights deterministically.

## Spells

Spells are campaign data (`define-spell`); the engine knows the
mechanics: casters (`define-hero-class ... :caster t`) carry **spell
points** (2 per level plus the IQ bonus) and pay them per cast, and a
spell has exactly one engine-interpreted effect — damage (combat only,
strikes the melee target), heal (one chosen hero), a timed party AC
buff, timed light (the answer to darkness), or a timed compass:
Bard's Tale style, the party only sees which way it faces — the rose
in the effect strip, the facing in the map footer — while a compass
effect burns.  A timed spell may name an `:image`, the icon the
effect strip shows for it.  Spell points trickle back Bard's Tale
style while walking outdoors in daylight.  The "Spells" test section
of `tests/run-tests.lisp` is the executable specification.

## Bard songs and taverns

Songs are campaign data too (`define-song`): a song is always a timed
effect from the same vocabulary (AC buff, light or compass, with a
`:duration` and an optional `:image`).  Singers (`define-hero-class
... :singer t`) pay **tunes** — one charge per song, one charge per
level when rested — and only **one song plays at a time**: striking up
a new one displaces the old, the Bard's Tale rule.  `p` opens the sing
menu, and in combat `(:sing SONG)` is a party action beside attacking
and casting.  Tunes come back with a drink at a **tavern** — a
`(location TITLE :tavern :price N)` map special; a tavern may also
hold the way below (`:down FILE`, the trapdoor to the cellar).  The
"Bard songs" test section of `tests/run-tests.lisp` is the executable
specification.

## Save games

Save games (`save-game`/`load-game`) are a single readable Lisp form:
the current zone's map file, position, the game clock, active effects,
every visited zone's automap knowledge, story flags and the party with
packs and equipment.  Up to 9 **named saves** live side by side as
`saves/NAME.sav`: both front-ends share the same slot picker (`S`/`L`,
and the Save/Load menu items on the Amiga) — pick a slot by number or
type a new name (refused once 9 slots exist, so every slot stays
reachable by its digit) — and saving is refused during combat.

The test suite (`tests/run-tests.lisp`) doubles as the executable
specification for the map model, movement, knowledge tracking,
renderers, events, specials, zones and travel, party, items, shops,
combat and save games.

## Debug log

`(tale:debug-log-enable)` opens a timestamped trace file
(`tale-debug.log` by default, or pass a path) and
`(tale:debug-log-disable)` closes it again; setting the environment
variable `TALE_DEBUG_LOG` (a path, or `1` for the default) enables it
as the engine loads.  While enabled, the engine logs every image, map
and campaign load with its duration, every emitted event with its
handler count, and every key press — each line wall-clock timestamped
with a millisecond fraction and flushed immediately, so a session that
crashes still leaves the trace up to the moment it died.  Off by
default and free when off.  Game code can write its own lines with
`(tale:dlog "..." args...)` and time a block with `tale:dlog-timed`;
see the debug-log section of `tests/run-tests.lisp` for usage.

The log doubles as a launch profiler: the engine loader logs each
source file's load time and a `launch -> loaded` summary, and
`play-amiga` marks `new-game`, `display open` and `first frame up`,
each with milliseconds since clamiga started (`get-internal-real-time`
counts from process launch).  Together with clamiga's `--boot-log`
(the pre-engine runtime/CLOS boot phases) one debug-log trace shows
where every second between launch and the first rendered frame goes —
see "Load times and memory" in the Closure README for measured
results.

## Roadmap

- **M0 (done)**: map model, movement, automap knowledge, ASCII map view,
  interactive walkabout on the host
- **M1 (done)**: wireframe first-person view (shared geometry + display
  list, ASCII and Amiga renderers, automap fed by what the party sees)
- **M2 (done)**: events + cell-specials story layer, party and character
  system, round-based combat, save games
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
- **M4 (in progress)**: the game proper — now the separate
  [Closure](../closure/README.md) subproject — plus engine support as
  it needs it: zones/travel/shops (**done**), day/night and darkness
  (**done**), spells (**done**), named saves (**done**); next sound,
  then polish.  The Bard's Tale II chrome and the day/night sky art
  are parked until the game content lands.
