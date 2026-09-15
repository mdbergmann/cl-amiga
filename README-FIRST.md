# CL-Amiga -- read me first

Common Lisp for AmigaOS 3+ and MorphOS.  This drawer is the binary
release: everything runs from here, from any current directory, with no
assigns and no environment variables.  Unpack it, then double-click an
icon or read on.  (This page is `README-FIRST.guide` on the Amiga and
`README-FIRST.md` everywhere else; the two are the same text.)

## What is where

| File | Description |
|------|-------------|
| `CLAmiga`, `CLAmiga-FPU`, `Clamacs` | Workbench icons: a double-click starts the matching program from `bin/` (`bin/mos/` on MorphOS), see Workbench below |
| `README-FIRST.guide` | this page |
| `cl-amiga.guide` | the manual: features, usage, the GUI libraries, the ARexx port, known limitations (the project's README) |
| `clamacs.guide` | Clamacs, the editor/IDE: keys, windows, the REPL and debugger windows |
| `bin/aos3/clamiga` | AmigaOS 3.x, 68020 or better -- runs on any CPU |
| `bin/aos3-fpu/clamiga` | AmigaOS 3.x, hard-float build -- REQUIRES an FPU |
| `bin/mos/clamiga` | MorphOS (PowerPC, native) |
| `bin/*/clamiga.img` | the heap image of the bare boot, one per binary (see Startup) |
| `bin/aos3/clamacs`, `bin/mos/clamacs` | Clamacs for AmigaOS 3.x (one binary for every 68020+) and for MorphOS |
| `lib/` | the runtime library: precompiled FASLs plus the Lisp sources |
| `docs/` | the package reference, as AmigaGuide and Markdown; `docs/README.guide` is its index |
| `examples/` | example programs, as Lisp source |
| `README.md`, `LICENSE` | the manual as Markdown, and the Apache-2.0 license |

## Workbench

Open this drawer on Workbench (or Ambient on MorphOS) and double-click
`CLAmiga` to start clamiga in a console window, `CLAmiga-FPU` for the
hard-float build, or `Clamacs` for the editor.  Each icon runs the small
launcher script of the same name through IconX, from this drawer, with a
128K stack -- the same as the shell quick start below.  On MorphOS the
scripts start the `bin/mos` binaries.  The console's size and title are
the WINDOW tool type of the CLAmiga icons (Icons > Information); closing
that window ends clamiga.

Every `.guide` file has an icon too: a double-click opens it in
MultiView.

## Which AmigaOS binary?

`bin/aos3` does float math in software (mathieeedoubbas.library) and runs
on every 68020+ machine, FPU or not.  `bin/aos3-fpu` is compiled for the
68881/68882 FPU: float arithmetic runs directly on the FPU and is much
faster.  Use it if your machine has one -- 68881/68882 boards, 68040/68060
(with the standard 68040/68060.library installed), Vampire/Apollo,
PiStorm.  On a machine without an FPU it will crash; when in doubt, start
with `bin/aos3`.

Both binaries print and read floats identically (conversion is exact
integer arithmetic, independent of the FPU), so FASLs and float-heavy
source files are fully interchangeable between them.

## Quick start from a shell

```
stack 131072
cd <this drawer>
bin/aos3/clamiga
```

On MorphOS use `bin/mos/clamiga`, on FPU machines `bin/aos3-fpu/clamiga`.

The binary finds `lib/` on its own: it looks in the current directory, in
`PROGDIR:lib`, and two directory levels above the executable -- which is
exactly where `lib/` sits in this layout.  No assigns or environment
variables are needed; you can run it from any current directory.

A stack of 128K (`stack 131072`) is recommended.  The AmigaOS default of
64K is enough for the core, but deeply nested source (the GUI libraries,
Quicklisp systems) needs more -- with too little stack you get a clean
"C stack nearly exhausted" error instead of a crash.

For bigger programs raise the heap, e.g. `bin/aos3/clamiga --heap 16M`.

## Startup

Each binary starts from the `clamiga.img` beside it: a snapshot of the
booted runtime (boot + CLOS), restored in one read instead of loading
`lib/boot.fasl` and `lib/clos.fasl` form by form.  Keep the image next to
its binary -- it is tied to that exact build and refused by any other.
`clamiga --no-image` boots from the FASLs instead, and `--boot-log` prints
the startup phase timings either way.  See [Heap images](docs/ext.md#heap-images)
in the EXT reference for saving images of your own.

clamiga runs `S:.clamigarc` at startup when the file exists (Lisp forms,
evaluated one after the other -- the place for `(require ...)` lines and
your own settings; `--no-userinit` skips it).

## Clamacs and the ARexx port

Clamacs is an Emacs-flavoured Lisp editor and IDE: a native MUI
application that talks to a running clamiga over clamiga's ARexx port --
load, compile and evaluate from the buffer, arglists and completion, jump
to definition, a REPL window (`C-c C-z`), a debugger window with restarts
and backtrace, and an inspector (`C-c I`).  It needs MUI 3.8 or newer
(muimaster.library 19+) and TextEditor.mcc 15.29 or newer in
`MUI:Libs/mui/` (MorphOS ships it; on AmigaOS 3 install both from
Aminet).  The editor checks for them at startup.

**clamiga does not open its ARexx port by itself.**  Put these two lines
into `S:.clamigarc` (create the file if you have none):

```lisp
(require "amiga/arexx")
(amiga.arexx:start)
```

Every clamiga started from then on -- from the `CLAmiga` icon, from a
shell, or by the editor -- opens the port `CLAMIGA` (a second instance
takes `CLAMIGA.1`, and so on).  For the current session only, type the
same two forms at the REPL.  Then either:

- start clamiga first (icon or shell) and Clamacs second: the editor finds
  the port and connects, or
- start Clamacs alone: the first command that needs clamiga asks "No
  clamiga ARexx port was found.  Start clamiga in its own console
  window?"; Start launches the `clamiga` next to the editor
  (`bin/aos3/clamacs` starts `bin/aos3/clamiga`, `bin/mos/clamacs` starts
  `bin/mos/clamiga`) with a 128K stack and waits for the port to appear.
  That only happens when `S:.clamigarc` opens it -- without the two lines
  the editor reports "Cannot start clamiga" after twenty seconds,
  although a clamiga is running in the new console.

The editor's keys, windows and menus are in [clamacs.guide](clamacs/README.md);
the port's commands (`LOAD`, `EVAL`, `COMPLETE`, ...) are in the manual's
ARexx section ([cl-amiga.guide](README.md#arexx-port-amigaos--morphos)).

## Libraries

```lisp
(require "asdf")             ; ASDF system loader
(require "quicklisp")        ; Quicklisp client
(require "amiga/intuition")  ; windows, screens, IDCMP events
(require "amiga/graphics")   ; drawing primitives
(require "amiga/gadtools")   ; GadTools gadgets and menus
(require "amiga/reaction")   ; ReAction helpers (OS 3.5+/3.2, MorphOS)
(require "amiga/mui")        ; MUI (MUI 3.8+, MorphOS)
(require "amiga/raw/<lib>")  ; generated 1:1 OS bindings, every library/class
(require "amiga/exec")       ; memory introspection, chip RAM
(require "amiga/audio")      ; audio.device sample playback
(require "amiga/arexx")      ; the ARexx port (see above)
```

The core library and everything under `lib/amiga/` (including the
generated raw OS bindings) ship precompiled (`*.fasl`, loaded directly --
the sources sit next to them for reference); the remaining modules
(asdf, quicklisp) are Lisp sources that compile on your machine the first
time they are required and are cached under `S:cl-amiga/faslcache/`, so
later loads are fast.

## Documentation

Every page ships twice: as AmigaGuide (`*.guide`, each with an icon --
double-click it, or `MultiView cl-amiga.guide` from a shell) and as
Markdown (`*.md`).  Every heading is a node, and the links between the
pages work, across the drawers.

- [README-FIRST.guide](README-FIRST.md) -- this page
- [cl-amiga.guide](README.md) -- the manual: features, the REPL and the
  command line, the GUI libraries, the ARexx port, SLY, known limitations
- [clamacs.guide](clamacs/README.md) -- the Clamacs editor/IDE
- [docs/README.guide](docs/README.md) -- the index of the package
  reference: EXT (sockets, TLS, GC, images, introspection), MP (threads),
  FFI, GRAY (Gray streams), MOP, CLAMIGA, and the AMIGA.* GUI bindings --
  every function documented with its call signature

## Examples

```
bin/aos3/clamiga --load examples/amiga/gfx/bouncing-lines.lisp
bin/aos3/clamiga --load examples/amiga/reaction/listbrowser.lisp
```

`examples/amiga/README.md` lists them all: ReAction and MUI GUIs,
graphics, audio, IFF, ARexx.

## Project

Source code, documentation and issue tracker:
https://github.com/mdbergmann/cl-amiga -- Clamacs:
https://github.com/mdbergmann/clamacs.  License: Apache 2.0.
