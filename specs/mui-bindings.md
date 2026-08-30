# MUI from Lisp — raw bindings + `AMIGA.MUI` (2026-08-30)

Status: **Layer 1 (§3, the raw module), the BOOPSI split (§4.1) and
Layer 2 (`AMIGA.MUI`, §4) are implemented** — see §3.5 and §4.6 for what
was built and where it deviates from the sketch; `hello.lisp` of §5, its
harness entry and the docs are in.  The remaining §5 examples are the
open item (§8 step 4).  Companion to `specs/raw-bindings-footprint.md` (the
binding-table format) and to `lib/amiga/reaction.lisp` (the toolkit
layer this one mirrors).

## 1. Goal

Give MUI (Magic User Interface, 3.8 on AmigaOS 3.x, 4.x built into
MorphOS) the same two-layer shape every other OS API has here:

1. **Raw** — `amiga/raw/muimaster`, one generated
   `amiga.ffi:define-binding-table` form (the condensed form): every
   `muimaster.library` function plus every `MUIA_`/`MUIM_`/`MUIV_`/
   `MUII_`/`MUIO_`/`MUIC_`… identifier of `libraries/mui.h`, 1:1 with
   the C header, demand-interned, near-zero cost until touched.
2. **Curated** — `lib/amiga/mui.lisp`, package `AMIGA.MUI`,
   `(require "amiga/mui")`: what a C MUI program gets from `muimaster`'s
   macros and amiga.lib (`MUI_NewObject` with a class *name*, `DoMethod`,
   `set`/`get`, the `MUIM_Notify` idiom, the `MUIM_Application_NewInput`
   event loop, `MUI_Request`), with Lisp values, pooled strings, and an
   unattended-run timeout — the exact counterpart of `AMIGA.REACTION`.

Plus the usual surroundings: examples (ports of the MUI 3.8 SDK demo
sources), host + Amiga tests, README / `docs/amiga.md` sections.

## 2. Where we stand (facts checked 2026-08-30)

- `lib/amiga/raw/muimaster.lisp` **already exists**: 26 `:fn` rows from
  the MorphOS SDK `muimaster_lib.fd`+`clib` (the generator's
  `BINDGEN_MOS_ONLY` allowlist, "function tables only"), **0 constants,
  0 structs** — because no MUI header is a generator input.  Its `:fn`
  rows carry no guard, so on AmigaOS the module opens `muimaster.library`
  and works for the LVOs MUI 3.8 has; `MUI_GetRGBColor` (LVO −690,
  MorphOS only) would jump into nothing on 3.8.
- The **MUI 3.8 developer kit is on disk** (untracked, part of the
  FS-UAE Workbench): `verify/realamiga/aos3/System/MUI/Developer/` —
  `FD/muimaster_lib.fd` (17 public functions, bias 30…198, `##private`
  entries between), `C/Include/clib/muimaster_protos.h`,
  `C/Include/libraries/mui.h` (3179 lines), `C/Examples/*.c` (MUI-Demo,
  Layout, Balancing, Pages, Menus, ShowHide, Slidorama, Virtual,
  AppWindow, Popup, DragnDrop, InputHandler, Class1–3, Subtask, WbMan,
  BoopsiDoor, EnvBrowser), `Autodocs/MUImaster.doc`, `Docs/MUIdev.guide`.
  FS-UAE's User-Startup assigns `MUI:` and adds `MUI:Libs` to `LIBS:`;
  `muimaster.library` there is 19.35 (MUI 3.8).  So the FS-UAE suite can
  run MUI tests unattended today.
- `mui.h` profile: 1062 `#define`s — 349 `MUIA_`, 120 `MUIM_`, 169
  `MUIV_`, 62 `MUII_`, 23 `MUIO_`, 65 `MUIC_` (**string** values:
  `"Application.mui"` …), `MUIKEY_*`, `MUI_EHF_*`, `MUI_MAXMAX`,
  `MUIMASTER_VMIN 11`; 121 function-like macros (the `ApplicationObject`
  / `String(...)` shortcuts, `MUIV_Window_AltHeight_Screen(p)`, `get`/`set`/
  `DoMethod`); 137 C structs — 119 `MUIP_*` method-message structs and
  ~18 real ones (`MUI_MinMax`, `MUI_NotifyData`, `MUI_AreaData`,
  `MUI_RenderInfo`, `MUI_CustomClass`, `MUI_GlobalInfo`,
  `MUI_EventHandlerNode`, `MUI_InputHandlerNode` (contains a union),
  `MUI_PenSpec`, `MUI_RGBcolor`, `MUI_PubScreenDesc`, `MUI_Command`, …).
  Values are plain hex (`0x804260ab`) or small ints, a few casts
  (`((STRPTR)~0)`), `(1<<0)`; the header `#define`s `MUI_OBSOLETE`
  itself, and every `#ifdef MUI_OBSOLETE` block sits inside the
  `#ifndef MUI_NOSHORTCUTS` macro section, i.e. holds function-like
  macros only.  `#include`s are all NDK headers with `.i` twins (ignored
  by the `.h` reader).
- Generator (`scripts/gen-amiga-bindings.lisp`) capabilities that
  matter: the twin-less `.h` reader handles object-like integer
  `#define`s, enums, `#if/#ifdef/#ifndef/#elif/#else`, `#undef`, casts
  to the integer/pointer types, `?:`; it **skips** string macros
  (counted as "C macros skipped"), function-like macros and C structs.
  ONE C-header root (`*h-include-root*`); `*module-includes*` maps a
  library to explicit includes (`keymap` → `libraries/keymap.h` is the
  precedent); the LVO cross-check only covers functions that have an
  `lvo/*_lib.i` (none for muimaster, so no false failure).
- Binding-table rows: `:const`/`:var` values are **integers only**
  (`encode_value` in `src/core/bindtab.c` accepts fixnum/bignum);
  `:name` rows export a symbol defined by ordinary forms after the
  table (the >7-register defcfun precedent).
- `ffi:make-callback` is **unsupported on AmigaOS/MorphOS**
  (`platform_ffi_make_closure` returns NULL in `platform_amiga.c`) →
  no Lisp-side `struct Hook`s, no custom-class dispatchers on the
  target.  That bounds v1 of the curated layer (§4.5).
- MorphOS box (mos MCP, .212) was offline today; its SDK copy in
  `tools/mos-sdk/` has `fd/` + `clib/` only — `gg:os-include/libraries/
  mui.h` (MUI 4 header, a superset) is not pulled yet.

## 3. Layer 1 — `amiga/raw/muimaster` (generated)

### 3.1 Inputs

A third SDK input next to the NDK and the MorphOS SDK:

```
MUI_SDK=<dir>          # default tools/mui-sdk (gitignored, like tools/mos-sdk)
  FD/muimaster_lib.fd
  C/Include/clib/muimaster_protos.h
  C/Include/libraries/mui.h
```

**Decided 2026-08-30**: `tools/mui-sdk/` is a gitignored copy of the
FS-UAE tree's `System/MUI/Developer/` (`rsync -a --exclude='*.info'`,
1.5 MB: FD, C/Include, C/Examples, Autodocs, Docs) — done, `.gitignore`
entry added next to `tools/mos-sdk/`.  Not redistributed; only the
output is committed — the same policy as the NDK / MorphOS SDK.

**Baseline = MUI 3.8** (muimaster 19.x).  Its tags, methods, values and
17 functions are the common subset of every MUI a program can meet:
MUI 3.8 on AmigaOS 3.x (FS-UAE, Vampire), MorphOS's built-in MUI
(4.x/5, header superset), MUI 5 for AmigaOS (muidev.de).  Later MUIs
keep 3.8's numbers, so bindings generated from the 3.8 header are
correct everywhere; what they lack is the newer surface (extra
`MUIA_`/`MUIM_`/`MUIV_` values, new classes such as `Calendar.mui` /
`Hotkeystring.mui`, `MUIB_` tag bases, `MUI_GetRGBColor` and friends).
Those come as an *additive* second source (§3.4 Phase 3b): the MorphOS
SDK's `libraries/mui.h` (pull `gg:os-include/libraries/mui.h` when the
box is back) and/or the MUI 5 SDK header, merged with the rule "3.8
value wins, a differing value for the same name is fatal", new names
emitted unguarded (a tag unknown to an older muimaster is ignored by
the class; a missing class makes `MUI_NewObjectA` return NULL, which
`new-object` reports with the class name), new *functions* guarded by
`:morphos` / `(%version>= N)` exactly as today.

### 3.2 Generator changes (`scripts/gen-amiga-bindings.{sh,lisp}`)

1. **Wrapper**: `MUI_SDK` → `fd2sfd FD/muimaster_lib.fd
   C/Include/clib/muimaster_protos.h` → `$TMP/muimaster_lib.sfd`, exported
   as `BINDGEN_MUI_SFD`; `BINDGEN_MUI_INCLUDE_H=$MUI_SDK/C/Include`.
   Check that `fd2sfd` accepts the 3.8 file (`##private` blocks with
   repeated `MUI_Private()()` lines — the sfd parser already skips
   private entries; the bias arithmetic must stay right across them,
   verify `MUI_ObtainPen` lands at −156 and `MUI_EndRefresh` at −198).
2. **Roles**: the MUI SDK sfd joins the *primary* (AmigaOS) table —
   `load-sfd-dir` result merged into `ndk-libs` under source `:mui` —
   so the existing merge model applies unchanged: 3.8 ∩ MorphOS →
   unconditional; MorphOS-only (`MUI_GetRGBColor` −690) → `:morphos`;
   3.8-only (none today) → `:not-morphos`.  `BINDGEN_MOS_ONLY` keeps
   `muimaster` so a run **without** `MUI_SDK` still emits today's
   function-table-only module (and a loud "no MUI SDK — constants
   omitted" line, mirroring the "commit only output generated WITH the
   MorphOS SDK" rule).
3. **Header roots**: `*h-include-root*` becomes a list searched in order
   (NDK first, then the MUI SDK include dir); `include-path`,
   `parse-h-file`, `h-twinless-p`, `twinless-h-files` take the first hit.
   `*module-includes*` gains `("muimaster" "libraries/mui.h")`.
   `libraries/mui.h` must NOT be picked up as a header-only module of
   `libraries/` (it is claimed by the muimaster module — same mechanism
   as `keymap`).
4. **String constants** — the one format extension.  Two options:
   - **(A, recommended)** a `:string` row (or `:const` with a string
     value) in `bindtab.c`: the blob's value field is already
     variable-length (≤255 bytes); a flag bit marks "bytes = Latin-1
     string", materialisation makes a `DEFCONSTANT` of a fresh simple
     string.  Touches `encode_value`/`parse_row`, `materialize`,
     `%binding-table-entries`, `tests/test_binding_table.c`, a gc-stress
     case, `tests/test_amiga_bindgen.sh` fixture.  No FASL-version bump
     (the blob is a literal byte vector) — but `CL_IMAGE_VERSION` only if
     the table *header* changes, which this does not.
   - **(B, zero C)** emit the 65 `MUIC_` names as `:name` rows and one
     `(amiga.ffi:%define-string-constants '(("+MUIC-APPLICATION+" .
     "Application.mui") …))` form after the table (a single top-level
     unit, ~2 KB).  Cheaper to build, but the 65 symbols + strings are
     eager (≈5 KB heap) and the module is no longer "one form".
   Either way the generator stops counting string macros as skipped for
   this header.
5. **Naming** — mechanical, unchanged:
   `MUIA_Window_CloseRequest` → `+muia-window-close-request+`,
   `MUIM_Application_ReturnID` → `+muim-application-return-id+`,
   `MUIV_Application_ReturnID_Quit` → `+muiv-application-return-id-quit+`,
   `MUIV_Notify_Application` → `+muiv-notify-application+`,
   `MUII_WindowBack` → `+muii-window-back+`, `MUIO_Button` →
   `+muio-button+`, `MUIC_Application` → `+muic-application+`
   (= `"Application.mui"`), `MUI_MAXMAX` → `+mui-maxmax+`,
   `MUI_NewObjectA` → `mui-new-object-a` (as today).  Cast values:
   `((STRPTR)~0)` → `#xFFFFFFFF` (check `c-cast-apply` on pointer
   types; fixture case).
6. **Skips** (documented in the module header as today): the 121
   function-like macros (object shortcuts, `MUIV_Window_Alt*_Screen(p)`,
   `get`/`set`/`nnset`/`DoMethod`), the 137 C structs (§3.4 Phase 2),
   the `MUI_OBSOLETE` blocks (all function-like anyway).

### 3.3 Expected output (sketch)

```lisp
;;; amiga/raw/muimaster  GENERATED by scripts/gen-amiga-bindings.lisp. DO NOT EDIT.
;;; Sources:
;;;   MUI 3.8 SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)
;;;   MorphOS SDK muimaster_lib.fd + clib/muimaster_protos.h (via fd2sfd)
;;;   libraries/mui.h
;;; 26 functions, ~800 constants, 65 string constants, 0 structs.
;;; 121 C macros skipped: not an integer constant (string, call, float).
...
(defvar *muimaster-base*
  (when (member :amigaos *features*)
    (amiga.ffi:open-library-or-die "muimaster.library" 0)))   ; 3.8 = v19, MorphOS = v20
...
(amiga.ffi:define-binding-table "AMIGA.RAW.MUIMASTER"
    (:base *muimaster-base* :version *muimaster-version*)
  ;; --- functions (MUI 3.8 SDK, MorphOS SDK) ---
  (:fn "MUI-NEW-OBJECT-A" -30 (:a0 :a1) :pointer)     ; Object * MUI_NewObjectA(char *class, struct TagItem *tags)
  (:fn "MUI-DISPOSE-OBJECT" -36 (:a0) :void)
  (:fn "MUI-REQUEST-A" -42 (:d0 :d1 :d2 :a0 :a1 :a2 :a3) :signed)
  ...
  (:fn "MUI-END-REFRESH" -198 (:a0 :d0) :void)
  (:fn "MUI-GET-RGB-COLOR" -690 (:a0 :a1 :a2) :signed :morphos)   ; MorphOS only
  ;; --- constants from libraries/mui.h ---
  (:const "+MUIMASTER-VMIN+" 11)
  (:string "+MUIC-APPLICATION+" "Application.mui")
  (:string "+MUIC-WINDOW+" "Window.mui")
  ...
  (:const "+MUIO-BUTTON+" 2)
  (:const "+MUIV-TRIGGER-VALUE+" #x49893131)
  (:const "+MUIV-EVERY-TIME+" #x49893131)
  (:const "+MUIV-NOTIFY-APPLICATION+" 3)
  (:const "+MUIV-APPLICATION-RETURN-ID-QUIT+" -1)
  (:const "+MUIM-NOTIFY+" #x8042C9CB)
  (:const "+MUIM-APPLICATION-NEW-INPUT+" #x80423BA6)
  (:const "+MUIM-APPLICATION-RETURN-ID+" #x804276EF)
  (:const "+MUIA-APPLICATION-TITLE+" #x804281B8)
  (:const "+MUIA-WINDOW-CLOSE-REQUEST+" #x8042E86E)
  (:const "+MUIA-WINDOW-ROOT-OBJECT+" #x8042CBA5)
  (:const "+MUIA-GROUP-CHILD+" #x804226E6)
  (:const "+MUIA-TEXT-CONTENTS+" #x8042F8DC)
  (:const "+MUIA-PRESSED+" #x80423535)
  ...)
```

Footprint estimate: ~865 rows × ~12 B ≈ 10–12 KB packed table (vs.
intuition's ~2000 rows); untouched names cost nothing else.

### 3.5 As built (2026-08-30)

`lib/amiga/raw/muimaster.lisp`: **26 functions, 891 constants (79 of
them strings), 0 structs**, 73 macros skipped; 908 table entries in
34.8 KB (the estimate above forgot that the names dominate —
`+MUIA-WINDOW-CLOSE-REQUEST+` is 28 bytes — the entry itself is 16 B).
Host load: ~0 heap beyond the table; FASL 37 KB.

- Inputs and roles exactly as §3.1/§3.2: `MUI_SDK` (default
  `tools/mui-sdk`) → `fd2sfd` → `BINDGEN_MUI_SFD`, joining the primary
  table under source `:mui`; `BINDGEN_MUI_INCLUDE_H` is the second entry
  of `*h-include-roots*` (a list now; first hit wins;
  `twinless-h-files` scans every root); `*module-includes*` maps
  `muimaster` → `libraries/mui.h`.  `fd2sfd` handles the 3.8 fd's
  `##private` blocks (`ULONG MUI_Private() ()` entries that advance the
  bias): `MUI_ObtainPen` −156, `MUI_EndRefresh` −198, and a new
  generator cross-check against the MorphOS SDK's rendering reports
  "25 functions agree, 0 differ" (a disagreement is a warning that the
  fixture test treats as fatal).  `MUI_GetRGBColor` −690 is `:morphos`.
- **String constants — option (A)**, but as `(:const "NAME" "string")`
  rather than a separate `:string` row kind: the value's type carries
  the information, the row kind still says DEFCONSTANT.  In
  `bindtab.c`: flag `CL_BT_F_STRING`, payload `u8 len + bytes` in the
  arena next to the wide integers, ASCII only (like the names — the
  FASLs must stay byte strings for the m68k build), ≤ 255 characters;
  materialised as a fresh simple string; `%BINDING-TABLE-ENTRIES` round-
  trips it; the validator also rejects unknown flag bits now.  No FASL
  or image version bump (a byte-vector literal either way).
- The generator reads a `#define` whose body is one string literal
  (escapes decoded) as a string constant; **control characters** are
  emitted through a `#.` form so the module stays printable and ASCII
  — the nine `MUIX_*` text-style escapes (`+muix-c+` =
  `"\033c"`) are `#.(map 'string #'code-char '(27 99))`.  Non-ASCII
  (`PSD_NAME_FRONTMOST "«Frontmost»"`), concatenations and calls
  (`PSD_ID_MPUB MAKE_ID(...)`) are skipped and counted.  Two NDK
  modules gained a string constant as a side effect (`trackfile`
  `+trackfilename+`, `reaction/reaction-prefs` `+raprefssemaphore+`).
- Casts to the pointer typedefs (`(STRPTR)`, `(APTR)` …) now read
  unsigned like `T *` casts: `((STRPTR)~0)` → `#xFFFFFFFF`
  (`+mc-template-id+`, `+muiv-application-save-envarc+`).  No NDK
  module changed value.
- Correction to §3.2.6: `mui.h` `#define`s `MUI_OBSOLETE` *itself*, so
  its `#ifdef MUI_OBSOLETE` blocks are live for a C program and hold
  plain constants too (`MUIM_Application_Input`, `..._GetMenuCheck` …);
  they are emitted, as they should be.
- Without `MUI_SDK` the run says so and `muimaster` falls back to the
  MorphOS function table (on `BINDGEN_MOS_ONLY`, as before), which the
  fixture test pins with a second generator run.
- Tests: `tests/test_binding_table.c` (string rows: packing, layout,
  rejections, corrupt blobs, materialisation, compaction, FASL round
  trip), `tests/test_gc_stress_regression.sh` (a binding-table case
  under forced compaction, FASL load included),
  `tests/test_amiga_bindgen.sh` + fixture `mui-sfd/`, `mos-sfd/
  muimaster_lib.sfd`, `mui-include_h/libraries/mui.h` (everything in
  §6's list except the fd itself — the generator consumes sfd, the .sh
  wrapper runs fd2sfd), plus committed-output spot checks;
  `tests/amiga/test-raw-bindings.lisp` gains a `muimaster` section
  (string/tag constants, `MUI_NewObjectA`/`MUI_DisposeObject` of a
  `Rectangle.mui`, the `:morphos` guard), skipped where the library is
  absent.

### 3.4 Later phases of the raw layer (not v1)

- **Phase 2 — C structs from `.h`**: a small C struct reader (member
  types ULONG/LONG/UWORD/WORD/UBYTE/BYTE/BOOL/APTR/`T *`, `struct X`
  (size from the `.i` symbol table — `MinNode`, `Node`, `Hook`,
  `Rectangle`, `TextFont`… are all there), arrays, unions = max, m68k
  2-byte alignment) emitting `:struct` rows.  Unlocks `MUI_MinMax`,
  `MUI_InputHandlerNode`, `MUI_EventHandlerNode`, the `MUIP_*` messages —
  and, as a bonus, the ReAction `.h`-only structs (`ColumnInfo` etc.,
  today "built by hand" in the listbrowser example).  Needed for real
  use only once callbacks exist (§4.5).
- **Phase 3 — MCC headers**: `mui/*_mcc.h` (TextEditor, NList,
  BetterString, …) → `amiga/raw/mui/<name>` modules, mirroring
  `MUI:Libs/mui/<Name>.mcc` the way `gadgets/button.gadget` →
  `gadgets/button`; the class opens through `MUI_NewObjectA` by name, so
  these modules are constants-only (no base variable).
- **Phase 3b — MUI 5 (AmigaOS) SDK** as an alternative primary: its fd
  has more functions with `##bias` beyond 198 and the header adds `MUIB_`
  tags; version markers → `(%version>= 20)` guards, exactly the OS 3.2
  vs 3.0 handling.  Also pull `gg:os-include/libraries/mui.h` from the
  MorphOS box as a second constant source (conflicting values = fatal,
  like the LVO check).

## 4. Layer 2 — `AMIGA.MUI` (`lib/amiga/mui.lisp`, hand-written)

### 4.1 Shared BOOPSI core

`AMIGA.REACTION` originally held the toolkit-neutral half: `%ulong`,
`with-foreign-pool` / `pool-alloc` / `pool-string`, `%build-tags` /
`with-tags`, `object-class` (OCLASS), `do-method` (`CallHookPkt` on the
dispatcher — MUI objects are plain BOOPSI objects, so this works for
`MUIM_*` too), `get-attr` / `get-attr-pointer` / `set-attrs`.  Two ways
to share it:

- **(A, recommended)** move that half into `lib/amiga/boopsi.lisp`
  (`AMIGA.BOOPSI`, `(require "amiga/boopsi")`); `AMIGA.REACTION` and
  `AMIGA.MUI` both `(:use)`-import and re-export those names, so every
  existing `amiga.reaction:` reference, test and example stays valid
  (`tests/test_amiga_reaction.sh` unchanged, `test-reaction.lisp`
  unchanged).  One implementation, one set of pool/tag tests.
- **(B)** `AMIGA.MUI` simply `(require "amiga/reaction")` and re-exports.
  No refactor, but "MUI depends on ReAction" reads wrong and duplicates
  nothing only by accident.

**Decided (A), done 2026-08-30.**  As built: `AMIGA.BOOPSI` exports
`do-method`, `object-class`, `with-foreign-pool`, `pool-alloc`,
`pool-string`, `new-list`, `free-list-nodes`, `with-tags`, `get-attr`,
`get-attr-pointer`, `set-attrs` — the spec's list plus the two exec-List
helpers, which are not a ReAction concept either.  `AMIGA.REACTION`
`(:use "AMIGA.BOOPSI")`, `(:import-from "AMIGA.BOOPSI" "%ULONG"
"%WITH-TAGS")` for the helpers it uses itself, and names the shared
symbols in its own `:export` — `defpackage` processes `:use` before
`:export`, so that re-exports the inherited symbol rather than minting a
homonym (`(eq 'amiga.reaction:do-method 'amiga.boopsi:do-method)`, asserted
by the tests).  `new-object` / `dispose-object` / `set-gadget-attrs` stay
in `AMIGA.REACTION`; `AMIGA.MUI` gets its own over `MUI_NewObjectA`.
Error messages of the moved code say `AMIGA.BOOPSI:`.  Tests:
`tests/test_amiga_boopsi.sh` (host; standalone load with no toolkit
package present, the portable half, the symbol identity) and
`tests/amiga/test-boopsi.lisp` (FS-UAE and real MorphOS; a `propgclass`
object of intuition's built-in classes through `do-method` OM_GET/OM_SET,
`get-attr`, `set-attrs`, `object-class` — no toolkit needed, so the
layer is proven on a bare 3.1 too.  Only `PGA_Top` is read back: the
classic propgclass answers OM_GET for nothing else, MorphOS's for more —
tests of the layer must not lean on one OS's class).  `AMIGA.MUI` should `:use` it the
same way.

### 4.2 Exports of `AMIGA.MUI`

| Name | Kind | What |
|---|---|---|
| `available-p` | fn | `muimaster.library` ≥ `MUIMASTER_VMIN` (11) opens here; NIL on the host / an Amiga without MUI |
| `with-foreign-pool`, `pool-alloc`, `pool-string`, `pool-string-array`, `with-tags` | re-exported core (+ `pool-string-array`: NULL-terminated `STRPTR[]` for `MUIA_Cycle_Entries`, `MUIA_Radio_Entries`, `MUIO_Cycle`) |
| `do-method`, `object-class`, `get-attr`, `get-attr-pointer`, `get-attr-string`, `set-attrs` | re-exported core (+ `get-attr-string`: STRPTR attribute → Lisp string, for `MUIA_String_Contents` etc.) |
| `new-object (class &rest tags)` | fn | `MUI_NewObjectA`: `class` is a string (`"Window.mui"`), a keyword (`:window` → `"Window.mui"`, table built from the 65 `MUIC_` names) or a class pointer (`MUI_CreateCustomClass` result, later); tags with `new-object`'s value rules — nested objects are just foreign-pointer values, repeated `MUIA_Group_Child` tags are fine; NULL → error naming the class |
| `make-object (type &rest params)` | fn | `MUI_MakeObjectA`: `:button "OK"`, `:label "Name" flags`, `:checkmark`, `:cycle label entries`, `:radio`, `:slider label min max`, `:string label maxlen`, `:popbutton`, `:hspace`/`:vspace n`, `:hbar`/`:vbar`, `:menuitem`; params packed into a `ULONG[]`, strings pooled |
| `dispose-object (object)` | fn | `MUI_DisposeObject` (not intuition's `DisposeObject`); NIL ignored; disposing the application takes every window and child along |
| `notify (object attr trigger dest method &rest params)` | fn | `MUIM_Notify`: `trigger` is a value, `:every-time` (`MUIV_EveryTime`) or `t`/`nil`; `dest` an object or `:self`/`:window`/`:application`/`:parent` (`MUIV_Notify_*`); `FollowParams` = `(1+ (length params))`; `MUIV_TriggerValue` / `MUIV_NotTriggerValue` usable in `params` |
| `return-id (app id)` | fn | `MUIM_Application_ReturnID` (useful from Lisp-side code paths) |
| `application-input (app)` | fn | `MUIM_Application_NewInput`: returns two values, the id (`+muiv-application-return-id-quit+` folded to `:quit`, 0 to NIL) and the signal mask to `Wait()` on |
| `do-application-events (((id) app &key timeout signals) &body)` | macro | The MUI loop: `NewInput` until `:quit`; `Wait(sigs | SIGBREAKF_CTRL_C | signals)`; `body` runs once per non-zero id; `(return)` leaves; with `timeout` (default `*event-loop-timeout*`) it polls with `Delay(5)` and returns when the time is up — the unattended-run knob, identical to `AMIGA.REACTION` |
| `*event-loop-timeout*` | var | as in ReAction; the examples harness sets it |
| `request (app window title gadgets format &rest params)` | fn | `MUI_RequestA` — the easy requester; `params` become the `ULONG[]` (strings pooled); returns the gadget number; `window`/`app` may be NIL |
| `make-id (string)` | fn | `MAKE_ID('M','A','I','N')` for `MUIA_Window_ID` / `MUIA_Application_…` |
| `set-attrs`-style sugar `set` / `get`? | — | **No**: `cl:set`/`cl:get` would be shadowed; keep `set-attrs` / `get-attr` |
| `window-alt-width-screen (p)` & co. | fn | the four `MUIV_Window_Alt*_Screen/MinMax(p)` function-like macros |

Everything else — layouts, frames, fonts, images (`MUII_*`), menus via
`MUIA_Menustrip` + `Menustrip.mui`/`Menu.mui`/`Menuitem.mui` objects —
is plain `new-object` with raw constants; no wrappers needed.

### 4.3 What a program looks like

```lisp
(require "amiga/mui")
(require "amiga/raw/muimaster")

(defpackage "MUI-HELLO"
  (:use "CL")
  (:local-nicknames ("MUI" "AMIGA.MUI") ("M" "AMIGA.RAW.MUIMASTER")))
(in-package "MUI-HELLO")

(defun run ()
  (mui:with-foreign-pool ()
    (let* ((ok  (mui:make-object :button "_OK"))
           (win (mui:new-object :window
                  m:+muia-window-title+ "Hello from Lisp"
                  m:+muia-window-id+    (mui:make-id "HELO")
                  m:+muia-window-root-object+
                  (mui:new-object :group
                    m:+muia-group-child+
                    (mui:new-object :text m:+muia-text-contents+ "\\33cHello, MUI!")
                    m:+muia-group-child+ ok)))
           (app (mui:new-object :application
                  m:+muia-application-title+  "HelloMUI"
                  m:+muia-application-base+   "HELLOMUI"
                  m:+muia-application-window+ win)))
      (unwind-protect
           (progn
             (mui:notify win m:+muia-window-close-request+ t
                         :application m:+muim-application-return-id+
                         m:+muiv-application-return-id-quit+)
             (mui:notify ok m:+muia-pressed+ nil
                         :application m:+muim-application-return-id+ 1)
             (mui:set-attrs win m:+muia-window-open+ t)
             (unless (mui:get-attr m:+muia-window-open+ win)
               (error "window would not open"))
             (mui:do-application-events ((id) app)
               (case id (1 (format t "OK pressed~%")))))
        (mui:dispose-object app)))))

(if (mui:available-p) (run) (format t "MUI not available on this system~%"))
```

### 4.4 Error paths (diagnostics are part of the spec)

`new-object` names the class in its NULL error and, for a keyword, says
"unknown MUI class keyword :FOO — known: …"; `notify` rejects an unknown
`dest` keyword and a non-integer method; `do-application-events` used
outside `with-foreign-pool` is fine (no strings), but `new-object` with a
string tag outside one gives the pool error as today; `available-p`
answers NIL rather than erroring when `muimaster.library` is missing,
while `(require "amiga/raw/muimaster")` fails at require time with the
library name (raw-module policy).

### 4.5 Out of scope for v1 — and why

- **Hooks and custom classes** (`MUIA_List_DisplayHook`/`ConstructHook`/
  `DestructHook`, `MUIM_CallHook`, `MUI_CreateCustomClass` dispatchers,
  `MUIA_Window_AppWindow` hooks, the SDK's `Class1–3.c`, `InputHandler.c`,
  `DragnDrop.c`, `AppWindow.c`, `Popup.c`): every one needs a `struct
  Hook` whose `h_Entry` re-enters Lisp with a0/a1/a2 in registers.  That
  is a C-runtime feature (`ffi:make-callback` on m68k + a `HookEntry`
  glue that spills the registers, plus MorphOS's PPC gate), tracked
  separately as "amiga/hook".  `AMIGA.MUI` is designed so that adding
  `make-hook` later slots in without API change (`%ulong` already
  accepts foreign pointers as tag values).
- Lists still work without hooks: `List.mui` with `MUIA_List_Format` and
  `MUIM_List_InsertSingle` of pooled strings uses MUI's default
  display; `Cycle`/`Radio` take `pool-string-array`.
- Timers: `MUIM_Application_AddInputHandler` targets an object *method*
  — for stock classes that means `MUIM_Set`-style tricks; the Lisp loop
  can simply `Wait` on a `timer.device` signal via `:signals` instead.

### 4.6 As built (2026-08-30)

`lib/amiga/mui.lisp`, package `AMIGA.MUI`, `(:use "AMIGA.BOOPSI")` +
`(:import-from "AMIGA.BOOPSI" "%ULONG" "%WITH-TAGS")` exactly like
`AMIGA.REACTION`; re-exports the BOOPSI names of the §4.2 table
(`new-list` / `free-list-nodes` stay inherited-only: no exec label lists
in MUI).  Deviations from §4.2, all deliberate:

- **The library is opened lazily**, not at `require`: `*muimaster-base*`
  is NIL until the first entry point (or `available-p`) opens
  `muimaster.library` v11, and stays open for the process (as in C).
  The four functions the module calls itself — `MUI_NewObjectA`,
  `MUI_DisposeObject`, `MUI_MakeObjectA`, `MUI_RequestA` — are its own
  `defcfun`s over that base (the raw module cannot be `require`d where
  MUI is absent); `test_amiga_curated_vs_raw` maps `*MUIMASTER-BASE*` →
  `AMIGA.RAW.MUIMASTER` and pins their LVOs/registers, and the 28
  hand-typed `MUIM_`/`MUIV_`/`MUIO_`/`MUIMASTER_VMIN` constants, to the
  generated table.  Every entry point that needs the library says
  "AMIGA.MUI:NEW-OBJECT: muimaster.library (MUI 3.8 or newer, version
  11) is not available … check AVAILABLE-P" instead of the generic
  OP_AMIGA_CALL "base is NIL".
- **No class table** (§9.4 decided): `class-id` maps a keyword
  mechanically — hyphens dropped, `string-capitalize`, `.mui` — which
  reproduces all 65 `MUIC_*` strings (the host test proves it against
  the raw table) and covers MUI 4/5 classes (`:calendar`,
  `:hotkeystring`) with nothing to keep in sync.  An unknown class is
  reported by MUI's NULL: the error names the string *and* the keyword.
  A `struct IClass *` as `class` goes through intuition's `NewObjectA`.
- `make-object` takes the parameters as `mui.h` lists them; a *list* of
  strings is the `STRPTR *entries` of `:cycle` / `:radio` (a pooled
  `pool-string-array`).  Labels and entries are pooled (the object keeps
  them); `request`'s `%s` parameters are temporaries freed after the
  call, so `request` needs no pool.
- `notify` validates before touching the object (attribute and method
  must be integers, destination an object or one of the four keywords,
  trigger a value or `:every-time`, parameters values or
  `:trigger-value` / `:not-trigger-value`), so the argument errors are
  the same on the host; `%notify-args` is the testable packing.
- `application-input` folds d0 to a signed LONG (`:quit` for −1, NIL for
  0); `do-application-events` follows MUI's rule that a zero mask means
  "NewInput again at once" and never spins on it.
- The `MUIV_Window_*` function-like macros collapse to four functions,
  because the four families share the formulas: `window-size-minmax`
  (−p), `window-size-visible` (−100−p), `window-size-screen` (−200−p),
  `window-edge-delta` (−3−p).
- Tests as §6, MUI-specific: `tests/test_amiga_mui.sh` (41 host checks
  incl. the example load) and `tests/amiga/test-mui.lisp` (FS-UAE MUI 3.8
  and real MorphOS): Rectangle/Text/String objects, `get-attr-string`
  round trip, `make-object` button/slider/cycle, `return-id` →
  `application-input`, `:quit`, **notify without a user** (both
  `:application` and the object as destination; a non-matching trigger
  stays silent), window open/close read back, the bounded loop, and the
  loop's ID/`:quit`/`(return)` semantics.  Three MUI facts the two real
  MUIs taught the tests: the return-ID queue is FIFO and Quit is just an
  entry in it (an ID queued after Quit comes back from the next
  `NewInput`); `MUIV_Notify_Application` from an object in a never-opened
  window fires on MUI 3.8 (the app pointer is propagated at attach time)
  but is silently dropped on MorphOS's muimaster 22 (resolved at window
  setup) — the object destination works on both, and the usual "set up
  notifications, then open" order is fine either way; and a real MorphOS
  box can have a class called `Nonexistent.mui`, so the bogus-class test
  asks for `Clamigabogus.mui`.  Test files that `require` the raw module
  conditionally must look every constant up with `find-symbol`: a
  `amiga.raw.muimaster:` prefix is read before the `when` runs.

## 5. Examples — `examples/amiga/mui/`

Ports of the hook-free MUI 3.8 SDK demos, each ending with the
`(if (mui:available-p) (run) …)` bow-out so the host load-checks them:

| File | SDK source | Shows |
|---|---|---|
| `hello.lisp` | — | §4.3, the minimal application / window / notify / loop |
| `layout.lisp` | `Layout.c` | groups (horiz/vert/columns), weights, frames, `MUII_*` backgrounds |
| `balancing.lisp` | `Balancing.c` | `Balance.mui` between groups |
| `pages.lisp` | `Pages.c` | `MUIA_Group_PageMode` + `Register.mui`, cycle-driven page switch via `notify` with `MUIV_TriggerValue` |
| `menus.lisp` | `Menus.c` | `Menustrip`/`Menu`/`Menuitem` objects, `MUIA_Menuitem_Shortcut`, checkmarks |
| `showhide.lisp` | `ShowHide.c` | `MUIA_ShowMe` / `MUIM_Group_InitChange` / `ExitChange` |
| `slidorama.lisp` | `Slidorama.c` | sliders and gauges tied together with `notify` only |
| `virtual.lisp` | `Virtual.c` | `Virtgroup.mui` + `Scrollgroup.mui` |
| `requester.lisp` | — | `request` (`MUI_RequestA`) with format params |

Harness: `verify/realamiga/examples.lisp` adds the `"mui"` directory
(skip when `(mui:available-p)` is NIL, like `reaction`), sets
`amiga.mui:*event-loop-timeout*` = 6; `run-examples.sh` picks up the
screenshots as today.  `examples/amiga/README.md` gets an "MUI" table.

## 6. Tests

Host (`make test`):
- `tests/test_amiga_bindgen.sh`: fixture MUI SDK (`tests/fixtures/
  bindgen/mui/`: a 10-line fd with a `##private` gap, a clib, a
  `libraries/mui.h` excerpt) exercising: string `#define` → `:string`
  row, negative `MUIV_`, `((STRPTR)~0)` → `#xFFFFFFFF`, `(1<<0)`,
  `#define MUI_OBSOLETE` + `#ifdef` block of function-like macros
  skipped, `#ifndef MUI_NOSHORTCUTS` section skipped, the second header
  root, the `:morphos` guard on a MorphOS-only LVO; plus checks of the
  **committed** `lib/amiga/raw/muimaster.lisp` (row counts, a dozen
  spot values against the header).
- `tests/test_binding_table.c` + gc-stress case: `:string` row
  materialises a string constant; APROPOS/DO-SYMBOLS eager flip covers
  it.
- `tests/test_amiga_curated_vs_raw.sh`: `AMIGA.MUI`'s hand-typed
  constants (`MUIMASTER_VMIN`, `MUIV_Notify_*`, `MUIM_Notify`,
  `MUIM_Application_NewInput/ReturnID`, `MUIV_Application_ReturnID_Quit`,
  `MUIV_TriggerValue`, `MUIO_*`) against the raw rows — same reason
  ReAction duplicates its `WM_*` values: the curated module must load
  without the library.
- `tests/test_amiga_mui.sh` + `tests/test_amiga_mui.lisp`: the portable
  half (`available-p` NIL, `make-id`, `pool-string-array`, the
  `notify`/`make-object` argument packing checked by peeking the built
  message/params arrays, all error messages) and a load of every
  example.  If §4.1(A) is chosen, the pool/tag tests move to
  `tests/test_amiga_boopsi.*` and `test_amiga_reaction.*` keeps its
  class-side checks.

Amiga (`make -f Makefile.cross test-amiga`, FS-UAE has MUI 3.8):
- `tests/amiga/test-mui.lisp` hooked after `test-reaction` in
  `run-tests.lisp`: `available-p` T; raw `+muic-*+` strings and a few
  tag values; `new-object` of Application/Window/Group/Text/String;
  `get-attr-string` of `MUIA_String_Contents` round trip via
  `set-attrs`; **notify without a user**: `notify` a String object's
  `MUIA_String_Contents` `:every-time` → `:application`
  `MUIM_Application_ReturnID 42`, then `set-attrs` the contents and
  assert `application-input` returns 42; window open/close through
  `MUIA_Window_Open` read back; `do-application-events` with `:timeout
  1` returns; `dispose-object` of the application; `make-object :button`
  non-NULL.  `MUI_RequestA` is interactive — covered by the example only.
- Skipped, with an assertion that `available-p` is NIL, where MUI is
  absent (Vampire box status unknown; MorphOS has MUI 4 built in —
  rerun `test-raw-bindings` + `test-mui` there once the box is back).

## 7. Documentation

- README: an **MUI** subsection next to "ReAction" (what it is, the
  `(require "amiga/mui")` one-liner, the §4.3 program, pointer to
  `examples/amiga/mui/` and the tests), and two lines in "Raw OS
  bindings" (the `MUI_SDK` input, `libraries/mui.h` constants, string
  constants).  Known Limitations: "no Lisp hooks/custom classes on the
  target yet".
- `docs/amiga.md`: `AMIGA.MUI` row in the package table + a section
  with the §4.2 table (and `AMIGA.BOOPSI` if split).
- `CLAUDE.md`: nothing new (the `make gen-amiga-bindings` line gains
  `MUI_SDK=tools/mui-sdk`).

## 8. Order of work

1. ~~Generator: `MUI_SDK` input, second header root, `muimaster` include
   mapping, `:string` rows (bindtab.c) — regenerate, commit
   `lib/amiga/raw/muimaster.lisp` (+ FASL).  Host bindgen/bindtab tests.~~
   **Done 2026-08-30** (§3.5).
2. ~~`lib/amiga/boopsi.lisp` split (if A) — pure refactor, all existing
   gates green before step 3.~~  **Done 2026-08-30** (§4.1).
3. ~~`lib/amiga/mui.lisp` + host tests + `hello.lisp`; FS-UAE
   `test-mui.lisp` green.~~  **Done 2026-08-30** (§4.6); `hello` is in
   the examples harness, README / `docs/amiga.md` / `examples/amiga/
   README.md` have their MUI sections.
4. Remaining §5 examples (`layout`, `balancing`, `pages`, `menus`,
   `showhide`, `slidorama`, `virtual`, `requester`).
5. Later: Phase 2 struct reader, MCC modules, MUI 5 / MorphOS header
   merge, and the `amiga/hook` runtime feature that unlocks §4.5.

## 9. Decisions to take before coding

1. ~~`:string` binding-table row (A) vs. defconstant tail (B)~~ —
   **decided (A)**, spelled `(:const NAME "string")`; see §3.5.
2. ~~Extract `AMIGA.BOOPSI` (A) vs. `AMIGA.MUI` over `AMIGA.REACTION` (B)~~
   — **decided (A)**, built; see §4.1.
3. ~~Generator input location~~ — **decided**: `tools/mui-sdk/`
   (gitignored copy of `MUI:Developer`), MUI 3.8 as the baseline,
   MorphOS / MUI 5 headers additive later (§3.1).  Note the kit's
   second fd, `FD/muiclass_lib.fd` (`MCC_Query`, the interface every
   `.mcc` exports): it has no `clib/` twin, so the wrapper's fd2sfd loop
   skips it by itself — no module for it, which is right (there is no
   `muiclass.library` to open).
4. ~~Keyword class names in `new-object` (`:window`) — keep, or strings
   only (`+muic-window+`) to stay 1:1?~~ — **decided: both**, the
   keyword by a mechanical rule rather than a table (§4.6); strings and
   the raw `+muic-*+` constants pass through unchanged.

## 10. Appendix — what `ffi:make-callback` on the target requires

Checked 2026-08-30.  The core half already exists and is platform-neutral:
`bi_ffi_make_callback` (src/core/builtins_ffi.c) keeps 64 fixed slots
(`FFICallback`: types, `lisp_fn` as a registered GC root, a mutex for
slot claim), and `ffi_callback_handler` marshals `CLFFIValue[]` → boxed
args under `CL_GC_PROTECT` → `cl_vm_apply` → marshals the result back.
The host supplies the trampoline through libffi
(`platform_ffi_make_closure` in platform_posix.c: `ffi_closure_alloc` +
`ffi_prep_closure_loc`); platform_amiga.c's version returns NULL, hence
"callbacks are not supported on this platform".  What is missing:

### 10.1 m68k AmigaOS — a runtime-generated stub (small)

No libffi on m68k, but none is needed: a callback is ~24 bytes of m68k
code in `AllocVec(MEMF_PUBLIC)` memory (all RAM is executable on 68k;
`platform_cache_clear` = `CacheClearU` already serves the JIT,
src/jit/jit.c:3040) with the `FFICallback*` baked in as an immediate
(static C memory, no relocation concern):

```
    movem.l d0-d7/a0-a6,-(sp)     ; register frame (60 bytes)
    move.l  sp,-(sp)              ; &frame  (frame+60 = return addr, +64 = stack args)
    pea     <FFICallback*>
    jsr     _cl_amiga_callback_entry   ; C: (desc, frame) -> d0
    addq.l  #8,sp
    movem.l (sp)+,d1-d7/a0-a6     ; restore callee-saved, keep d0 = result
    rts
```

One stub shape serves both conventions, chosen per callback:

- **C stack ABI** (`ffi:make-callback` as it is today, CFFI
  `defcallback`, C-function-pointer callbacks such as AmiSSL's passphrase
  cb): the entry decodes arguments from `frame+64` using gcc-m68k's
  rounding — `PUSH_ROUNDING(bytes) = (bytes+1)&~1`, `PARM_BOUNDARY 16`
  (toolchain `config/m68k/m68k.h:523`): char/short occupy 2 bytes, int/
  pointer 4, 64-bit 8.  Result in d0 (d0/d1 for 64-bit).
- **Register ABI** (`struct Hook` entries: a0 = hook, a2 = object,
  a1 = message; BOOPSI/MUI dispatchers via `CallHookPkt` use the same
  three): the entry reads the named registers from the frame.  Exposed
  as `amiga.ffi:make-hook (fn &key data)` returning a ready `struct Hook`
  (`h_Entry` = stub, `h_Data`), and `make-dispatcher` for
  `MUI_CreateCustomClass`; a general `(:regs (:a0 :a1 :a2 :d0))` option
  covers the rest.
- `:float`/`:double` args and results rejected in v1 (fp0 vs d0/d1
  differs between the soft-float and `FPU=1` builds; raw bindings skip
  DOUBLE functions for the same reason).
- Self-test without any GUI: `utility.library` `CallHookPkt` on a
  `make-hook` hook invokes the stub through the OS → FS-UAE suite
  (`test-raw-bindings.lisp` "callback" section).  A C-ABI stub is
  exercised through a hook whose `h_SubEntry` it is (amiga.lib's
  `HookEntry` composition).

### 10.2 MorphOS (PPC) — gates, no code generation

A callback is a `struct EmulLibEntry { TRAP_LIB, 0, ppc_fn }` — the
`CL_PROC_ENTRY_GATE` mechanism platform_amiga_ppc.h already uses for
thread entries.  The gate function reads the 68k register frame through
`REG_A0`…`REG_A7` (`emul/emulregs.h`, as `platform_amiga_call` writes it
on the way out).  The open point is per-callback identity: a gate carries
no user data, so

- hooks: one shared gate; the descriptor lives in the Hook itself
  (`REG_A0` → `h_SubEntry`/`h_Data`) — this is how MorphOS's own
  `HookEntry` works;
- plain C-ABI callbacks: reuse the §10.1 68k byte template (`pea desc;
  jsr shared_gate`) — the emulator runs those two instructions and traps
  into PPC, where the gate finds `desc` at `4(REG_A7)`.  Verify the
  exact frame layout on the box (offline today); AmiSSL's passphrase
  callback (platform_amiga.c:1682, "not implemented yet") is the
  existing consumer that would start working.

### 10.3 Runtime concerns (platform-neutral, host-testable)

1. **Error boundary.**  A Lisp error unhandled inside a callback would
   `longjmp` through the OS's C frames (MUI dispatcher state, held
   semaphores, `Forbid()` nesting) — must be impossible.  The entry
   pushes a C error frame (`cl_error_frames`, the `CL_CATCH` discipline
   whose `saved_nlx_top` floor already stops `THROW`/`RETURN-FROM` from
   crossing it), converts an escaping condition into the callback's
   NULL/0 result and stores it in the thread; the foreign-call return
   path (`cl_ffi_stub_call` / `OP_AMIGA_CALL` / `platform_amiga_call`
   callers) re-signals it once the OS has returned.  Entering the
   interactive debugger on the OS's stack is disabled inside a callback
   (`ext:*callback-error-policy*` `:defer` default; `:debug` opt-in on
   the host).  Note this is a latent host issue too: today an error in
   a `qsort` comparator longjmps through libc.
2. **Which task?**  The callback runs on the *calling* task.  A Lisp
   thread (`tc_UserData` → `CL_Thread`, per-thread VM) is fine — MUI
   invokes hooks and custom-class methods from the application's own
   task, so all of §4.5 is reachable.  Intuition calls BOOPSI *gadget*
   methods (`GM_HITTEST`, `GM_HANDLEINPUT`) from input.device's task:
   no VM, tiny stack — the entry must detect a non-Lisp task and return
   0 with a one-time diagnostic, and custom `gadgetclass` subclasses
   stay documented as unsupported.
3. **GC / MT.**  Re-entry is on the same thread with C values as
   inputs; the generic library-call path does not enter a GC safe
   region (only dedicated blocking builtins like `AREXX-WAIT` do), so no
   transition is needed and `ffi_callback_handler`'s protection is
   exactly sufficient.  STW from another thread waits for this one as it
   already does for any foreign call.  `saved_jit_depth` in the error
   frame keeps the m68k JIT consistent across a deferred error.
4. **Stack.**  Callbacks run on the caller's stack (the 128K
   recommendation applies); `cl_check_recursion_guards` probes relative
   to `cl_c_stack_base`, so deeper re-entry on the same stack is
   guarded as usual.
5. **Lifetime.**  The stub must outlive every object holding it:
   `with-foreign-pool` gains `pool-hook`/`pool-callback` freed after the
   pool's objects are disposed; 64 slots are plenty for a GUI
   (DisplayHook + Construct/DestructHook per list).

### 10.4 Size of the job

- m68k stub + argument decoder + `make-hook`/`make-dispatcher` +
  FS-UAE tests: ~1–2 days.
- MorphOS gate + verification on the real box: ~1 day.
- Error boundary / deferred re-signal / foreign-task check (shared with
  the host, unit-testable there through `call-foreign` of a C function
  that calls back): ~1 day.
- Unlocks: CFFI `defcallback` on the target, MUI custom classes and
  hooks (§4.5), AmiSSL encrypted keys on MorphOS, ReAction hooks
  (`LISTBROWSER_*Hook`, `GA_*`), ASL / commodities hooks.
