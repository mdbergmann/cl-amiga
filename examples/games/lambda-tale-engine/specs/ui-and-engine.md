# Lambda's Tale — UI, map-scale and platform spec

Design constraints for the game from M2.5 onward.  The test suite
(`tests/run-tests.lisp`) is the executable form of this spec; when the
two disagree, fix one of them.

## Engine first

Lambda's Tale is an **engine**; the shipped campaign is one instance of
it.  Cities, dungeons, shops, items, monsters and story are all
**first-class, data-driven concepts** — the engine never hard-codes a
story fact, a map, a shop inventory or an item.  Anything M4 and later
adds (towns, shops, sound, spells) must land as engine mechanics plus
campaign data, never as code that knows about "the" town.

## The world: zones and travel (M4)

- A game world is a set of **zones** — ordinary map files linked by
  travel.  A map file self-describes through a `(zone ...)` data form
  after the art: `:kind` (`:dungeon` default, `:city`, ... — an open
  set), `:title` (display name), `:wrap`, `:start-facing`.
- **Cities and dungeons are the same thing to the engine**: maps with
  walls, doors, specials.  `:kind` is data for campaigns and front-ends
  (per-zone tile packs via the ZONE form's :gfx), not an engine branch.
- The special op `(travel FILE [X Y] [FACING])` moves the party to
  another zone: stairs, city gates, portals are all map data.  `FILE`
  resolves relative to the current map's directory; the party arrives
  at `(X,Y)`/`FACING` or the target's start.  Ops after a `travel` in
  the same special are skipped (they belong to the cell just left).
- The game keeps **every visited zone's map and automap knowledge
  alive** (`game-zones`); traveling back restores both.  `:enter-zone`
  is emitted on arrival; travel is depth-capped like teleport chains
  and refused during combat.
- Save games (v2) record the current zone plus **all zones' automap
  knowledge**; other zones reload lazily from their map files when the
  party travels back.
- **A world is a directory**: map files plus a `campaign.lisp` beside
  them.  `load-campaign` (called by both front-ends) loads the
  campaign next to the starting map, so a designer's world brings its
  own classes, monsters and items — never the demo's.

## Locations, items and shops (M4)

- A **location** is an enterable place on a cell — a building in a
  city, a hut in a dungeon: the special op
  `(location TITLE KIND ARG...)`.  Stepping onto the cell enters it;
  the game gains a modal location state (like combat: no walking until
  `leave-location`), `:enter-location`/`:leave-location` frame it.
  KIND is an open set; the engine ships `:shop` mechanics, campaigns
  script other kinds via events.
- **Items are campaign data** (`define-item`): kind (`:weapon` /
  `:armor` / `:shield` / `:misc`), price, damage dice, AC bonus
  (descending AC — the bonus subtracts), optional class restrictions.
- Heroes carry up to **8 items** (`+inventory-limit+`) and equip **one
  weapon, one armor, one shield**; combat uses the equipped weapon's
  dice and the equipment-adjusted AC (`hero-attack-dice`,
  `hero-effective-ac`).  Packs and equipment live in save games.
- A **shop** is a location with `:stock (ITEM-NAME...)` — unlimited
  stock, Bard's Tale style; it buys anything back at half price.
  Freshly bought equipment auto-equips when the slot is free and the
  class allows it.
- The shop interaction is modeled **platform-free** in the engine
  (`shop-view` / `shop-lines` / `shop-act`): both front-ends feed keys
  into the same model and draw the same text lines, so the whole flow
  is testable on the host.  Keys: `1`-`7` pick the shopping hero,
  `1`-`9` buy/sell, `s`/`b` flip the page, `Esc` back/leave.
- Menu lines are **structured** (2026-07-19): a pickable option row is
  `(TEXT . KEY)` (`menu-option`/`menu-numbered`, accessors
  `menu-line-text`/`menu-line-key` in events.lisp), plain lines stay
  strings, and footer hints keep the bracket convention (`[s] sell
  [Esc] back`) located by `menu-key-spans`.  A pointing front-end maps
  clicks on either straight to the model's keys — the Amiga UI's
  hotspot list (`*hotspots*` in amiga-ui.lisp) is rebuilt on every
  redraw from exactly what was drawn, so the whole game plays by
  mouse: walk zones on the view, roster rows, menu rows, footer
  hints, and click-anywhere-to-close on the map/help/sheet pages.

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
+----------------------+---------------------------+
|                      |  message log              |
|  first-person view   |  (microfont, newest line  |
|                      |   at the bottom, older    |
|                      |   lines scroll up)        |
+----------------------+---------------------------+
| location plaque      | effect icons [+ rose]     |
+----------------------+---------------------------+
| party roster (header + up to 7 rows)             |
+--------------------------------------------------+
```

(Revised 2026-07-18 for the lo-res display profile: the narrow
active-spells strip between the view and the text column did not fit
320 pixels, so both profiles share this two-column split — BT1/BT3 on
the Amiga use the same arrangement.)

- **Message log** (right column): everything the game says — combat
  transcript, door/wall feedback, story messages — appended at the
  bottom, older lines scrolling up, exactly like Bard's Tale's text
  column.  Backed by the engine's `attach-message-log` ring
  (`:message` events); front-ends render as many trailing lines as
  fit.  On the Amiga the log renders in the engine's own **5x7
  microfont** (src/microfont.lisp — 6x8 cells, rendered once per
  distinct line into a cached offscreen bitmap and blitted), smaller
  than topaz 8 so the narrow column holds more text.
- **Effect strip** (below the log page, separated by a small gap, on
  the grey chrome; the profiles keep it **20px** — just clearing the
  16px icons, so the log page above gets the room): the party's
  active effects — shield, light and friends, Bard's Tale style — as
  **icons only, laid out left to right in effect order**; no text
  labels (the log announces casting and expiry).  The engine carries
  them as `game-effects`: **records** with a display name, an
  optional expiry on the game clock (`add-effect`'s `:duration`;
  `advance-time` announces and drops the expired), a payload plist of
  engine facts — `(:ac N)` feeds the party's effective AC, `(:light
  t)` defeats darkness, `(:compass t)` orients the party — and an
  optional icon image: a file name resolved against the current map's
  directory (`effect-image-path`, the zone tile-pack rule) — 16x16
  ILBMs with pen 0 as the transparent key (`draw-effect-icon` in
  tools/gen-walls.lisp draws placeholder art; a file that will not
  load logs once and the effect shows nothing in the strip).  The
  host UI stays text.  Re-adding a name refreshes it in place.
  Effects live in save games.  An effect that carries `(:compass t)`
  shows the **live compass rose** in its own slot instead of its
  icon — the diamond with the amber needle pointing at the party's
  facing and the facing letter beside it — so the rose sits wherever
  the granting spell/item/song sits in the strip, not at a fixed
  corner (`compass-points` remains the full-rose geometry helper).
  While one burns (`compass-active-p`) the map footer shows the
  facing; without one the party has no facing readout (Bard's Tale's
  Magic Compass rule).  Turning is silent (no "You turn left." log
  noise).
- **Party roster** (bottom, full width, right under the location
  plaque — there is **no status line**): a header row and one roster
  row per party member, the Bard's Tale table — `# CHARACTER AC HIT
  PTS SPL PTS CL` (max/current hit points, max/current spell points,
  class code via `hero-class-abbrev` — always **two characters**, so
  the name column keeps the room; a downed hero's name and hit
  points turn amber) — the layout must reserve room for **7 rows**.
  What the status line used to carry moved: the key reference to the
  **help page** (`h`/`?`, `help-lines`), position/facing/clock to the
  **map footer**, contextual prompts (combat keys, win/lose) into the
  message log.

## Message-area takeovers (locations, character sheet)

(Added 2026-07-19: interactions used to draw as a page over the view
column; the user-directed rework moved them into the message area,
with pictures in the view column.)

- Entering a **location** (shop, tavern, any kind) or opening a
  **character sheet** (`1`-`7`) does not cover the view column with a
  menu page: the interaction **takes over the message area** — its
  menu lines render at the top of the log page (microfont on the
  Amiga, `%amiga-draw-takeover`), and the trailing log lines keep
  scrolling below a separator rule, so game feedback (a purchase, a
  drink) stays visible while the menu is up.  The page interior
  repaints wholesale on every redraw (a `cls`) — switching pages
  never leaves stale text.
- The **view column** meanwhile shows a picture when the campaign
  ships one: the location op's `:image`, or the sheet hero's class
  portrait (`define-hero-class :image`) — both resolved relative to
  the current map file (the effect-icon rule: `location-image-path`,
  `hero-image-path`).  Without a picture the live first-person view
  stays.  Pictures are opaque ILBMs drawn centered on black,
  center-cropped when they overhang the viewport
  (`%amiga-draw-picture`); a file that will not load logs once and
  the view falls back.  Placeholder art comes from
  `draw-location-scene` / `draw-portrait` (tools/gen-walls.lisp);
  Closure ships viewport-sized scenes and 64x64 portraits
  (worlds/closure/gfx/make-pack.lisp).
- The **cast/use/sing menus and the save picker keep the overlay
  page** over the view column (`%amiga-draw-page`) — they can open in
  combat, where the log must stay readable for the transcript.  The
  full-page sheet overlay (`%amiga-draw-sheet`) stays available as a
  drawing primitive but the play flow uses the takeover.
- The sheet content is the platform-free `hero-sheet-lines` (header,
  `hero-summary-lines` stat block, key hints); the host UI shows it
  as its `:sheet` mode under the same keys (`1`-`7` switch, `Esc`
  back).

## Full map view (`m`)

- The play view carries no map at all; the **automap lives in a
  full-screen map mode** toggled with the `m` key.
- Map mode covers the entire window/screen, draws the explored automap
  centered on the party (clamped to what fits at a readable cell size),
  and returns to the play view on `m` or `Esc`.
- The **two-line footer** carries what the play page has no room for:
  the zone title, the party position `(x,y)` — plus the facing while a
  compass effect burns — and the game clock on the first line, the map
  size (and the `FULL` marker) on the second.  No key hints — those
  live on the help page.
- `f` inside map mode toggles the omniscient debug view (full map
  regardless of knowledge); it exists for development, not gameplay.
- `h`/`?` opens the **help page** (the key reference, `help-lines`)
  from both the play view and map mode; `h` or `Esc` returns to where
  it was opened.
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
  with a borderless backdrop window (input + menus as usual).
- Screen geometry, viewport, tile pack and layout tuning come from a
  **display profile** (`src/profiles.lisp`, `play-amiga`'s `:profile`
  argument): **`:lores`** — 320x256 PAL lores, 5 bitplanes (32
  colors), the ECS target and the default (half the chip-RAM/DMA cost
  of hires, near-square pixels for the art, the Bard's Tale
  presentation) — and **`:hires`** — 640x256 PAL hires, 4 bitplanes
  (16 colors), the classic look with the larger 240x130 viewport.  A
  future target (say a big RTG screen) is a new profile plus an asset
  pack, not new code.
- Mode selection must be **RTG-aware, no chipset assumptions** (this is
  the M3 roadmap rule): ask the display database via
  `graphics.library/BestModeIDA` for the profile's nominal geometry
  and open the screen with whatever ID it returns; only fall back to a
  plain PAL request when the database has no answer.  Bitmaps and
  rendering stay behind OS calls only.
- The window and the screen share the profile's geometry — the window
  version must fit a PAL Workbench, and both displays lay out
  identically.  The layout is computed from the actual inner
  width/height and the rastport font's metrics (line height and
  character cell — no hardcoded 8px glyph math), so the window's
  title bar, the borderless screen, and whatever an RTG driver
  promotes the mode to all come out right.  On a PAL Workbench the
  window opens at 0,0 — there is no room for an offset.
- `lib/amiga/intuition.lisp` provides `open-screen` / `close-screen` /
  `with-screen` (OpenScreenTagList/CloseScreen) and
  `lib/amiga/graphics.lisp` provides `best-mode-id` and `set-rgb4`.

## Wall graphics (M3)

- The Amiga first-person view is composited from **pre-rendered wall
  pieces** in fixed Bard's Tale screen slots; the slot geometry
  (`wall-piece-rect` / `view-blit-list` in view.lisp) derives from the
  same perspective planes as the wireframe display list, so both
  renderers agree about where walls are.
- Piece set per depth (4 depths): front wall, receding left/right side
  walls (trapezoids with the ceiling/floor corners baked in — pieces
  are rectangular blits, correctness comes from back-to-front order),
  left/right flank walls (the neighbor's front wall seen through an
  open side), each with a door variant — 40 pieces.
- Assets are **IFF ILBM** files, one per piece, named by
  `wall-piece-file` — one pack per display profile (`data/gfx/` for
  `:lores`, `data/gfx-hires/` for `:hires`), since the piece sizes
  derive from the profile's viewport.  `src/ilbm.lisp` is a pure-CL ILBM reader/writer
  (ByteRun1 + uncompressed, interleaved masks skipped, unknown chunks
  skipped) — it must keep working on the host, where the tests and the
  art generator run.
- Art is **generated, not hand-drawn**: `tools/gen-walls.lisp` draws
  every piece procedurally (4-color dungeon palette: black, white,
  grey brick, amber doors) and `make assets` writes the files.  The
  test suite regenerates every piece in memory and compares it
  **pixel-for-pixel** against the checked-in file — assets can never
  drift from the generator.
- Rendering is **RTG-safe, OS calls only** (the M3 roadmap rule): the
  pieces are uploaded once per session into `AllocBitMap` bitmaps
  (friend = the window's bitmap, depth = the display's, so blits copy
  all planes), chunky pens via `WriteChunkyPixels` (V40+, per-pixel
  fallback on V39), composited with `BltBitMapRastPort`.  No planar
  poking, no chip-ram assumptions, no bytes-per-row math.
- When the active pack is missing, unreadable or sized for another
  profile the view **falls back to the wireframe renderer** (and says
  so in the message log); the blitted path also requires the layout's
  full profile-viewport size.
- `lib/amiga/graphics.lisp` carries the bindings: `alloc-bitmap` /
  `free-bitmap` / `with-bitmap`, `get-bitmap-attr`, `init-rastport` /
  `with-bitmap-rastport`, `write-chunky`, `read-pixel` / `write-pixel`,
  `blt-bitmap-rastport`, `gfx-version`.

## Test requirements

- Map: parse/movement/knowledge/save round-trip on generated 64x64 and
  128x128 maps; `map-viewport` clamping at all four edges and on maps
  smaller than the requested window (map mode uses it to clamp).
- World: zone-form parsing (kind/title/wrap/start-facing, bad kinds
  rejected), relative path resolution (incl. Amiga volumes), travel
  with per-zone knowledge persistence and zone reuse, ops-after-travel
  skipping, travel-loop depth cap, save v2 round-trip across zones with
  lazy knowledge restore, old save versions rejected.
- Items: registry (titles, bad kinds), pack limit, equip rules (one
  per kind, class restrictions, misc rejected), attack dice and
  effective AC — including scripted combat rounds where the weapon
  carries the kill and the armor turns the blow.
- Shops: enter/leave events and modality, buy (gold, pack space,
  auto-equip), sell (half price, unequip), and the full `shop-act` /
  `shop-lines` interaction from hero pick to leaving; the shipped
  town/cellar world walks end-to-end (gate, shoppe, tavern, ladder).
- Message log: ring limit, trailing-lines query, oldest-first order.
- Party: `join-party` up to 7, refusal at 8, `:party-joined` event.
- Effects: `add-effect` refresh-in-place, `remove-effect`, fresh game
  has none; durations set expiries on the clock, payloads round-trip
  through saves, `:ac` payloads sum, `:light` defeats darkness.
- Time: fresh game at day 1 08:00; steps, turns and combat rounds cost
  a minute, blocked bumps are free; daylight boundary values;
  `:sunrise`/`:sunset` events with their log lines; `clock-line`
  formatting; timed-effect expiry (message + `:effect-expired`).
- Darkness: night or `(zone :dark t)` shrink `game-view-depth` to 1 —
  the view and the automap alike; a `(:light t)` effect restores the
  full depth; `at-night`/`at-day` specials switch on the pure clock.
- Spells: `define-spell` validation (exactly one effect, timed ones
  need durations), class/level knowledge gates, refusals that say why
  and cost nothing, scripted damage/heal/buff/light casts, sp payment
  and daylight-outdoors regen (night, :dark zones and full sp regen
  nothing), `(:cast SPELL [TARGET])` combat-round actions beside the
  bare keywords, and the full `cast-lines`/`cast-act` key walk — out
  of combat, in combat (one caster casts, the rest attack) and the
  Esc unwind.
- Takeovers: `hero-sheet-lines` (header, summary block, key hints);
  `location-image`/`location-image-path` and the class portraits
  resolve map-relative, absent ones NIL; generated scenes/portraits
  size to order and keep to the fixed UI pens; Amiga smoke: the
  takeover page draws cached and uncached, a real picture draws in
  the view column, a missing file defers to the caller after logging
  once.
- Named saves: `slot-path`/`save-slots` (empty without the dir), the
  full `save-menu-lines`/`save-menu-act` key walk — name entry with
  junk-char rejection, Backspace, the live echo, the empty-name and
  length guards, Return and code-13 commits, slot digits for
  overwrite and load, the combat refusal page (digits dead, Esc
  lives) — and a real save/load round trip through the picked paths.
- Amiga (FS-UAE suite): smoke tests for the layout draw calls (incl.
  the effects strip and map page), an unattended `*autoplay*` session
  that enters and leaves map mode, and a `:display :screen` session
  exercising the custom-screen path.
- ILBM: reader/writer round trips (both compressions, pad-boundary
  widths, depths 1-8), ByteRun1 edge cases, palette, unknown-chunk
  skipping, corrupt-file errors.
- Wall pieces: slot geometry (containment, mirroring, blit-list order
  vs. the display list), asset/generator pixel equality for all 40
  pieces; Amiga smoke test loads the assets into bitmaps, blits a view
  and reads a known pixel back.
