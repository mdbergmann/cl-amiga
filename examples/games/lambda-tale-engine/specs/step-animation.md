# Step animation: smooth movement between cells

Design sketch — **TODO, not implemented.**  Nothing in `src/` does any
of this yet; the view snaps from one cell to the next in a single
redraw.

## The observation

Bard's Tale 1 does not snap.  Stepping forward reads as a short
movement — the walls beside you swell and slide off the edge of the
screen, and the corridor ahead opens up — "almost like an animation".
It is a small effect but it is most of what makes the view feel like a
place rather than a slideshow.

Three mechanisms could produce it; the first two are the plausible
ones, and they compose.

**1. Sub-cell perspective frames.**  The view is not only drawn at
integer cell distances.  A step from cell A to cell B is a continuous
change of distance by 1, so an intermediate frame is *the same slice
list, from the old position, drawn with the plane table shifted by a
fraction of a cell*.  `*plane-fractions*` (src/view.lisp) is that
table; plane `k` at step fraction `t` wants the inset `f(k - t)`.

**2. Back-to-front painting with no double buffer.**  Far ranks land
first, near ranks slam over them.  On a slow machine you watch the
four ranks arrive and that alone reads as motion.  We already draw
this way — `%draw-fp-view` blits `view-blit-list` back to front
straight into the window rastport — so on a 14 MHz 020 some of this
effect is already there for free, and on an 040/RTG it is gone.

**3. Real per-frame rescaling of the wall bitmaps.**  Ruled out: no
68020 rescales a viewport of bitmaps at speed, and the blitter cannot
scale at all.

## Stage 1 — the expand-blit (no new art)

Cheapest thing that could work, and the place to start.

For the intermediate frame, do not re-render.  Take the frame already
on screen and push it outward from the vanishing point with four
rectangle blits — left band left, right band right, top band up,
bottom band down, on the order of 6-8 pixels — then draw the true new
frame over it.  Blitter rectangle moves inside one bitmap are close to
free, and because the correct frame lands immediately afterwards the
distortion never accumulates.

Scope: a helper in `src/amiga-ui.lisp` beside `%draw-fp-view`, called
from the movement path before the redraw.  No asset changes, no
`view.lisp` changes, ~15 lines.

If it reads as a smear rather than a step, go on to stage 2.

## Stage 2 — half-step planes

Sample the inset curve at half-cell offsets.  Interpolating the
existing `*plane-fractions*` `#(0 1/5 33/100 42/100 47/100)` at the
midpoints, and extrapolating linearly below zero:

```lisp
;; plane k of the half-step frame carries the inset f(k - 1/2)
(defparameter *plane-fractions-half* #(-1/10 1/10 53/200 3/8 89/200))
```

The negative plane 0 is the whole point: at half a cell in, the
boundary that was the viewport edge is *behind* the viewer, so the
near side walls are wider than the screen and clip.  That clipping is
the wall sliding past you.

Consequences:

- `view-planes` needs no change — `(round (* (1- width) -1/10))` is
  simply negative, and `x1` correspondingly exceeds `width - 1`.
- The blit path **must** clip to the viewport.  Today only flanks are
  cropped (`visible-flank-rect`, with its `sx` source offset); a
  half-step frame needs the same treatment for the near `:side`
  pieces, on both sides.  The `sx` machinery generalises to this.
- New art.  A half-step `(:side 0 :l)` slot spans `f(-1/2)…f(1/2)`,
  which is no existing piece's size, so it needs its own bitmap.

## The art budget

A full half-rank set doubles the pack — roughly 40K to 80K of
bitmaps in lores, per active pack, and doubles what a hand-drawn
custom pack has to supply.  That is too much for one frame of motion.

**Half-ranks for depth 0 and 1 only.**  All the perceived motion is in
the two near side walls sweeping outward; ranks 2 and 3 are a few
pixels wide and can stay at their integer positions for the single
intermediate frame without anyone noticing.  Eight extra files instead
of forty, and a cheaper frame to blit.

Naming: add half kinds rather than a new depth token, so
`wall-piece-file` keeps working unchanged —

```
(:front-h d)          front-h-0.iff
(:front-door-h d)     front-door-h-0.iff
(:side-h d :l/:r)     side-h-0-l.iff
(:side-door-h d :l)   side-door-h-0-l.iff
```

`tools/gen-walls.lisp` generates these procedurally, so the shipped
pack costs only generation time.  A pack that ships no `-h` files must
still work: fall back to stage 1's expand-blit, or to no animation at
all.  Same probe-until-missing rule as the `-vN` style variants.

## Pacing

The animation is one intermediate frame; there is no budget for more
on a 14 MHz 020.  But on an 040 or RTG machine that frame flashes by
invisibly, so it has to be held for a fixed wall-clock time from
`src/time.lisp` rather than "however long the redraw took".  Too long
and movement feels laggy — this wants a tuning constant and a profile
override, and probably a campaign/UI switch to turn it off entirely.

## Turning

The same question applies to turning left/right, where BT also does
not snap.  A 90-degree turn cannot be faked by sliding the viewport in
any honest way, but a single sideways expand-blit frame (stage 1's
trick along one axis) may sell it just as well.  Decide after stage 1
is on screen.

## Tests

The host-testable parts are in `view.lisp`, so `tests/run-tests.lisp`
covers them without an Amiga:

- `view-planes` on `*plane-fractions-half*`: plane 0 has a negative
  `x0` and an `x1` past `width - 1`; planes stay monotone inward.
- The blit list for a half-step frame clips near side pieces to the
  viewport and reports a non-zero `sx` for the left one, exactly as
  the flank case does today.
- The ASCII renderer draws a half-step frame, so the geometry can be
  asserted as a character grid like every other view test.

The pacing and the expand-blit itself are Amiga-only and belong in
`tests/amiga/`.

## Open questions

- Does stage 1 alone sell it?  If yes, stage 2 never has to happen.
- Is one intermediate frame enough at 14 MHz, and what does a full
  redraw actually cost there today?  Measure before building.
- Does the effect survive on a 68040/RTG where mechanism 2 (visible
  back-to-front painting) contributes nothing?
