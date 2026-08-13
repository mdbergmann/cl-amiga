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

| Signature | Kind | Description |
|-----------|------|-------------|
| `(open-tcp-stream host port &optional connect-timeout)` | function | Connect to `host` `port`, return a bidirectional character stream |
| `(socket-listen port &optional loopback)` | function | Open a listening server socket on a port (port `0` picks a free one; `loopback` non-`nil` binds 127.0.0.1 only) |
| `(socket-accept listener)` | function | Accept one incoming connection, returning its stream |
| `(socket-local-port listener)` | function | The local port a listening/connected socket is bound to |
| `(socket-stream-timeout stream direction)` | function | `setf`-able place: read/write deadline for a socket stream (`direction` is `:input` / `:output`, value in seconds): `(setf (socket-stream-timeout stream direction) seconds)` |
| `(%set-socket-stream-timeout stream direction seconds)` | function | Internal setter behind the `socket-stream-timeout` `setf` expander |
| `socket-timeout` | condition | Signaled when a socket read/write exceeds its deadline (subtype of `stream-error`) |
| `(open-udp-stream host port)` | function | Create a UDP (datagram) socket aimed at `host` `port` |
| `(udp-stream-send stream buffer &optional length)` | function | Send one datagram from byte vector `buffer` (`length` defaults to its full length) |
| `(udp-stream-receive stream buffer &optional max-length)` | function | Receive one datagram into byte vector `buffer`; returns the received length |
| `(socket-stream-local-endpoint stream)` | function | Local `(address . port)` a socket is bound to |

## TLS

A connected TCP socket stream can be upgraded to TLS **in place**: after
`socket-start-tls` the very same stream object carries the encrypted
connection, so any wrapper already holding it (flexi-streams, chunga,
usocket) keeps working.  The provider is loaded lazily at runtime and is
optional — OpenSSL 1.1.1/3.x via dlopen on the host, AmiSSL v5 on
AmigaOS (also the MorphOS route for now), so `tls-available-p` is the
honest capability gate.  drakma and Hunchentoot consume this layer through
the bundled cl+ssl facade (`contrib/shims/cl+ssl`, installed by
`make install-shims`).  End-to-end examples: `tests/tls-loopback.lisp`
(host), `tests/amiga/tls-tests.lisp` (Amiga), `tests/test_tls.c`
(platform layer).

```lisp
;; HTTPS-style client with certificate + hostname verification
(let ((s (ext:open-tcp-stream "example.com" 443 10)))
  (ext:socket-start-tls s :hostname "example.com")   ; :verify t is the default
  (write-string "GET / HTTP/1.0 ..." s)
  ...)

;; Server side (certificate required; key defaults to the same file)
(ext:socket-start-tls conn :server t
                           :certificate "certs/server.pem"
                           :key "certs/server-key.pem")
```

| Signature | Kind | Description |
|-----------|------|-------------|
| `(tls-available-p)` | function | True when a TLS provider is present (loads it on first call) |
| `(tls-version)` | function | Provider identification string (`"OpenSSL 3.x ..."`, `"AmiSSL 5.27"`), or `nil` |
| `(socket-start-tls stream &key server hostname (verify t) certificate key key-password ca-file ca-path (timeout 30))` | function | Upgrade a connected TCP socket stream to TLS in place and return it. Client by default (`hostname` sets SNI and, with `verify`, hostname checking; `ca-file`/`ca-path` name trust anchors, defaulting to the provider's store). `:server t` accepts instead — then `certificate` is required. Handshake failure (including certificate rejection) signals an error with the provider's diagnostic |
| `(socket-tls-p stream)` | function | True when `stream` is already TLS-upgraded |
| `(tls-peer-certificate stream)` | function | Peer certificate as a plist `(:subject :issuer :not-before :not-after)`, or `nil` |
| `(%socket-start-tls stream server hostname verify cert key password ca-file ca-path timeout)` | function | Positional core behind `socket-start-tls` |

## GC, environment, host

| Signature | Kind | Description |
|-----------|------|-------------|
| `(gc)` | function | Force a garbage collection (and compaction when fragmented) |
| `(getenv name)` | function | Read an environment variable |
| `(getcwd)` | function | Current working directory |
| `(system-command command)` | function | Run a host/AmigaOS shell command |
| `(defglobal name value &optional doc)` | macro | Define a global (non-dynamic) variable |

## Bulk byte-vector operations

C-speed loops over `(unsigned-byte 8)` vectors for binary file formats —
per-byte Lisp loops cost a VM round-trip per byte, which is prohibitive on a
14MHz 68020 (decoding one IFF ILBM image took seconds).  Standard functions
`read-sequence` / `write-sequence` / `replace` / `map-into` already take C
fast paths on byte vectors; these two cover the decode and reshuffle steps
that have no standard equivalent:

```lisp
;; ByteRun1/PackBits RLE decode (IFF ILBM BODY, TIFF, MacPaint):
;; decode from SRC[pos..end) until DST-LEN bytes land in DST at DST-START;
;; returns the new source position.  Signals on truncated/overlong data.
(ext:unpack-byterun1 src pos end dst dst-len &optional (dst-start 0))

;; Strided row copy — the gather/scatter step for interleaved formats:
;; row I goes from SRC[src-start + I*src-stride ...) to
;; DST[dst-start + I*dst-stride ...), CHUNK bytes per row.  Returns DST.
(ext:copy-rows dst src count chunk dst-start dst-stride src-start src-stride)

;; Together they decode a whole interleaved ILBM BODY in one call and pull
;; each bitplane out with one call per plane:
(ext:unpack-byterun1 body 0 (length body) buf (length buf))
(dotimes (p depth)
  (ext:copy-rows (aref planes p) buf height row-bytes
                 0 row-bytes (* p row-bytes) (* n-planes row-bytes)))
```

| Signature | Kind | Description |
|-----------|------|-------------|
| `(unpack-byterun1 src pos end dst dst-len &optional dst-start)` | function | Decode ByteRun1/PackBits RLE data between byte vectors; returns the new source position |
| `(copy-rows dst src count chunk dst-start dst-stride src-start src-stride)` | function | Copy `count` rows of `chunk` bytes with independent source/destination strides; returns `dst` |

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

| Signature | Kind | Description |
|-----------|------|-------------|
| `(tty-p)` | function | `T` iff stdin is an interactive terminal/console |
| `(tty-raw-mode enable)` | function | Enable (`t`) / disable (`nil`) raw mode; returns `T` on success |
| `(tty-size)` | function | Terminal size as `(cols . rows)`, or `NIL` when unknown |

## Introspection / debugging (SLY backend)

| Signature | Kind | Description |
|-----------|------|-------------|
| `(backtrace &optional max-frames)` | function | Capture the current call stack (used by the debugger / SLDB); `max-frames` limits the depth |
| `(frame-locals frame-index)` | function | Local variable bindings of a stack frame |
| `(function-arglist fn)` | function | Lambda list of a function, for completion/arglist display |
| `(function-source-location fn)` | function | Source file/position of a definition (`M-.`) |

## Source of truth

- Bulk byte-vector operations: `tests/test_byte_vector.c` (the
  `unpack_byterun1_*` and `copy_rows_*` tests) and the matching blocks in
  `tests/amiga/run-tests.lisp`; the ILBM loader in the Lambda's Tale
  engine repo (`src/ilbm.lisp`) is the worked example.
- TCP sockets & timeouts: `tests/test_stream.c`
  (`platform_socket_table_grows_many_connections`, `socket_read_timeout_*`,
  `eval_socket_stream_timeout_*`) and `tests/amiga/run-tests.lisp` (the
  `socket-listen` / `socket-accept` / `socket-local-port` block).
- Introspection: `tests/test_backtrace.c`, `tests/test_arglist.c`,
  `tests/test_srcloc.c`, and the `ext:backtrace` probes in
  `tests/amiga/run-tests.lisp`.

See also the [Emacs (SLY) integration](../README.md#emacs-sly-integration) and
[TCP networking](../README.md#architecture) sections of the main README.
