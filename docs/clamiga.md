# `CLAMIGA` — Implementation Extras

The `CLAMIGA` package is mostly **implementation internals** — boot/CLOS/loop
helpers and metaobject plumbing prefixed with `%` or `*` (over 220 of them). Those
are not a stable API and are intentionally undocumented here.

A small number of `CLAMIGA` symbols are genuinely useful from user code; those are
documented below.

- **Package:** `CLAMIGA` (mutually `:use`d with `CL` during bootstrap)
- **Inherited by:** `COMMON-LISP-USER`.

## IEEE float bit access

Convert between floats and their raw IEEE-754 bit patterns. Used by the
**float-features** CL-Amiga fork (e.g. so jzon can serialize floats).

| Symbol | Kind | Description |
|--------|------|-------------|
| `single-float-bits` | function | The 32-bit integer bit pattern of a `single-float` |
| `double-float-bits` | function | The 64-bit integer bit pattern of a `double-float` |
| `bits-single-float` | function | Reconstruct a `single-float` from a 32-bit pattern |
| `bits-double-float` | function | Reconstruct a `double-float` from a 64-bit pattern |

## Package-local nicknames (CDR-10)

| Symbol | Kind | Description |
|--------|------|-------------|
| `add-package-local-nickname` | function | Add a local nickname for a package within another |
| `remove-package-local-nickname` | function | Remove one |
| `package-local-nicknames` | function | List the local nicknames defined in a package |

## Type predicates

| Symbol | Kind | Description |
|--------|------|-------------|
| `structurep` | function | Whether an object is a `defstruct` instance |
| `conditionp` | function | Whether an object is a condition |
| `condition-type-name` | function | The type name of a condition object |
| `condition-slot-value` | function | Read a slot from a condition object |

## JIT & trace toggles (AmigaOS)

The m68k JIT is on by default; these `%`-prefixed entry points toggle and inspect
it (no-ops / compiled out on the host build). Useful for A/B benchmarking and
isolating JIT bugs around a single `defun`.

| Symbol | Kind | Description |
|--------|------|-------------|
| `%jit-set-active` | function | Enable/disable JIT translation for subsequent definitions |
| `%jit-active-p` | function | Whether JIT translation is currently on |
| `%jit-set-frames` | function | Enable JIT shadow frames so the backtrace can see JIT'd frames (~5% FPS cost) |
| `%jit-frames-p` | function | Whether shadow frames are on |
| `%jit-disassemble` | function | Disassemble a function's native m68k code |
| `%jit-invoke-count` | function | How many times the JIT path was entered |
| `%trace-function` / `%untrace-function` / `%untrace-all` | function | Low-level tracing primitives behind `trace`/`untrace` |
| `%get-gc-count` | function | Number of GCs performed (diagnostics) |

```lisp
(clamiga::%jit-set-active nil)   ; define the next function bytecode-only
(defun foo (x) (* x x))
(clamiga::%jit-set-active t)
```

> Everything else exported from `CLAMIGA` (the `%…` and `*…*` names) is internal
> plumbing for `lib/boot.lisp` / `lib/clos.lisp` and the compiler — not a public
> API and subject to change.

## Source of truth

Float bits: `tests/test_float.c`. Package-local nicknames: `tests/test_package.c`.
`structurep`: `tests/test_struct.c`. JIT toggles/coverage:
`tests/amiga/test-jit.lisp`. See also the [JIT](../README.md#jit-m68k) section of
the main README.
