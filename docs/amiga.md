# AmigaOS Packages — `AMIGA`, `AMIGA.FFI`, `AMIGA.EXEC`, `AMIGA.INTUITION`, `AMIGA.GFX`, `AMIGA.GADTOOLS`, `AMIGA.REACTION`, `AMIGA.AUDIO`, `AMIGA.ASYNCIO`, `AMIGA.AREXX`

The AmigaOS-native bindings. These exist **only on the AmigaOS build** — on the
POSIX host the packages are not present. `AMIGA` is a C-level package (raw
register-based library calls); the rest are Lisp libraries loaded on demand via
`require`, with zero binary-size cost until used.

| Package | Load with | Provides |
|---------|-----------|----------|
| `AMIGA` | `(require "ffi")` | Raw library open/close and register-based library calls |
| `AMIGA.FFI` | `(require "amiga/ffi")` | Tag lists, `defcfun`, `with-library` |
| `AMIGA.EXEC` | `(require "amiga/exec")` | AvailMem memory introspection, chip-RAM upload helper |
| `AMIGA.INTUITION` | `(require "amiga/intuition")` | Windows, screens, IDCMP events, public screens |
| `AMIGA.GFX` | `(require "amiga/graphics")` | Drawing: lines, rectangles, ellipses, text, pens, fonts, bitmaps, blits |
| `AMIGA.GADTOOLS` | `(require "amiga/gadtools")` | GadTools gadgets, menus, bevel boxes, VisualInfo |
| `AMIGA.REACTION` | `(require "amiga/reaction")` | ReAction / BOOPSI helpers over the generated class modules: methods, objects and attributes, the window.class event loop, requesters |
| `AMIGA.AUDIO` | `(require "amiga/audio")` | Non-blocking 8-bit sample playback through audio.device |
| `AMIGA.ASYNCIO` | `(require "amiga/asyncio")` | Double-buffered asynchronous file I/O over DOS packets (the NDK AsynchIO package as Lisp) |

`COMMON-LISP-USER` `:use`s `AMIGA` on AmigaOS, so its symbols are available
unqualified. The `AMIGA.*` libraries are referenced by their package prefix
(e.g. `amiga.intuition:`, `amiga.gfx:`).

---

## `AMIGA` — Raw library calls

Open an AmigaOS library and call any function by its negative vector offset and a
register spec. This is the lowest level; higher-level packages are built on it.

```lisp
(require "ffi")
(let ((dos (amiga:open-library "dos.library" 36)))
  ;; Delay(ticks) — dos.library offset -198, d1 = ticks
  (amiga:call-library dos -198 (list :d1 50))
  (amiga:close-library dos))
```

| Signature | Kind | Description |
|-----------|------|-------------|
| `(open-library name &optional version)` | function | Open library `name` at minimum `version` (default 0 = any); returns a base pointer, or `NIL` on failure |
| `(close-library base)` | function | Close a library base |
| `(call-library base offset reg-spec)` | function | Call `base` at `offset` with a register-spec plist (`(:d1 x :a0 ptr …)`); returns the d0 result as an integer |
| `(call-library-fast base offset regspec &rest values)` | function | Fast path: `regspec` is a fixnum of nibbles (low to high), one register index per value (`d0`..`d7` = 0..7, `a0`..`a5` = 8..13); up to 7 register args |
| `(alloc-chip size)` | function | Allocate `size` bytes of Chip RAM; returns a foreign pointer |
| `(free-chip pointer)` | function | Free Chip RAM from `alloc-chip` |

ARexx host-port transport. These are the raw primitives; ordinary use goes
through `AMIGA.AREXX` below, which builds the handler thread on top of them.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(arexx-open &optional basename)` | function | Create the public port, claiming `basename` (upcased, default `"CLAMIGA"`) or the first free `BASENAME.<n>`; returns the name claimed. Must be called from the thread that will wait on it |
| `(arexx-close)` | function | Remove the port, replying to anything still queued |
| `(arexx-port-name)` | function | Name the port is registered under, or `NIL` |
| `(arexx-wait)` | function | Block until a command arrives; returns the command string, or `NIL` when woken by `arexx-request-stop` |
| `(arexx-reply rc &optional result)` | function | Answer the message returned by the last `arexx-wait` |
| `(arexx-request-stop)` | function | Wake the waiting thread so it can shut down; safe from any thread |
| `(arexx-send port command &optional result-size)` | function | Send `command` to a public ARexx port and wait for the reply; returns `(values rc result-string)` |

---

## `AMIGA.FFI` — Tag lists & `defcfun`

Conveniences over `AMIGA`/`FFI` for the AmigaOS calling conventions.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(make-tag-list pairs)` | function | Build an AmigaOS TagItem array from `pairs`, a flat list of tag/value pairs; caller frees with `ffi:free-foreign` |
| `(with-tag-list (var &rest pairs) &body body)` | macro | Build a tag list from `pairs`, bind it to `var`, run `body`, free it after |
| `(with-library (var name &optional (version 0)) &body body)` | macro | Open library `name`, bind the base to `var`, run `body`, close it after |
| `(open-library-or-die name &optional (version 0))` | function | `OpenLibrary` that signals a descriptive error (naming the library) instead of returning NIL |
| `(library-version base)` | function | The `lib_Version` of an open library base (the OS revision actually running) |
| `(defcfun name library-base offset (&rest reg-spec) &key void result doc)` | macro | Define `name` as a binding for the library function at `offset` in `library-base`; `reg-spec` alternates register keywords and parameter names (`(:a1 rastport :d0 x …)`); `:result` chooses how d0 comes back — `:unsigned` (default), `:void`, `:pointer` (foreign pointer, NIL for NULL), `:signed`, `:bool`, `:u16`/`:i16`/`:u8`/`:i8` (`:void t` is the legacy spelling of `:result :void`); more than seven registers fall back to a `defun` over `amiga:call-library`; `:doc` is the docstring.  The function cell receives an *FFI stub* (a compact binding descriptor — a function for every CL purpose; `(ffi::%ffi-stub-info #'name)` lists its fields); a direct call compiles to the library-call opcode in the caller |
| `*defcfun-docstrings*` | variable | When NIL, `defcfun` drops its `:doc` string (default T); `scripts/compile-lib-fasls.sh --no-docstrings` — i.e. `make fasl-amiga` and the binary release — binds it for the Amiga FASLs to save heap on the target |
| `(define-binding-table package (&key base version) &body rows)` | macro | A whole module's bindings as one packed table attached to `package`, materialised on first reference: `(:const "NAME" value)`, `(:var "NAME" value)`, `(:fn "NAME" lvo (:a0 …) :result [:not-morphos\|:morphos] [min-version])` (a `defcfun`), `(:struct "NAME" size ("FIELD" type offset) …)` (a `defcstruct`), `(:field "NAME" type offset)`, `(:name "NAME")` (export only); `base`/`version` name the library-base and `lib_Version` variables the `:fn` rows and version guards use.  Names are strings taken literally.  `(clamiga::%binding-table-info pkg)` / `(clamiga::%binding-table-entries pkg)` inspect a table |

The generated modules under `lib/amiga/raw/` (`(require "amiga/raw/<lib>")`,
packages `AMIGA.RAW.<LIB>`) are built on these: one `define-binding-table`
per module holding every OS function, constant and struct of the NDK
includes, so `(require "amiga/raw/intuition")` costs the table bytes plus
the names a program actually touches — see *Raw OS bindings* in the main
README.

---

## `AMIGA.EXEC` — Memory introspection & chip RAM

```lisp
(require "amiga/exec")
(amiga.exec:avail-mem amiga.exec:+memf-chip+)   ; free chip RAM in bytes
```

| Signature | Kind | Description |
|-----------|------|-------------|
| `(avail-mem &optional (requirements +memf-any+))` | function | Free system memory in bytes — exec.library AvailMem; `requirements` is a `MEMF_*` mask (`+memf-largest+` ORed in asks for the largest single free block, `+memf-total+` for the pool's total size) |
| `(alloc-chip-bytes bytes)` | function | Copy the `(unsigned-byte 8)` vector `bytes` into a fresh chip-RAM allocation (blitter masks, sprites, audio samples); returns the chip foreign pointer, freed with `amiga:free-chip` |
| `*exec-base*` | variable | ExecBase foreign pointer (read from absolute address 4) |

- **Constants:** `MEMF_*` requirement/option flags `+memf-any+`, `+memf-public+`,
  `+memf-chip+`, `+memf-fast+`, `+memf-clear+`, `+memf-largest+`, `+memf-total+`.

---

## `AMIGA.INTUITION` — Windows, screens, events

```lisp
(require "amiga/intuition")
(require "amiga/graphics")

(amiga.intuition:with-window (win :title "Hello Amiga"
                                  :width 320 :height 200
                                  :idcmp amiga.intuition:+idcmp-closewindow+)
  (let ((rp (amiga.intuition:window-rastport win)))
    (amiga.gfx:set-a-pen rp 1)
    (amiga.gfx:move-to rp 20 40)
    (amiga.gfx:gfx-text rp "Hello from CL-Amiga!"))
  (amiga.intuition:event-loop win
    (#.amiga.intuition:+idcmp-closewindow+ (msg) (return))))
```

### Windows

| Signature | Kind | Description |
|-----------|------|-------------|
| `(open-window &key (title "CL-Amiga") (left 0) (top 0) (width 320) (height 200) screen (idcmp +idcmp-closewindow+) flags)` | function | Open an Intuition window via OpenWindowTagList; `title` `NIL` opens an untitled window; `screen` is a custom screen pointer; `flags` defaults to close gadget + drag bar + depth gadget + size gadget + activate |
| `(close-window window)` | function | Close a window from `open-window` |
| `(with-window (var &rest args) &body body)` | macro | Open a window (`args` as for `open-window`), bind to `var`, close on exit |
| `(window-rastport window)` | function | RastPort pointer of the window |
| `(window-width window)` | function | Window width in pixels |
| `(window-height window)` | function | Window height in pixels |
| `(window-left window)` | function | Window left edge |
| `(window-top window)` | function | Window top edge |
| `(window-title window)` | function | Window title pointer |
| `(window-user-port window)` | function | The UserPort (IDCMP message port) of the window |
| `(window-border-left window)` | function | Pixels reserved at the left border |
| `(window-border-top window)` | function | Pixels reserved at the top border (incl. title bar) |
| `(window-border-right window)` | function | Pixels reserved at the right border |
| `(window-border-bottom window)` | function | Pixels reserved at the bottom border |
| `(window-gzz-width window)` | function | Inner width (valid with `+wflg-gimmezerozero+`) |
| `(window-gzz-height window)` | function | Inner height (valid with `+wflg-gimmezerozero+`) |

### Screens

| Signature | Kind | Description |
|-----------|------|-------------|
| `(open-screen &key (width 640) (height 256) (depth 2) (title "CL-Amiga") mode-id show-title)` | function | Open a custom screen via OpenScreenTagList; `mode-id` non-`NIL` requests that display mode (get one RTG-safely from `amiga.gfx:best-mode-id`); `show-title` non-`NIL` shows the title bar |
| `(close-screen screen)` | function | Close a screen from `open-screen` |
| `(with-screen (var &rest args) &body body)` | macro | Open a screen (`args` as for `open-screen`), bind to `var`, close on exit |
| `(show-title screen show-it)` | function | ShowTitle: put the screen's title bar in front of (`show-it` non-`NIL`) or behind (`NIL`) backdrop windows |
| `(screen-width screen)` | function | Screen width in pixels |
| `(screen-height screen)` | function | Screen height in pixels |
| `(screen-bar-height screen)` | function | Height of the screen's title bar |
| `(screen-viewport screen)` | function | Pointer to the screen's embedded ViewPort — what `amiga.gfx:set-rgb4` wants |

### Public screens

| Signature | Kind | Description |
|-----------|------|-------------|
| `(lock-pub-screen &optional name)` | function | Lock a public screen by name (`NIL` = default/Workbench); returns a Screen pointer or `NIL` |
| `(unlock-pub-screen screen &optional name)` | function | Unlock a previously locked public screen |
| `(with-pub-screen (var &optional name) &body body)` | macro | Lock a public screen, bind to `var`, unlock on exit |

### Display dimensions

How large a display actually is belongs to the display *mode*, not to
the screen you ask for: PAL and NTSC differ by 56 rows, and an RTG mode
is whatever its driver says.  Ask, and a program can open a screen the
size of the machine it landed on instead of a size it guessed — which
is what keeps a fixed-layout program from being letterboxed or
rescaled.  Pair these with `amiga.gfx:best-mode-id`.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(query-overscan mode-id &optional oscan-type)` | function | `QueryOverscan`: the overscan rectangle of display mode `mode-id` as `(values min-x min-y max-x max-y)`; `NIL` if the display database does not know the mode |
| `(display-mode-height mode-id &optional oscan-type)` | function | Rows that mode can show — 256 on PAL, 200 on NTSC, the driver's own on RTG; `NIL` for an unknown mode |
| `+oscan-text+` | constant | Region every monitor of that mode is guaranteed to show (the default) |
| `+oscan-standard+` | constant | The mode's nominal display clip |
| `+oscan-max+` | constant | As much as the mode can show |
| `+oscan-video+` | constant | Beyond the visible edges |

```lisp
(let* ((mode (amiga.gfx:best-mode-id :width 320 :height 200 :depth 5))
       (rows (and mode (amiga.intuition:display-mode-height mode))))
  ;; rows is 256 on a PAL machine, 200 on NTSC
  (amiga.intuition:with-screen (scr :width 320 :height (or rows 200)
                                    :depth 5 :mode-id mode)
    ...))
```

### IDCMP events

| Signature | Kind | Description |
|-----------|------|-------------|
| `(get-msg port)` | function | Next message from `port`, or `NIL` if none available |
| `(reply-msg msg)` | function | Reply to a received message |
| `(wait-port port)` | function | Block until a message arrives on `port` |
| `(msg-class msg)` | function | IDCMP class from an IntuiMessage |
| `(msg-code msg)` | function | Code field from an IntuiMessage |
| `(msg-qualifier msg)` | function | Qualifier field from an IntuiMessage (`+iequalifier-*+` bits) |
| `(msg-mouse-x msg)` | function | MouseX from an IntuiMessage |
| `(msg-mouse-y msg)` | function | MouseY from an IntuiMessage |
| `(event-loop window &body clauses)` | macro | Process IDCMP messages for `window` until a clause calls `(return)`; each clause is `(idcmp-class (msg) &body body)` |
| `*event-loop-max-waits*` | variable | When non-`NIL`, bound `event-loop`'s wait to this many `get-msg` polls instead of blocking forever (for unattended test runs) |

### Mouse pointer

| Signature | Kind | Description |
|-----------|------|-------------|
| `(set-pointer window sprite-data height width x-offset y-offset)` | function | SetPointer: show a custom pointer while `window` is active; `sprite-data` is chip RAM in hardware sprite layout, `width` at most 16, `x-offset`/`y-offset` place the hot spot |
| `(clear-pointer window)` | function | Restore the window's default mouse pointer |
| `(make-pointer-sprite rows)` | function | Build the chip-RAM sprite data `set-pointer` wants from `rows`, a list of `(word-a word-b)` pairs, one per pointer line; returns `(values chip height)`; caller frees with `amiga:free-chip` |

### Menus / gadgets on a window

| Signature | Kind | Description |
|-----------|------|-------------|
| `(set-menu-strip window menu)` | function | Attach a menu strip to a window |
| `(clear-menu-strip window)` | function | Remove the menu strip from a window |
| `(add-gadget-list window gadget-list)` | function | Add a gadget list to a window; returns position |
| `(refresh-gadget-list window gadget-list)` | function | Refresh all gadgets in the list |

- **Library base:** `*intuition-base*`.
- **Constants:** IDCMP class flags `+idcmp-closewindow+`, `+idcmp-gadgetup+`,
  `+idcmp-gadgetdown+`, `+idcmp-mousebuttons+`, `+idcmp-mousemove+`,
  `+idcmp-rawkey+`, `+idcmp-menupick+`, `+idcmp-refreshwindow+`,
  `+idcmp-newsize+`, `+idcmp-vanillakey+`, `+idcmp-activewindow+`,
  `+idcmp-inactivewindow+`, `+idcmp-intuiticks+`; mouse button codes
  `+selectdown+`, `+selectup+`, `+menudown+`, `+menuup+`
  (`+idcmp-mousebuttons+` `msg-code` values); input qualifier bits
  `+iequalifier-lshift+`, `+iequalifier-rshift+`, `+iequalifier-capslock+`
  (`msg-qualifier` values); window flags `+wflg-*+`
  (`+wflg-closegadget+`, `+wflg-dragbar+`, `+wflg-depthgadget+`,
  `+wflg-sizegadget+`, `+wflg-activate+`, `+wflg-smart-refresh+`,
  `+wflg-simple-refresh+`, `+wflg-backdrop+`, `+wflg-borderless+`,
  `+wflg-gimmezerozero+`, `+wflg-reportmouse+`, `+wflg-rmbtrap+`); window tags
  `+wa-*+` (`+wa-left+`, `+wa-top+`, `+wa-width+`, `+wa-height+`, `+wa-title+`,
  `+wa-idcmp+`, `+wa-flags+`, `+wa-customscreen+`, `+wa-gadgets+`).

---

## `AMIGA.GFX` — Drawing

### Drawing primitives

| Signature | Kind | Description |
|-----------|------|-------------|
| `(move-to rastport x y)` | function | Move the pen to `(x,y)` |
| `(draw-to rastport x y)` | function | Draw a line from the current pen position to `(x,y)` |
| `(draw-line rastport x1 y1 x2 y2)` | function | Draw a line from `(x1,y1)` to `(x2,y2)` |
| `(rect-fill rastport x-min y-min x-max y-max)` | function | Fill the rectangle between the two corners |
| `(draw-ellipse rastport cx cy rx ry)` | function | Draw an ellipse centered at `(cx,cy)` with radii `rx`/`ry` |
| `(set-a-pen rastport pen)` | function | Set the foreground (A) pen |
| `(set-b-pen rastport pen)` | function | Set the background (B) pen |
| `(set-drmd rastport mode)` | function | Set the draw mode (`+jam1+`, `+jam2+`, `+complement+`, `+inversvid+`) |

### Text & fonts

| Signature | Kind | Description |
|-----------|------|-------------|
| `(gfx-text rastport string)` | function | Render `string` at the current pen position |
| `(text-length rastport string)` | function | Pixel width of `string` in the rastport's font |
| `(open-font name ysize)` | function | OpenFont: the ROM font `name` (e.g. `"topaz.font"`) at `ysize` pixels; returns a TextFont pointer or `NIL`; close with `close-font` |
| `(close-font font)` | function | Close a font from `open-font` |
| `(set-font rastport font)` | function | Select `font` in `rastport` |

### Display database & palette

| Signature | Kind | Description |
|-----------|------|-------------|
| `(best-mode-id &key (width 640) (height 256) (depth 2))` | function | Ask the display database (BestModeIDA) for the mode that best fits — the RTG-safe way to pick a screen mode; returns the mode ID or `NIL` |
| `(set-rgb4 viewport index red green blue)` | function | Set palette entry `index` (4-bit color components) |
| `(get-rgb4 colormap entry)` | function | Read a palette entry as packed nibbles `#x0RGB`; -1 for an entry outside the map |
| `(viewport-color-map viewport)` | function | Pointer to the viewport's ColorMap — what `get-rgb4` wants |

### Bitmaps & blits (RTG-safe)

All through OS calls: bitmaps from AllocBitMap (pass a friend bitmap so RTG
systems allocate their native format), chunky pixels via WriteChunkyPixels
(V40+; WritePixel fallback on V39), copies via BltBitMapRastPort.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(gfx-version)` | function | graphics.library version; WriteChunkyPixels needs 40+ |
| `(alloc-bitmap width height depth &key (flags +bmf-clear+) friend)` | function | AllocBitMap: an offscreen bitmap, cleared by default; pass the screen's or window's bitmap as `friend` for RTG-native format; signals on failure |
| `(free-bitmap bitmap)` | function | Free a bitmap from `alloc-bitmap` |
| `(with-bitmap (var width height depth &rest keys) &body body)` | macro | Allocate a bitmap (`keys` as for `alloc-bitmap`), bind to `var`, free on exit |
| `(get-bitmap-attr bitmap attribute)` | function | GetBitMapAttr: query `+bma-width+`/`+bma-height+`/`+bma-depth+`/`+bma-flags+` |
| `(init-rastport rastport)` | function | InitRastPort on a raw RastPort allocation |
| `(with-bitmap-rastport (var bitmap) &body body)` | macro | A scratch RastPort rendering into `bitmap`: allocated, InitRastPort'd and pointed at the bitmap; freed on exit |
| `(rastport-bitmap rastport)` | function | Pointer to the BitMap the rastport renders into (rp_BitMap) — the natural `alloc-bitmap` `:friend` |
| `(write-chunky rastport x y width height pens)` | function | Write the `(unsigned-byte 8)` vector `pens` (row-major `width` x `height` pen indices) into `rastport` at `(x,y)` |
| `(write-pixel rastport x y)` | function | WritePixel with the foreground pen |
| `(read-pixel rastport x y)` | function | ReadPixel: the pen number at `(x,y)` |
| `(blt-bitmap-rastport src-bitmap src-x src-y dest-rastport dest-x dest-y width height &optional (minterm +minterm-copy+))` | function | BltBitMapRastPort: copy a `width` x `height` region into the destination rastport |
| `(blt-mask-bitmap-rastport src-bitmap src-x src-y dest-rastport dest-x dest-y width height mask &optional (minterm +minterm-cookie+))` | function | BltMaskBitMapRastPort: like above but cookie-cut through `mask`, a single interleaved bitplane in chip RAM with a 1 bit per pixel to copy |
| `*write-chunky-force-fallback*` | variable | Non-`NIL` forces `write-chunky` onto the V39 per-pixel WritePixel path (for exercising the fallback) |

### Planar bitmap access

Legal only on a standard planar BitMap: one from `alloc-bitmap` with no
`:friend` and without `+bmf-displayable+`. Pour plane rows in with
`write-planes`, then `blt-bitmap-rastport` into the friend-format destination.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(bitmap-bytes-per-row bitmap)` | function | struct BitMap BytesPerRow |
| `(bitmap-rows bitmap)` | function | struct BitMap Rows |
| `(bitmap-flags bitmap)` | function | struct BitMap Flags |
| `(bitmap-depth bitmap)` | function | struct BitMap Depth |
| `(bitmap-plane bitmap n)` | function | Pointer to the bitmap's `n`th bitplane (bm_Planes[n]) |
| `(write-planes bitmap planes src-row-bytes height)` | function | Copy planar pixel data into `bitmap`: `planes` is a list of `(unsigned-byte 8)` vectors, one per bitplane, each `height` rows of `src-row-bytes` bytes (ILBM BODY plane layout) |

### RastPort accessors

| Signature | Kind | Description |
|-----------|------|-------------|
| `(rastport-fgpen rastport)` | function | Current foreground pen |
| `(rastport-bgpen rastport)` | function | Current background pen |
| `(rastport-cp-x rastport)` | function | Current pen X position |
| `(rastport-cp-y rastport)` | function | Current pen Y position |
| `(rastport-tx-height rastport)` | function | Current font height in pixels (rp_TxHeight) |
| `(rastport-tx-baseline rastport)` | function | Baseline offset from glyph top (rp_TxBaseline) |

- **Library base:** `*gfx-base*`.
- **Constants:** draw modes `+jam1+`, `+jam2+`, `+complement+`, `+inversvid+`;
  bitmap flags `+bmf-clear+`, `+bmf-displayable+`, `+bmf-interleaved+`,
  `+bmf-standard+`, `+bmf-minplanes+`; GetBitMapAttr attributes `+bma-width+`,
  `+bma-height+`, `+bma-depth+`, `+bma-flags+`; blit minterms `+minterm-copy+`,
  `+minterm-cookie+`.

---

## `AMIGA.GADTOOLS` — Gadgets & menus

```lisp
(require "amiga/gadtools")

(amiga.intuition:with-pub-screen (scr)
  (amiga.gadtools:with-visual-info (vi scr)
    (amiga.gadtools:with-gadgets (glist ctx vi)
      (amiga.gadtools:create-gadget
        amiga.gadtools:+button-kind+ ctx vi
        :left 20 :top 30 :width 120 :height 16
        :text "Click Me" :gadget-id 1)
      ;; … open a window, add the gadget list, run an event loop …
      )))
```

### VisualInfo

| Signature | Kind | Description |
|-----------|------|-------------|
| `(get-visual-info screen &rest tags)` | function | Get VisualInfo for a screen; returns a foreign pointer |
| `(free-visual-info vi)` | function | Free a VisualInfo from `get-visual-info` |
| `(with-visual-info (var screen) &body body)` | macro | Get VisualInfo for `screen`, bind to `var`, free on exit |

### Gadgets

| Signature | Kind | Description |
|-----------|------|-------------|
| `(create-context glist-ptr)` | function | Create a gadget context (dummy head gadget); `glist-ptr` is a foreign pointer to a zero-initialized Gadget* |
| `(create-gadget kind previous-gadget visual-info &key (left 0) (top 0) (width 80) (height 14) text text-attr (gadget-id 0) (flags +placetext-in+) (user-data 0) tags)` | function | Create a GadTools gadget of `kind` (e.g. `+button-kind+`); `tags` is an optional flat list of additional tag/value pairs |
| `(free-gadgets gadget-list)` | function | Free all gadgets in a gadget list |
| `(with-gadgets (glist-var context-var visual-info) &body body)` | macro | Create a gadget context, bind list pointer to `glist-var` and context to `context-var`, free all gadgets on exit |
| `(set-gadget-attrs gadget window &rest tags)` | function | Modify gadget attributes; `tags` is a flat list of tag/value pairs |

### Menus

| Signature | Kind | Description |
|-----------|------|-------------|
| `(make-new-menu-array entries)` | function | Build a foreign NewMenu array from menu specs — each entry is `(type label &key commkey flags userdata)` or `:bar` for a separator; returns `(values pointer strings)` (caller frees both) |
| `(create-menus new-menu-array &rest tags)` | function | Create a menu strip from a NewMenu array; returns a Menu pointer |
| `(layout-menus menu visual-info &rest tags)` | function | Lay out menus for display; call after `create-menus` |
| `(free-menus menu)` | function | Free a menu strip |
| `(with-menus (var entries visual-info &optional window) &body body)` | macro | Create menus from `entries`, lay out with `visual-info`, optionally attach to `window`, clean up on exit |

### Messages & refresh

| Signature | Kind | Description |
|-----------|------|-------------|
| `(gt-get-msg port)` | function | Next IDCMP message via GadTools (handles gadget-specific processing), or `NIL` |
| `(gt-reply-msg msg)` | function | Reply to a GadTools-processed message |
| `(gt-refresh-window window)` | function | Refresh GadTools gadgets after window resize/reveal |
| `(gt-begin-refresh window)` | function | Begin optimized refresh |
| `(gt-end-refresh window &optional (complete t))` | function | End optimized refresh |

### Bevel box

| Signature | Kind | Description |
|-----------|------|-------------|
| `(draw-bevel-box rastport left top width height &key visual-info recessed)` | function | Draw a bevel box on a RastPort |

- **Library base:** `*gadtools-base*`.
- **Gadget kinds:** `+button-kind+`, `+checkbox-kind+`, `+integer-kind+`,
  `+listview-kind+`, `+mx-kind+`, `+number-kind+`, `+cycle-kind+`,
  `+palette-kind+`, `+scroller-kind+`, `+slider-kind+`, `+string-kind+`,
  `+text-kind+`.
- **NewGadget flags:** `+placetext-left/right/above/below/in+`, `+ng-highlabel+`.
- **Per-gadget tags:** the `+gtst-*+`, `+gtin-*+`, `+gtcb-*+`, `+gtcy-*+`,
  `+gtlv-*+`, `+gtsl-*+`, `+gtsc-*+`, `+gtmx-*+`, `+gttx-*+`, `+gtnm-*+`,
  `+gtbb-recessed+`, `+gtmn-new-look-menus+`, `+gt-visual-info+`,
  `+gt-underscore+` families (see the `:export` list in
  `lib/amiga/gadtools.lisp` for the full set).
- **NewMenu constants:** `+nm-end+`, `+nm-title+`, `+nm-item+`, `+nm-sub+`,
  `+nm-barlabel+`.
- **Per-kind IDCMP masks:** `+buttonidcmp+`, `+checkboxidcmp+`, `+integeridcmp+`,
  `+stringidcmp+`, `+cycleidcmp+`, `+mxidcmp+`, `+listviewidcmp+`,
  `+scrolleridcmp+`, `+slideridcmp+`.

---

## `AMIGA.AUDIO` — Sample playback

Allocates a Paula channel through audio.device and plays 8-bit signed samples
from chip RAM. Playback is strictly non-blocking: no call here stalls the
caller for the duration of a sample. See `tests/amiga/test-audio.lisp` for
usage end-to-end.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(open-audio &key (precedence 0))` | function | Allocate one Paula channel and return an audio handle, or `NIL` if no channel is available; `precedence` (-128..127) is the allocation priority |
| `(close-audio audio)` | function | Stop playback, free the channel and all request plumbing |
| `(with-audio (var &rest open-args) &body body)` | macro | Open an audio channel (`open-args` as for `open-audio`), bind the handle to `var`, close on exit; signals when no channel is available |
| `(audio-channel-mask audio)` | function | The Paula channel mask (1, 2, 4 or 8) OpenDevice allocated |
| `(play-sample audio chip-data length &key (period 443) (volume +max-volume+) (cycles 1))` | function | Start `length` bytes of signed 8-bit sample data at `chip-data` (a chip-RAM pointer, e.g. from `amiga.exec:alloc-chip-bytes`); `period` is the Paula period (see `period-for-rate`), `volume` 0..64, `cycles` the repeat count (0 loops until `stop-sample`); returns immediately, `T` when queued |
| `(stop-sample audio)` | function | Silence the channel: abort any in-flight write |
| `(playing-p audio)` | function | True while the last `play-sample` is still sounding |
| `(period-for-rate rate)` | function | Paula period for a sample `rate` in Hz (PAL clock) |

- **Constants:** `+max-volume+` (64), `+max-sample-bytes+` (131072).

---

## `AMIGA.REACTION` — ReAction / BOOPSI helpers

What amiga.lib and reaction.lib give a C ReAction program — `DoMethod()`,
the `RA_*` macros, `NewList()`, literals that outlive the objects — on top
of the generated class modules (`amiga/raw/classes/window`,
`amiga/raw/gadgets/*`, `amiga/raw/images/*`, `amiga/raw/classes/requester`),
which supply the tags, method IDs and class functions.  The module loads on
every system (the host included); the classes themselves exist on AmigaOS
3.5+/3.2 and MorphOS.  See the [ReAction](../README.md#reaction-amigaos-3532-morphos)
section of the main README, the ports of the NDK examples under
`examples/amiga/reaction/`, and `tests/amiga/test-reaction.lisp` /
`tests/test_amiga_reaction.sh` for usage end to end.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(available-p)` | function | True when the ReAction classes can be opened here (window.class opens); `NIL` on the host and on an AmigaOS without ReAction |
| `(do-method object method-id &rest args)` | function | `IDoMethodA`: invoke a method on a BOOPSI object — `CallHookPkt` on the object's class dispatcher; `args` are the message longwords after the MethodID (integers, foreign pointers, `T`/`NIL`); returns d0 as an unsigned integer |
| `(object-class object)` | function | `OCLASS(object)`: the object's IClass as a foreign pointer |
| `(with-foreign-pool () &body body)` | macro | Run `body` with a foreign pool; every `pool-alloc`/`pool-string`/`new-list` made inside — also the string tag values of `new-object`, `set-attrs`, `set-gadget-attrs`, `open-requester` — is freed on exit.  Wrap the life of a GUI in one |
| `(pool-alloc size)` | function | Zeroed foreign memory living until the pool exits |
| `(pool-string string)` | function | A NUL-terminated foreign copy of `string` living until the pool exits |
| `(new-list)` | function | A fresh, initialised exec `struct List` (pooled) — the label list of a chooser / clicktab / listbrowser, filled with `amiga.raw.exec:add-tail` |
| `(free-list-nodes list free-node)` | function | `RemHead` every node of `list` and call `free-node` on it (e.g. `amiga.raw.gadgets.chooser:free-chooser-node`); returns the count |
| `(with-tags (var &rest tags) &body body)` | macro | Bind `var` to a TagItem array built from the `(tag value ...)` plist for `body` — for class functions that take a tag list themselves (`alloc-list-browser-node-a`, `alloc-chooser-node-a`, …); strings are pooled, the array freed on exit |
| `(new-object class &rest tags)` | function | `NewObjectA(class, NULL, tags)`: `class` is what a class module's `xxx-get-class` returns; tag values may be integers, foreign pointers, `T`/`NIL` or strings (pooled, the object keeps the pointer).  Returns the object; signals when the class returns NULL |
| `(dispose-object object)` | function | `DisposeObject` — a window or layout object takes everything attached to it along; `NIL` is ignored |
| `(get-attr attribute object)` | function | `GetAttr`: the value as an unsigned integer, `NIL` if the object does not know the attribute |
| `(get-attr-pointer attribute object)` | function | `get-attr` for a pointer-valued attribute (`WINDOW_Window`, `LISTBROWSER_SelectedNode` …): a foreign pointer or `NIL` |
| `(set-attrs object &rest tags)` | function | `SetAttrsA` with `new-object`'s value rules |
| `(set-gadget-attrs gadget window &rest tags)` | function | `SetGadgetAttrsA(gadget, window, NULL, tags)` — change and refresh a displayed gadget (`window` may be `NIL`) |
| `(open-window window-object)` | function | `RA_OpenWindow` (`WM_OPEN`): the `struct Window` as a foreign pointer, or `NIL` |
| `(close-window window-object)` | function | `RA_CloseWindow` (`WM_CLOSE`) — the object survives |
| `(iconify window-object)` | function | `RA_Iconify` (`WM_ICONIFY`); needs `WINDOW_IconifyGadget` / `WINDOW_AppPort`; true on success |
| `(handle-input window-object)` | function | `RA_HandleInput` (`WM_HANDLEINPUT`): two values, the `WMHI_*` result (class in `WMHI_CLASSMASK`, gadget id in `WMHI_GADGETMASK`, `WMHI_LASTMSG` = 0 when nothing is pending) and the message Code word |
| `(window-signal-mask window-object)` | function | `WINDOW_SigMask`, 0 while the window is closed |
| `(do-window-events ((result code) window-object &key timeout signals) &body body)` | macro | The event loop: `Wait()` on the window's signals (+ `SIGBREAKF_CTRL_C` + `signals`), run `body` per message with `result`/`code` as `handle-input` returns them; `(return)` in `body` or Ctrl-C leaves.  With `timeout` (seconds, default `*event-loop-timeout*`) it polls and returns when the time is up |
| `*event-loop-timeout*` | variable | Default `:timeout` of `do-window-events`; `NIL` = interactive.  Set it before loading a GUI program to run it unattended |
| `(open-requester requester window &rest tags)` | function | requester.class `RM_OPENREQ` over `window` (or `NIL`) with the `REQ_*`/`REQS_*`/`REQI_*` attributes in `tags`; returns the gadget number chosen (1 = leftmost, 0 = rightmost / cancel) |

---

## `AMIGA.ASYNCIO` — Asynchronous file I/O over DOS packets

The NDK 3.1 AsynchIO package (`asyncio.c`, Amiga Developer CD) as Common
Lisp: two buffers alternate, and while the program consumes or fills one,
an `ACTION_READ` / `ACTION_WRITE` packet for the other is in flight at the
filesystem handler — sent with `PutMsg`, collected with `WaitPort` over an
embedded reply port using the classic `PA_IGNORE` / `SIGB_SINGLE` trick.
Buffers are rounded to the device's block size and 16-byte aligned for
DMA.  The module loads on every system; `available-p` (and every open)
needs AmigaOS/MorphOS.  An async file must be used from the thread that
opened it, and file names cannot be interactive streams (`CON:`, `RAW:`,
`*`; `NIL:` works).  I/O errors signal a Lisp error carrying the DOS
error code.

| Signature | Kind | Description |
|-----------|------|-------------|
| `(available-p)` | function | True when DOS packets can be spoken here: AmigaOS/MorphOS with dos.library; `NIL` on the host |
| `(open-async name mode &key buffer-size)` | function | Open `name` for `:read` (read-ahead starts before this returns), `:write` (create, replacing) or `:append`.  `buffer-size` (default 8192) is the total for both buffers, rounded up to twice the device block size.  Returns an `ASYNC-FILE`; signals on failure |
| `(close-async file)` | function | Flush buffered writes, close, free the OS memory.  Idempotent; returns `T` |
| `(with-async-file (var name mode &rest args) &body body)` | macro | `open-async` + `unwind-protect`ed `close-async` |
| `(read-async file dest &optional n)` | function | Read up to `n` bytes into `dest` — a foreign pointer, an integer address, or a `(unsigned-byte 8)` vector (defaults `n` to its length).  Returns bytes read; short only at EOF, `0` at EOF proper |
| `(write-async file src &optional n)` | function | Write `n` bytes from `src` (same kinds as `read-async`); a full buffer goes out asynchronously while the other fills |
| `(seek-async file position whence)` | function | `whence` is `:start` / `:current` / `:end`; returns the previous logical position, like DOS `Seek`.  `(seek-async f 0 :current)` is a position probe |
| `(read-byte-async file)` | function | Next byte as an integer, `NIL` at EOF — a single `peek-u8` on the fast path |
| `(read-char-async file)` | function | Next byte as a `CHARACTER`, `NIL` at EOF |
| `(read-line-async file)` | function | Next line without its newline, `NIL` at EOF |
| `(write-byte-async file byte)` | function | Write one byte (0..255) |
| `(write-char-async file char)` | function | Write one 8-bit character |
| `(write-string-async file string)` | function | Write a string's bytes |
| `(write-line-async file string)` | function | `write-string-async` plus a newline |

`examples/amiga/asyncio/copyfile.lisp` copies and verifies a 256 KB file
with it, timed against plain synchronous streams;
`tests/amiga/test-asyncio.lisp` is the executable specification
(`tests/test_amiga_asyncio.sh` load-checks module and example on the
host).

---

## `AMIGA.AREXX` — ARexx development port

An ARexx host port that lets a native Amiga editor drive the running Lisp:
load the file you are editing, get its compile diagnostics back, evaluate a
form in the live image. Served by its own thread, so it answers while the
REPL is busy.

```lisp
(require "amiga/arexx")
(amiga.arexx:start)          ; => "CLAMIGA"
```

| Signature | Kind | Description |
|-----------|------|-------------|
| `(start &key name stack-size vm-frames)` | function | Open the port and start its handler thread; returns the port name claimed. `stack-size`/`vm-frames` size the thread that compiles your code |
| `(stop)` | function | Remove the port and stop the handler thread |
| `(running-p)` | function | Whether the port is open and its thread alive |
| `(port-name)` | function | Name the port is registered under, or `NIL` |
| `(send port command &key result-size)` | function | Drive another application's ARexx port (or our own) — returns `(values rc result-string)` |
| `*default-port-name*` | variable | Base name `START` claims (`"CLAMIGA"`) |
| `*handler-thread*` | variable | The `MP` thread serving the port |

The commands themselves (`PING`, `VERSION`, `LOAD`, `COMPILE-FILE`, `EVAL`,
`IN-PACKAGE`, `LASTRESULT`), the return-code ladder, and the `RESULT`/`FAILAT`
protocol notes are documented in the
[ARexx port](../README.md#arexx-port-amigaos--morphos) section of the main
README. They are implemented by the portable `EXT.DEV` package
(`lib/dev-commands.lisp`), which loads and runs on the host as well —
`(ext.dev:handle-command "LOAD foo.lisp")` returns `(values rc text)` with no
Amiga in sight, which is how the command layer is tested.

## Source of truth

`tests/amiga/test-gui.lisp` exercises the Intuition/Graphics/GadTools path on
AmigaOS via FS-UAE; `tests/amiga/test-reaction.lisp` (with
`tests/test_amiga_reaction.sh` on the host) covers `AMIGA.REACTION`, and
`examples/amiga/reaction/` are its worked examples — run and photographed
unattended by `verify/realamiga/run-reaction-examples.sh`;
`tests/amiga/test-audio.lisp` covers `AMIGA.AUDIO`;
`tests/amiga/test-asyncio.lisp` (with `tests/test_amiga_asyncio.sh` on
the host) covers `AMIGA.ASYNCIO`; the
`tests/amiga/arexx-tests.lisp` drives `AMIGA.AREXX` end to end
over the real host protocol, and `tests/test_dev_commands.sh` is the
host-side specification for the command layer;
`examples/amiga/gfx/bouncing-lines.lisp` is a runnable graphics demo. See the
[AmigaOS Native GUI](../README.md#amigaos-native-gui) and
[Raw FFI Access](../README.md#raw-ffi-access) sections of the main README.

> The GUI bindings cover common cases (windows, drawing, gadgets, menus) but not
> the full API surface — see
> [Known Limitations](../README.md#known-limitations-and-future-work).
