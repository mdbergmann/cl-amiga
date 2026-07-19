# Closure

A Bard's Tale-like game for [cl-amiga](../../../README.md), built on
the [Lambda's Tale engine](../lambda-tale-engine/README.md) in the
sibling directory.  The party starts at the gate of the town of
Closure; Wolfgar's equipment shoppe is up the street, and the
tavern's trapdoor drops into the cellar dungeon below — a dark zone,
so buy a torch or let Zzgo the conjurer light a mage flame.

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
make test    # run the game's test suite (world, campaign, autoplay)
make run     # play (host ASCII walkabout)
```

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

## Keys

Walkabout: `w` forward, `s` back-step (keeps facing), `a`/`d` turn,
`m` full-screen map view (`m`/`Esc` back, `f` toggles the omniscient
debug view there; the footer shows the zone, position and game
clock), `h` or `?` the help page with this key reference (`h`/`Esc`
back), `1`–`7` open that party member's character sheet
(`1`–`7` switch heroes there, `Esc` back), `c` cast a spell (pick the
caster, the spell and — for a heal — the target by number, `Esc`
backs out), `u` use an item — a torch, a potion — the same way (pick
the user, the item and, for a heal, the target), `p` play a bard song
(pick the singer and the song; one song plays at a time), `S`/`L`
open the save/load slot picker (`1`–`9` pick a
slot, `n` types a new save name, `Esc` cancels; saves live as
`saves/NAME.sav`), `q` quit.  In combat:
`a` attack, `d` defend, `c` cast, `p` play, `f` flee.  Inside a shop:
`1`–`7` pick the shopping hero, `1`–`9` buy or sell, `s`/`b`
flip between the buy and sell pages, `Esc` back/leave.  Inside the
tavern: `1`–`7` buy that hero a drink (a singer's tunes come back),
`d` down the trapdoor, `Esc` leave.  On the Amiga
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
                     town.map + cellar.map (ASCII art + story forms),
                     campaign.lisp (hero classes, spells, items,
                     monsters, starting party) and gfx/ — the town's
                     tile pack (night-sky city palette), the worked
                     example of the engine's tile-pack contract, plus
                     the shoppe/tavern pictures and the class
                     portraits shown while those menus take over the
                     message area;
                     regenerate: worlds/closure/gfx/make-pack.lisp
tests/run-tests.lisp test suite (make test)
```

The town of Closure declares its pack in its map file
(`(zone :kind :city :gfx "gfx/")`, resolved next to the map — the
self-contained world-directory pattern from the engine README), so
travel swaps the art as the party moves between the town and the
cellar — the cellar keeps the engine's default dungeon stone.  Try
the pack on the cellar too:

```
clamiga --heap 8M --load worlds/closure/gfx/run.lisp
```
