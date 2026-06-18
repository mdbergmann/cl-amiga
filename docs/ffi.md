# `FFI` — Foreign-Function Interface

Foreign pointers and typed memory access on **all** platforms, plus a real,
general-purpose foreign-function engine (dynamic library loading, arbitrary C
calls with full marshaling, and Lisp-as-C callbacks) on the **POSIX host**.

- **Package:** `FFI` (uses `CL`)
- **Inherited by:** `COMMON-LISP-USER`.
- On AmigaOS, foreign *calls* go through the library-vector model in the
  [`AMIGA` / `AMIGA.FFI`](amiga.md) packages instead; the peek/poke and
  foreign-pointer parts of `FFI` work everywhere.
- The standard **CFFI** API is layered on top of this engine via the CL-Amiga
  CFFI backend, so CFFI-dependent Quicklisp systems load on the host.

```lisp
;; Resolve and call libc directly (host)
(ffi:call-foreign (ffi:symbol-pointer "pow") :double '(:double :double) '(2d0 10d0))
;; => 1024.0d0

;; Typed memory access (all platforms)
(let ((p (ffi:alloc-foreign 8)))
  (ffi:poke-u32 p 0 #xDEADBEEF)
  (prog1 (ffi:peek-u32 p 0) (ffi:free-foreign p)))
```

## Foreign pointers & allocation

| Symbol | Kind | Description |
|--------|------|-------------|
| `make-foreign-pointer` | function | Wrap a raw address as a foreign pointer; `&optional` |
| `foreign-pointer-address` | function | The integer address of a foreign pointer |
| `foreign-pointer-p` | function | Type predicate |
| `null-pointer-p` | function | Whether a pointer is null |
| `pointer-eq` | function | Address equality of two pointers |
| `pointer+` | function | Pointer plus a byte offset |
| `alloc-foreign` | function | Allocate `n` bytes of foreign memory |
| `free-foreign` | function | Free foreign memory from `alloc-foreign` |

## Typed peek / poke

`peek-*` reads from `pointer` `&optional offset`; `poke-*` writes `value` at
`pointer` `&optional offset`.

| Read | Write | Type |
|------|-------|------|
| `peek-i8` / `peek-u8` | `poke-i8` / `poke-u8` | 8-bit signed / unsigned |
| `peek-i16` / `peek-u16` | `poke-i16` / `poke-u16` | 16-bit signed / unsigned |
| `peek-i32` / `peek-u32` | `poke-i32` / `poke-u32` | 32-bit signed / unsigned |
| `peek-i64` / `peek-u64` | `poke-i64` / `poke-u64` | 64-bit signed / unsigned |
| `peek-single` / `peek-double` | `poke-single` / `poke-double` | IEEE float / double |
| `peek-pointer` | `poke-pointer` | machine-word pointer |

## Foreign strings

| Symbol | Kind | Description |
|--------|------|-------------|
| `foreign-string` | function | Copy a Lisp string into a freshly allocated C string |
| `foreign-to-string` | function | Read a NUL-terminated C string into a Lisp string; `&optional` |

## Calls, callbacks, libraries (host)

| Symbol | Kind | Description |
|--------|------|-------------|
| `load-library` | function | `dlopen` a shared library |
| `close-library` | function | `dlclose` it |
| `symbol-pointer` | function | `dlsym` — resolve a symbol to a foreign pointer; `&optional` |
| `call-foreign` | function | Call a C function: `(call-foreign ptr ret-type arg-types args &optional)` (libffi, incl. variadics) |
| `make-callback` | function | Create a C-callable callback from a Lisp function (libffi closure) |
| `free-callback` | function | Release a callback |

## Source of truth

`tests/test_ffi.c` and `trunk/load-and-test-cffi.lisp` (the CFFI backend
end-to-end). See also [Host FFI](../README.md#host-ffi-dlopen--libffi--cffi) in
the main README. Higher-level utilities (`defcstruct`, `with-foreign-alloc`) live
in `lib/ffi.lisp`.
