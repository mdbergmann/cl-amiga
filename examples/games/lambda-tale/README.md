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
make run     # interactive ASCII walkabout on data/cellar.map
```

Walkabout keys: `w` forward, `s` back-step (keeps facing), `a`/`d` turn,
`m` toggle the automap between explored-only and full (debug) view,
`q` quit.  The host walkabout shows the wireframe first-person view and
the automap side by side.

On AmigaOS (the repo is mounted as `CLAmiga:` in the FS-UAE setup), the
game runs in an Intuition window:

```
cd CLAmiga:examples/games/lambda-tale
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp
```

## Layout

```
src/package.lisp     package TALE
src/map.lisp         dungeon map model + ASCII map parser
src/knowledge.lisp   the party's automap knowledge (explored cells, seen walls)
src/view.lisp        first-person view geometry (view cone, perspective
                     planes, backend-independent display list)
src/game.lisp        game state, movement, automap observation
src/render.lisp      ASCII automap renderer (player view + omniscient debug view)
src/render-fp.lisp   ASCII wireframe first-person renderer
src/amiga-ui.lisp    AmigaOS front-end (Intuition window, graphics.library)
src/main.lisp        host walkabout entry point
src/main-amiga.lisp  Amiga walkabout entry point
data/*.map           maps as ASCII art
tests/run-tests.lisp test suite (make test)
```

The first-person view never looks around corners (Bard's Tale rules):
`compute-view` walks the cells straight ahead, stopping at walls, doors
and the view-depth cap, and `view-display-list` flattens the result into
line/door primitives that both the ASCII renderer and the Amiga
`draw-line` renderer consume.  Walking and turning call `observe`, which
records every wall the party can currently see into the automap.

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

The test suite (`tests/run-tests.lisp`) doubles as the executable
specification for the map model, movement rules, knowledge tracking and
renderer.

## Roadmap

- **M0 (done)**: map model, movement, automap knowledge, ASCII map view,
  interactive walkabout on the host
- **M1 (done)**: wireframe first-person view (shared geometry + display
  list, ASCII and Amiga renderers, automap fed by what the party sees);
  the Amiga window front-end still needs an FS-UAE shakedown
- **M2**: events/specials layer, party, combat, character system, save games
- **M3**: ILBM asset loading, blitted wall graphics, custom screen
- **M4**: town, shops, sound, polish
