# Closure

A Bard's Tale-like game for [cl-amiga](../../../README.md), built on
the [Lambda's Tale engine](../lambda-tale-engine/README.md) in the
sibling directory.  The party starts at the south gate of the town of
Closure — a 30x30 walled city on a Skara Brae-style street grid, with
a plaza at its center and dozens of houses to step into.  The streets
render as rows of houses with lit windows in three looks — timber-
framed, stone cottage, tall townhouse (the city pack's wall-piece
style variants, dealt out per building) — so no two neighbors need
look alike.  Facing a house's door shows its facade in the view
column, and stepping inside shows the matching interior (hearth room,
workshop or study); a house's street pieces, facade picture and
interior all agree on its type.  Wolfgar's equipment shoppe and
the tavern flank the gate, and the tavern's trapdoor drops into the
cellar dungeon below — a dark zone, so buy a torch or let Zzgo the
conjurer light a mage flame.

The game is pure data on top of the engine — see
`worlds/closure/campaign.lisp` (hero classes, spells, items,
monsters, the starting party) and the story layer inside
`worlds/closure/town.map` / `cellar.map`.  It doubles as the worked
example for building your own world ("Building your own world" in the
engine README).

## Running

Build clamiga in the repo root first (`make host`), then from this
directory:

```
make test      # run the game's test suite (world, campaign, autoplay)
make run       # play (host ASCII walkabout)
make run-amiga # play in FS-UAE: cross-compile and boot straight into
               # the game (./run-amiga.sh; quitting the game closes
               # the emulator — HEAP/STACK/CONFIG=lowend overridable)
```

`DEBUG=1 ./run-amiga.sh` traces the emulated session to `tale-debug.log`
in this directory (the engine's `TALE_DEBUG_LOG`, see the engine
README): image, map and campaign loads with their durations, every
event with its handler count, every key press and click.  Lines are
flushed as they are written, so the log survives a crash and can be
tailed from another shell while the game runs; `DEBUG=ram:t.log` puts
it elsewhere on the Amiga side instead.

On AmigaOS (the repo is mounted as `CLAmiga:` in the FS-UAE setup) the
game opens its **own screen** whose geometry comes from a display
profile (`:lores` 320x256/32-color by default, `:profile :hires` for
640x256/16 colors — see the engine README):

```
cd CLAmiga:examples/games/closure
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --load src/main-amiga.lisp
```

The test suite runs on the Amiga the same way (it is not part of the
parent repo's `test-amiga` run — the game is a separate subproject);
on AmigaOS it adds unattended `*autoplay*` sessions through the real
event loop:

```
cd CLAmiga:examples/games/closure
stack 128000
CLAmiga:build/amiga/clamiga --heap 8M --non-interactive --load tests/run-tests.lisp
```

For development there is also a window view on the Workbench screen
(no custom palette — the window keeps the Workbench colors):

```lisp
(tale:play-amiga "worlds/closure/town.map" :display :window)
```

## Load times and memory

Measured launch-to-first-frame on the FS-UAE low-end baseline (the
`CONFIG=lowend` A1200: cycle-exact 68020, warm FASL cache; the 28 MHz
column is the same machine with an accelerator's clock —
`uae_cpu_multiplier = 8` in the FS-UAE config):

| phase | 14 MHz | 28 MHz |
|---|---|---|
| clamiga boot (runtime + CLOS) | 21 s | 10 s |
| engine sources (25 cached FASLs) | 43 s | 21 s |
| campaign + town map + new game | 6 s | 3 s |
| screen open, menus, layout | 7 s | 4 s |
| town tile pack (43 ILBMs) | 32 s | 16 s |
| chrome + first frame | 8 s | 4 s |
| **first frame** | **~2 min** | **~1 min** |

Every phase scales with the CPU clock — the load path is compute-bound,
so faster storage changes nothing and a faster CPU changes everything.
The first launch after editing a source file additionally recompiles
that file into the FASL cache (about 47 s for `map.lisp`, 3.5 min for
`amiga-ui.lisp` at 14 MHz — paid once per edit, not per launch).

The game runs in a **4 MB heap** (`HEAP=4M ./run-amiga.sh`), including
the trip through the trapdoor — both zones and the second tile pack —
with load times identical to the default `--heap 8M`: tile packs and
other bitmaps live in OS graphics memory, not the Lisp heap, so the
heap carries only the runtime and the world state.

The numbers come from the engine's debug log (`DEBUG=1`, which stamps
`first frame up [launch+N ms]` and per-phase durations) together with
clamiga's `--boot-log`; see the engine README's debug-log section.

## Keys

Walkabout: `w` forward, `s` back-step (keeps facing), `a`/`d` turn,
`m` full-screen map view (`m`/`Esc` back, `f` toggles the omniscient
debug view there; the footer shows the zone, position and game
clock), `h` or `?` the help page with this key reference (`h`/`Esc`
back), `1`–`7` open that party member's character sheet
(`1`–`7` switch heroes there, `e` opens the hero's gear page — the
digits put a pack item on or take it off again, and items the hero's
class cannot use are marked `(unfit)` there and in the shops —
`g` pools the party's gold onto the hero, `Esc` back), `c` cast a
spell (pick the
caster, the spell and — for a heal — the target by number, `Esc`
backs out), `u` use an item — a torch, a potion — the same way (pick
the user, the item and, for a heal, the target), `p` play a bard song
(pick the singer and the song; one song plays at a time),
`Shift-S`/`Shift-L` open the save/load slot picker (`1`–`9` pick a
slot, `n` types a new save name, `Esc` cancels; saves live as
`saves/NAME.sav`), `q` quit.  In combat every living hero picks an
action in turn on the round-orders page — `a` attack, `d` defend,
`c` cast, `p` play, `Esc` undo the previous pick — `f` flees
(party-level) and `+`/`-` set the speed of the round transcript,
which plays out one message at a time.  Inside a shop:
`1`–`7` pick the shopping hero, `1`–`9` buy or sell, `s`/`b`
flip between the buy and sell pages, `g` pools the party's gold onto
the shopper, `Esc` back/leave.  Inside the
tavern: `1`–`7` buy that hero a drink (a singer's tunes come back),
`d` down the trapdoor, `Esc` leave.  A menu list deeper than a page —
Wolfgar's full stock, a packed sell page, a long character sheet —
scrolls: `u`/`d` (or clicking the `^ more`/`v more` rows) move the
window and the digits pick within it.  On the Amiga
the Save/Load/Quit menu strip sits under the right mouse button
(GadTools menus with the usual right-Amiga shortcuts), and everything
above also works by left click: the view walks (left/right quarters
turn, the middle steps forward, its bottom band back), roster rows
open the character sheets, menu rows and their `[s] sell`-style
footer hints pick, and the map/help/sheet pages close on a click
elsewhere.

## Layout

```
src/load.lisp        loads the engine from ../lambda-tale-engine
src/main.lisp        host entry point (make run)
src/main-amiga.lisp  AmigaOS entry point
worlds/closure/      the world, one self-contained directory:
                     town.map + cellar.map (ASCII art + story forms;
                     the 30x30 town is GENERATED — edit and rerun
                     gen-town.lisp, never town.map itself),
                     campaign.lisp (hero classes, spells, items,
                     monsters, starting party), gfx/ — the town's
                     tile pack (night-sky city palette), the worked
                     example of the engine's tile-pack contract, plus
                     the shoppe/tavern pictures and the class
                     portraits shown while those menus take over the
                     message area — and gfx-cellar/, the cellar's own
                     pack (packed-earth floor, ceiling darkening to
                     black);
                     regenerate: worlds/closure/gfx/make-pack.lisp
                     and worlds/closure/gfx-cellar/make-pack.lisp
tests/run-tests.lisp test suite (make test)
```

Both zones declare their pack in their map file (`(zone :kind :city
:gfx "gfx/")`, `(zone :kind :dungeon :gfx "gfx-cellar/")`, each
resolved next to the map — the self-contained world-directory pattern
from the engine README), so travel swaps the art as the party moves
between the town and the cellar.  The swap takes about two
seconds on a 68040 and under a minute on a plain 68020 (FS-UAE
measurements — the decode runs in C builtins); the engine keeps the
pack you left loaded so the way back is instant
(`tale:*gfx-cache-packs*` — see the engine README).  Try
the town pack on the cellar:

```
clamiga --heap 8M --load worlds/closure/gfx/run.lisp
```
