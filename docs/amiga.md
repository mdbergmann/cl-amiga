# AmigaOS Packages — `AMIGA`, `AMIGA.FFI`, `AMIGA.INTUITION`, `AMIGA.GFX`, `AMIGA.GADTOOLS`

The AmigaOS-native bindings. These exist **only on the AmigaOS build** — on the
POSIX host the packages are not present. `AMIGA` is a C-level package (raw
register-based library calls); the rest are Lisp libraries loaded on demand via
`require`, with zero binary-size cost until used.

| Package | Load with | Provides |
|---------|-----------|----------|
| `AMIGA` | `(require "ffi")` | Raw library open/close and register-based library calls |
| `AMIGA.FFI` | `(require "amiga/ffi")` | Tag lists, `defcfun`, `with-library` |
| `AMIGA.INTUITION` | `(require "amiga/intuition")` | Windows, screens, IDCMP events, public screens |
| `AMIGA.GFX` | `(require "amiga/graphics")` | Drawing: lines, rectangles, ellipses, text, pens |
| `AMIGA.GADTOOLS` | `(require "amiga/gadtools")` | GadTools gadgets, menus, bevel boxes, VisualInfo |

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

| Symbol | Kind | Description |
|--------|------|-------------|
| `open-library` | function | Open a named library at a minimum version; returns a base pointer |
| `close-library` | function | Close a library base |
| `call-library` | function | Call `base` at `offset` with a register spec list (`(:d1 x :a0 ptr …)`) |
| `call-library-fast` | function | Fast path: numeric register bitmask, up to 7 register args |
| `alloc-chip` | function | Allocate Chip RAM |
| `free-chip` | function | Free Chip RAM from `alloc-chip` |

---

## `AMIGA.FFI` — Tag lists & `defcfun`

Conveniences over `AMIGA`/`FFI` for the AmigaOS calling conventions.

| Symbol | Kind | Description |
|--------|------|-------------|
| `make-tag-list` | function | Build an AmigaOS TagItem array from a plist of tag/value pairs |
| `with-tag-list` | macro | Build a tag list, run a body with it, free it after |
| `with-library` | macro | Open a library, run a body, close it after |
| `defcfun` | macro | Define a Lisp wrapper for a library function (offset + register spec) |

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

- **Windows:** `open-window`, `close-window`, `with-window`,
  `window-rastport`, `window-width`, `window-height`, `window-left`,
  `window-top`, `window-title`, `window-user-port`, the `window-border-*`
  accessors, `window-gzz-width`, `window-gzz-height`.
- **Screens:** `open-screen`, `close-screen`, `with-screen`.
- **Public screens:** `lock-pub-screen`, `unlock-pub-screen`, `with-pub-screen`.
- **IDCMP events:** `get-msg`, `reply-msg`, `wait-port`, `msg-class`, `msg-code`,
  `msg-mouse-x`, `msg-mouse-y`, `event-loop`.
- **Menus / gadgets on a window:** `set-menu-strip`, `clear-menu-strip`,
  `add-gadget-list`, `refresh-gadget-list`.
- **Library base:** `*intuition-base*`.
- **Constants:** IDCMP class flags `+idcmp-closewindow+`, `+idcmp-gadgetup+`,
  `+idcmp-gadgetdown+`, `+idcmp-mousebuttons+`, `+idcmp-mousemove+`,
  `+idcmp-rawkey+`, `+idcmp-menupick+`, `+idcmp-refreshwindow+`,
  `+idcmp-newsize+`, `+idcmp-vanillakey+`; window flags `+wflg-*+`
  (`+wflg-closegadget+`, `+wflg-dragbar+`, `+wflg-depthgadget+`,
  `+wflg-sizegadget+`, `+wflg-activate+`, `+wflg-smart-refresh+`,
  `+wflg-simple-refresh+`, `+wflg-backdrop+`, `+wflg-borderless+`,
  `+wflg-gimmezerozero+`, `+wflg-reportmouse+`, `+wflg-rmbtrap+`); window tags
  `+wa-*+` (`+wa-left+`, `+wa-top+`, `+wa-width+`, `+wa-height+`, `+wa-title+`,
  `+wa-idcmp+`, `+wa-flags+`, `+wa-customscreen+`, `+wa-gadgets+`).

---

## `AMIGA.GFX` — Drawing

- **Drawing:** `move-to`, `draw-to`, `draw-line`, `rect-fill`, `draw-ellipse`.
- **Pens / draw mode:** `set-a-pen`, `set-b-pen`, `set-drmd`.
- **Text:** `gfx-text`, `text-length`.
- **RastPort accessors:** `rastport-fgpen`, `rastport-bgpen`, `rastport-cp-x`,
  `rastport-cp-y`.
- **Draw modes:** `+jam1+`, `+jam2+`, `+complement+`, `+inversvid+`.
- **Library base:** `*gfx-base*`.

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

- **VisualInfo:** `get-visual-info`, `free-visual-info`, `with-visual-info`.
- **Gadgets:** `create-context`, `create-gadget`, `free-gadgets`, `with-gadgets`,
  `set-gadget-attrs`.
- **Menus:** `create-menus`, `layout-menus`, `free-menus`, `with-menus`,
  `make-new-menu-array`.
- **Messages / refresh:** `gt-get-msg`, `gt-reply-msg`, `gt-refresh-window`,
  `gt-begin-refresh`, `gt-end-refresh`.
- **Bevel box:** `draw-bevel-box`.
- **Library base:** `*gadtools-base*`.
- **Gadget kinds:** `+button-kind+`, `+checkbox-kind+`, `+integer-kind+`,
  `+listview-kind+`, `+mx-kind+`, `+number-kind+`, `+cycle-kind+`,
  `+palette-kind+`, `+scroller-kind+`, `+slider-kind+`, `+string-kind+`,
  `+text-kind+`.
- **NewGadget flags:** `+placetext-left/right/above/below/in+`, `+ng-highlabel+`.
- **Per-gadget tags:** the `+gtst-*+`, `+gtin-*+`, `+gtcb-*+`, `+gtcy-*+`,
  `+gtlv-*+`, `+gtsl-*+`, `+gtsc-*+`, `+gtmx-*+`, `+gttx-*+`, `+gtnm-*+`,
  `+gtbb-recessed+`, `+gtmn-new-look-menus+`, `+gt-visual-info+`,
  `+gt-underscore+` families.
- **NewMenu constants:** `+nm-end+`, `+nm-title+`, `+nm-item+`, `+nm-sub+`,
  `+nm-barlabel+`.
- **Per-kind IDCMP masks:** `+buttonidcmp+`, `+checkboxidcmp+`, `+integeridcmp+`,
  `+stringidcmp+`, `+cycleidcmp+`, `+mxidcmp+`, `+listviewidcmp+`,
  `+scrolleridcmp+`, `+slideridcmp+`.

---

## Source of truth

`tests/amiga/test-gui.lisp` exercises the Intuition/Graphics/GadTools path on
AmigaOS via FS-UAE; `examples/gfx/bouncing-lines.lisp` is a runnable graphics
demo. See the [AmigaOS Native GUI](../README.md#amigaos-native-gui) and
[Raw FFI Access](../README.md#raw-ffi-access) sections of the main README.

> The GUI bindings cover common cases (windows, drawing, gadgets, menus) but not
> the full API surface — see
> [Known Limitations](../README.md#known-limitations-and-future-work).
