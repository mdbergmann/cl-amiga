# `EXT` — CL-Amiga Extensions

Non-standard utilities that don't belong in `COMMON-LISP`: TCP networking, GC
control, environment/host access, and runtime introspection used by the SLY
backend.

- **Package:** `EXT` (uses `CL`)
- **Inherited by:** `COMMON-LISP-USER`, so `ext:` symbols are usually available
  unqualified at the REPL.

## TCP networking

The socket layer is BSD sockets on POSIX and `bsdsocket.library` on AmigaOS. A
client connects with `open-tcp-stream`; a server listens with `socket-listen`,
then `socket-accept`s connections. Socket streams support per-connection
read/write deadlines via `socket-stream-timeout` — a timed-out operation signals
`ext:socket-timeout` (a subtype of `stream-error`) instead of blocking forever.

```lisp
;; Client
(let ((s (ext:open-tcp-stream "example.com" 80)))
  (format s "GET / HTTP/1.0~c~c~c~c" #\Return #\Linefeed #\Return #\Linefeed)
  (force-output s)
  (read-line s))

;; Per-connection read timeout (seconds; nil clears it)
(setf (ext:socket-stream-timeout s :input) 5)
```

| Symbol | Kind | Description |
|--------|------|-------------|
| `open-tcp-stream` | function | Connect to `host` `port`, return a bidirectional character stream |
| `socket-listen` | function | Open a listening server socket on a port |
| `socket-accept` | function | Accept one incoming connection, returning its stream |
| `socket-local-port` | function | The local port a listening/connected socket is bound to |
| `socket-stream-timeout` | function | `setf`-able place: read/write deadline for a socket stream (`:input` / `:output`, seconds) |
| `%set-socket-stream-timeout` | function | Internal setter behind the `socket-stream-timeout` `setf` expander |
| `socket-timeout` | condition | Signaled when a socket read/write exceeds its deadline (subtype of `stream-error`) |

## GC, environment, host

| Symbol | Kind | Description |
|--------|------|-------------|
| `gc` | function | Force a garbage collection (and compaction when fragmented) |
| `getenv` | function | Read an environment variable |
| `getcwd` | function | Current working directory |
| `system-command` | function | Run a host/AmigaOS shell command |
| `defglobal` | macro | Define a global (non-dynamic) variable |

## Terminal control (TUI raw mode)

Primitives for full-screen terminal applications (e.g. the
[cl-tuition](https://github.com/atgreen/cl-tuition) TUI library): raw mode
turns off echo and line buffering so single keypresses arrive immediately,
and while it is active `listen` / `read-char-no-hang` on the console report
input availability exactly — a TUI input loop can poll without blocking.
POSIX hosts use termios; AmigaOS uses the console handler's raw mode.
See `tests/test_tty.c` for a complete usage example.

```lisp
(when (ext:tty-p)
  (ext:tty-raw-mode t)                    ; no echo, keys arrive as typed
  (unwind-protect
       (loop for ch = (read-char-no-hang) ; NIL until a key is pressed
             until (eql ch #\q))
    (ext:tty-raw-mode nil)))              ; always restore cooked mode

(ext:tty-size)                            ; => (cols . rows), or NIL
```

| Symbol | Kind | Description |
|--------|------|-------------|
| `tty-p` | function | `T` iff stdin is an interactive terminal/console |
| `tty-raw-mode` | function | Enable (`t`) / disable (`nil`) raw mode; returns `T` on success |
| `tty-size` | function | Terminal size as `(cols . rows)`, or `NIL` when unknown |

## Introspection / debugging (SLY backend)

| Symbol | Kind | Description |
|--------|------|-------------|
| `backtrace` | function | Capture the current call stack (used by the debugger / SLDB) |
| `frame-locals` | function | Local variable bindings of a stack frame |
| `function-arglist` | function | Lambda list of a function, for completion/arglist display |
| `function-source-location` | function | Source file/position of a definition (`M-.`) |

## Source of truth

- TCP sockets & timeouts: `tests/test_stream.c`
  (`platform_socket_table_grows_many_connections`, `socket_read_timeout_*`,
  `eval_socket_stream_timeout_*`) and `tests/amiga/run-tests.lisp` (the
  `socket-listen` / `socket-accept` / `socket-local-port` block).
- Introspection: `tests/test_backtrace.c`, `tests/test_arglist.c`,
  `tests/test_srcloc.c`, and the `ext:backtrace` probes in
  `tests/amiga/run-tests.lisp`.

See also the [Emacs (SLY) integration](../README.md#emacs-sly-integration) and
[TCP networking](../README.md#architecture) sections of the main README.
