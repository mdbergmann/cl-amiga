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
`m` toggle the automap between explored-only and full (debug) view,
`S`/`L` save/load (`tale.sav`), `q` quit.  In combat: `a` attack,
`d` defend, `f` flee.  The host view shows the wireframe first-person
view, the automap and the party roster side by side.

On AmigaOS (the repo is mounted as `CLAmiga:` in the FS-UAE setup), the
game runs in an Intuition window; Save/Load/Quit sit in the window's
menu strip (right mouse button, GadTools menus with the usual
right-Amiga shortcuts):

```
cd CLAmiga:examples/games/lambda-tale
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp
```

## Layout

```
src/package.lisp     package TALE
src/dice.lisp        dice notation ("2d6+1") and the scriptable *RNG*
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
tests/run-tests.lisp test suite (make test)
```

The first-person view never looks around corners (Bard's Tale rules):
`compute-view` walks the cells straight ahead, stopping at walls, doors
and the view-depth cap, and `view-display-list` flattens the result into
line/door primitives that both the ASCII renderer and the Amiga
`draw-line` renderer consume.  Walking and turning call `observe`, which
records every wall the party can currently see into the automap.

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
hit dice per class) and level up on xp thresholds.  Combat is
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
- **M3**: ILBM asset loading, blitted wall graphics, custom screen —
  RTG-aware (MorphOS / Picasso96 / CyberGraphX): no chipset or planar
  assumptions, screens via `BestModeID`, bitmaps via `AllocBitMap`,
  blits through OS calls only
- **M4**: town, shops, sound, polish
