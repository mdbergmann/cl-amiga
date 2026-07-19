# Effects: sources, icons, and the spell-driven compass

Design sketch — implemented 2026-07-19.  (Superseded detail: the
Amiga effect strip now draws icons only, stacked vertically on the
grey chrome, with no text labels — see specs/ui-and-engine.md for the
current layout.)  Covers four connected changes:

1. Remove the always-on compass rose; facing indication becomes a
   timed effect a spell grants (Bard's Tale's Magic Compass).
2. Items become a second effect source: a usable item (torch, potion)
   applies an effect payload when used.
3. Bard songs become a third effect source: bards are effect
   "casters" with their own resource and menu.
4. Effects can carry an image, drawn as an icon in the effects area
   (the band below the message log) instead of / beside the text label.

The through-line: today only spells create effects
(`cast-spell` -> `add-effect`); the effect record (`game.lisp`) is
already source-agnostic.  We keep that record as the single funnel and
add sources and presentation around it.

## 1. The effect record (game.lisp)

```lisp
(defstruct (effect ...)
  name          ; display name / registry key (unchanged)
  expires-at    ; game minute or NIL (unchanged)
  payload       ; plist (grows a :compass key, see below)
  image)        ; NEW: icon filename (string) or NIL
```

- `add-effect` gains `:image`.
- Payload vocabulary grows from `:ac N` / `:light t` to include
  `:compass t` — queried by a new `compass-active-p game`, the exact
  sibling of `light-active-p`.
- Save format v3 -> v4: `%effects->list` adds `:image`; heroes gain a
  `tunes` field (section 4).  Loading stays `(apply #'%make-effect
  plist)`, so v3 saves load fine (missing keys default to NIL);
  bump the version comment anyway.

A shared applier factors the payload -> effect step out of
`cast-spell` so all three sources go through one door:

```lisp
(defun apply-effect-spec (game title spec &key image)
  "SPEC is the (:buff-ac N :duration M) / (:light t :duration M) /
   (:compass t :duration M) vocabulary; installs the timed effect."
  ...)
```

Instant payloads (`:damage`, `:heal`) stay where they are — they are
actions, not effects.

## 2. Compass: from chrome to effect

Today the rose is unconditional UI chrome in `%amiga-draw-band`
(amiga-ui.lisp), fed by `compass-points` (view.lisp).  Change:

- **Keep** `compass-points` (geometry is still needed) and its tests.
- `%amiga-draw-band` draws the rose **only when `compass-active-p`**.
  When inactive, the rose's square at the band's right belongs to the
  effects strip — full band width for effect entries.
- The automap (`m`) keeps its facing arrow: the map is knowledge the
  party remembers; the compass is knowing which way you face *now*
  without stepping.  (Bard's Tale precedent: MACO, Magic Compass.)
- Host UI is untouched for now (it has no rose; the automap arrow and
  step messages carry facing).
- Closure campaign gains the spell:
  ```lisp
  (define-spell 'magic-compass :cost 2 :level 1 :classes '(:conjurer)
    :compass t :duration 120)
  ```
- `define-spell` grows `:compass` in its one-of-exactly-one effect
  check, plus `:image` (section 5).
- Docs: README + specs/ui-and-engine.md band description updated
  ("effects + compass band" -> "effects band; the rose appears while a
  compass effect burns").

## 3. Items as an effect source

`define-item` grows:

```lisp
:use PAYLOAD      ; the effect vocabulary, e.g. (:light t :duration 30)
                  ; or an instant (:heal DICE) — potions
:consumed t/nil   ; T: one charge, the item leaves the pack on use
:image FILE       ; icon for the effect it grants (section 5)
```

New mechanics in items.lisp:

```lisp
(defun use-item (game hero name) ...)
```

checks carrying + class, applies `:use` via `apply-effect-spec`
(timed) or directly (`:heal` -> `heal-hero`), removes the item when
`:consumed`, says what happened, emits `:item-used`.  Items without
`:use` refuse with a message ("Nothing happens"), like `equip-item`
refuses `:misc`.

Interaction: a platform-free `use-view` on the cast-view pattern
(`use-lines` / `use-act`): pick hero -> pick a usable item from the
pack.  Key: `u` in play mode; also a combat action later if wanted
(not in v1 — Bard's Tale items work in camp too).

Closure: the torch finally earns its keep:

```lisp
(define-item 'torch :price 2 :use '(:light t :duration 60)
             :consumed t :image "fx-torch.iff")
```

## 4. Bard songs as an effect source

Mirrors the spell registry, deliberately parallel not shared —
songs and spells differ in resource, gating and replacement:

```lisp
(define-song NAME :title ... :level N :effect PAYLOAD
             :duration MIN :image FILE)
```

- **Who sings**: `define-hero-class` grows `:singer t`;
  `hero-singer-p` mirrors `hero-caster-p`.  Closure: the bard class
  gets it (Melody sings at last).
- **Resource**: `hero-tunes` / `hero-max-tunes` (= level, like Bard's
  Tale songs-per-day).  One tune per song.  Refill: **drinking at a
  tavern** — a new `:tavern` location kind (trivial next to :shop —
  a drink costs a few gold, refills the singer's tunes).  The
  location system is already an open set, so this is small.  A
  time-based regen fallback (like `%regen-sp`) can come later if
  taverns are too sparse.
- **Semantics**: exactly one song active per party.  Singing installs
  its effect with a `:song t` marker in the payload; `add-effect` for
  a new song first removes any effect whose payload carries `:song`.
  The song's payload uses the same vocabulary (`:ac`, `:light`, and
  whatever the vocabulary grows).  Duration in game minutes through
  the existing `%expire-effects` machinery — "the tune fades."
- **Interaction**: `sing-view` / `sing-lines` / `sing-act` on the
  cast-view pattern: pick singer (if >1) -> pick song.  Key: `p` in
  play mode ("play a tune"); in combat, a `:sing` party action next
  to `:cast` in `combat-round` — the bard sings instead of attacking.

## 5. Effect icons in the band

- **Where images live**: with the campaign, not the engine — resolved
  like everything a game owns.  Convention: the world's gfx dir
  (`worlds/<world>/gfx/`), same place as the tile packs; `define-spell`
  / `define-item` / `define-song` give bare filenames, resolved
  against the campaign's directory at load time (both loaders already
  self-locate).
- **Format/size**: IFF ILBM via the existing `read-ilbm`, fixed
  16x16 for both profiles (near-square lores pixels; small enough for
  the 48px band).  Pixels are pen indices into the screen palette
  (pens 0-3 are the UI's; pack pens above) — same rule as tile packs.
  Pen 0 = transparent via the existing cookie-cut mask path.
- **Amiga rendering** (`%amiga-draw-band`): each active effect draws
  as `[icon] label` on its line; no icon -> text-only line exactly as
  today.  Icon bitmaps load lazily on first draw into a session cache
  (hash name -> (bitmap . mask), the `%load-wall-assets` recipe, RTG-
  safe friend bitmap); a missing or unreadable file logs once and
  falls back to text — never a crash, same spirit as the wireframe
  fallback.
- **Host UI**: stays text (`%effects-pane` unchanged) — no bitmaps on
  the host; the model carries the image name so tests can assert the
  plumbing without a screen.

## Order of work

1. **Effect core + compass** — `image` slot, `apply-effect-spec`,
   `:compass` payload + `compass-active-p`, conditional rose, campaign
   spell, save v4.  Smallest slice that changes visible behavior.
2. **Icons** — band icon rendering + lazy cache + closure art.
3. **Item use** — `:use`/`:consumed`, `use-item`, use menu, torch.
4. **Songs** — registry, `:singer`, tunes, tavern, sing menu, combat
   `:sing` action.

Each step: host tests in tests/run-tests.lisp (mechanics + the
platform-free menu models), Amiga run via the game suite under FS-UAE,
README/spec updates.

## Open decisions

- Tavern refill vs. time-based tune regen (sketch says tavern).
- Key bindings: `u` = use item, `p` = sing (both free today; play
  mode uses w/s/a/d/m/c/q/S/L, combat a/d/c/f).
- Icon size 16x16 for both profiles vs. per-profile sizes.
- Whether the host UI should ever render icons (sketch: no).
