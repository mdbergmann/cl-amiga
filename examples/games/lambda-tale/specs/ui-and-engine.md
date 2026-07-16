# Lambda's Tale — UI, map-scale and platform spec

Design constraints for the game from M2.5 onward.  The test suite
(`tests/run-tests.lisp`) is the executable form of this spec; when the
two disagree, fix one of them.

## Map size

- The engine must support dungeon levels of **at least 30x30 cells**
  (Bard's Tale I level size — itself a C64/A500 memory constraint we do
  not inherit).
- To stay flexible the engine and all UI code must work unchanged up to
  **64x64 and 128x128** — no fixed-size buffers, no whole-map screen
  layouts, nothing quadratic per step.
- Per-cell engine cost stays small: walls are a `(h w 4)` keyword
  array, automap knowledge is one fixnum per cell, save games store
  knowledge as row lists.  A 128x128 level is ~16K cells and must load,
  play and save-round-trip on an 8MB Amiga.

## Screen layout (both front-ends)

The in-game screen is split Bard's Tale style.  (Revised 2026-07-16
after the first playtest: the 6x6 minimap viewport was dropped — it
ate the text column's space without earning it; the automap lives
solely in the full map mode under `m`.)

```
+----------------------+--------+------------------+
|                      | active |  message log     |
|  first-person view   | spells |  (newest line at |
|                      | shield |   the bottom,    |
|                      | lamp   |   older lines    |
+----------------------+ ...    |   scroll up)     |
| status line          |        |                  |
| party roster (up to 7 rows)                      |
+--------------------------------------------------+
```

- **Message log** (right column, full height): everything the game
  says — combat transcript, door/wall feedback, story messages —
  appended at the bottom, older lines scrolling up, exactly like Bard's
  Tale's text column.  Backed by the engine's `attach-message-log`
  ring (`:message` events); front-ends render as many trailing lines
  as fit.  This column gets the space the minimap used to take — text
  history is the point of the right side.
- **Active spells strip** (between the first-person view and the text
  column): a narrow column listing the party's active effects — shield,
  lamp/light, levitation and friends, Bard's Tale style.  The engine
  carries them as `game-effects` (`add-effect`/`remove-effect`, EQUAL
  dedup); the strip renders one line per effect.  Until the spell
  system lands (M4) this is reserved space fed by story/test code;
  effects are transient (not yet saved).
- **Status + party roster** (bottom, full width): position/facing/mode
  line and one roster row per party member — the layout must reserve
  room for **7 rows**.

## Full map view (`m`)

- The play view carries no map at all; the **automap lives in a
  full-screen map mode** toggled with the `m` key.
- Map mode covers the entire window/screen, draws the explored automap
  centered on the party (clamped to what fits at a readable cell size),
  and returns to the play view on `m` or `Esc`.
- `f` inside map mode toggles the omniscient debug view (full map
  regardless of knowledge); it exists for development, not gameplay.
- `q` still quits from map mode.  Map mode is unavailable during
  combat.

## Party

- The roster holds **up to 7 members**: **6 regular heroes plus one
  guest slot** — in Bard's Tale tradition the 7th slot is for a
  summoned/charmed monster or story NPC.
- Engine: `+party-limit+` = 7; `join-party` appends a hero and refuses
  (message + `NIL`, no error) when the roster is full; a successful
  join emits `:party-joined`.
- Combat, saves and both roster panes must handle all 7 rows.

## Amiga display: window and custom screen

The Amiga front-end supports two displays, selected by
`play-amiga`'s `:display` argument:

- `:window` (default) — an Intuition window on the Workbench/public
  screen, as before.  This stays the development default because it
  coexists with the shell running the test suite.
- `:screen` — the game opens its **own Intuition screen** and covers it
  with a borderless backdrop window (input + menus as usual).  On a
  real (chipset) Amiga the natural mode is **PAL 640x256 hires**; on
  RTG systems (Picasso96/CyberGraphX/MorphOS) an equivalent small mode
  such as 320x256 or 640x256 is fine.
- Mode selection must be **RTG-aware, no chipset assumptions** (this is
  the M3 roadmap rule): ask the display database via
  `graphics.library/BestModeIDA` for a nominal 640x256, depth 2 mode
  and open the screen with whatever ID it returns; only fall back to a
  plain PAL hires request when the database has no answer.  Bitmaps and
  rendering stay behind OS calls only.
- The window and the screen share the same **PAL 640x256 geometry** —
  the window version must fit (and fill) a PAL Workbench, and both
  displays lay out identically.  The layout is computed from the
  actual inner width/height, so the window's title bar, the borderless
  screen, and whatever an RTG driver promotes the mode to all come out
  right.  On a PAL Workbench the window opens at 0,0 — there is no
  room for an offset.
- `lib/amiga/intuition.lisp` provides `open-screen` / `close-screen` /
  `with-screen` (OpenScreenTagList/CloseScreen) and
  `lib/amiga/graphics.lisp` provides `best-mode-id` and `set-rgb4`.

## Test requirements

- Map: parse/movement/knowledge/save round-trip on generated 64x64 and
  128x128 maps; `map-viewport` clamping at all four edges and on maps
  smaller than the requested window (map mode uses it to clamp).
- Message log: ring limit, trailing-lines query, oldest-first order.
- Party: `join-party` up to 7, refusal at 8, `:party-joined` event.
- Effects: `add-effect` dedup, `remove-effect`, fresh game has none.
- Amiga (FS-UAE suite): smoke tests for the layout draw calls (incl.
  the effects strip and map page), an unattended `*autoplay*` session
  that enters and leaves map mode, and a `:display :screen` session
  exercising the custom-screen path.
