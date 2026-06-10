#ifndef CL_PLATFORM_H
#define CL_PLATFORM_H

/*
 * Platform abstraction layer for CL-Amiga.
 * Every OS-specific call goes through these functions.
 * Implementations: platform_posix.c (Linux/macOS), platform_amiga.c (AmigaOS 3+)
 */

#include <stddef.h>
#include <stdint.h>

/* GCC lacks Clang's __has_feature builtin; define a no-op fallback so that
 * `__has_feature(address_sanitizer)` and similar probes parse on every
 * compiler. Must be visible wherever those probes appear. */
#ifndef __has_feature
#define __has_feature(x) 0
#endif

/* Suppress unused-parameter warnings portably.
 * vbcc warns on (void)x ("statement has no effect", warning 153),
 * gcc/clang warn on unused parameters without (void)x. */
#ifdef PLATFORM_AMIGA
#define CL_UNUSED(x)
#else
#define CL_UNUSED(x)  (void)(x)
#endif

/* Memory */
void *platform_alloc(unsigned long size);
void  platform_free(void *ptr);

/* Console I/O */
void  platform_write_string(const char *str);
int   platform_read_line(char *buf, int bufsize);
int   platform_getchar(void);
void  platform_ungetchar(int ch);
void  platform_drain_input(void);  /* Drain residual data from stdin (AmigaOS CLI leak) */
/* Non-zero iff stdin is an interactive terminal (a real console/tty).
 * Used to gate the interactive C debugger: it may only block in
 * platform_read_line when there is a human at a terminal to answer.  Returns
 * 0 when stdin is a pipe, file or /dev/null (e.g. a `tail -f /dev/null | clamiga`
 * launcher, or a SLY worker), so the debugger falls back to non-interactive
 * reporting instead of deadlocking on a read no one will ever satisfy. */
int   platform_stdin_is_interactive(void);

/* File I/O (bulk read) */
char *platform_file_read(const char *path, unsigned long *size_out);

/* Handle-based file I/O (for CL streams) */
typedef uint32_t PlatformFile;
#define PLATFORM_FILE_INVALID 0
#define PLATFORM_FILE_READ    0
#define PLATFORM_FILE_WRITE   1
#define PLATFORM_FILE_APPEND  2

PlatformFile platform_file_open(const char *path, int mode);
void         platform_file_close(PlatformFile fh);
int          platform_file_getchar(PlatformFile fh);
int          platform_file_write_string(PlatformFile fh, const char *str);
int          platform_file_write_char(PlatformFile fh, int ch);
int          platform_file_write_buf(PlatformFile fh, const char *buf, uint32_t len);
int          platform_file_flush(PlatformFile fh);
int          platform_file_eof(PlatformFile fh);
long         platform_file_position(PlatformFile fh);
int          platform_file_set_position(PlatformFile fh, long pos);
long         platform_file_length(PlatformFile fh);

/* TCP Socket I/O */
typedef uint32_t PlatformSocket;
#define PLATFORM_SOCKET_INVALID 0

PlatformSocket platform_socket_connect(const char *host, int port);
void           platform_socket_close(PlatformSocket sh);
int            platform_socket_read(PlatformSocket sh);       /* Read one byte, -1 on EOF/error */
int            platform_socket_write(PlatformSocket sh, int byte); /* Write one byte, 0=ok, -1=error */
int            platform_socket_write_buf(PlatformSocket sh, const char *buf, uint32_t len); /* Write buffer */
int            platform_socket_flush(PlatformSocket sh);      /* Flush output, 0=ok */

/* Server-side: bind to `port` and start listening.
 * loopback != 0 binds 127.0.0.1 only; otherwise INADDR_ANY (all interfaces).
 * If port == 0 the OS assigns an ephemeral port; the chosen port is written
 * to *actual_port when actual_port != NULL.
 * Returns a listener handle or PLATFORM_SOCKET_INVALID. */
PlatformSocket platform_socket_listen(int port, int loopback, int *actual_port);
/* Block until a client connects to `listener`, returning a fresh connection
 * handle (a normal read/write socket) or PLATFORM_SOCKET_INVALID on error. */
PlatformSocket platform_socket_accept(PlatformSocket listener);

/* Timing */
uint32_t platform_time_ms(void);   /* Monotonic milliseconds (for elapsed time) */
void     platform_sleep_ms(uint32_t milliseconds);
uint32_t platform_universal_time(void); /* Seconds since 1900-01-01 00:00:00 UTC */

/* File system operations */
int      platform_file_exists(const char *path);
int      platform_file_is_directory(const char *path);
int      platform_file_delete(const char *path);
int      platform_file_rename(const char *oldpath, const char *newpath);
uint32_t platform_file_mtime(const char *path); /* Universal time of last mod, 0 on error */
int      platform_mkdir(const char *path);       /* Create single directory, 0=success */

/* Directory listing — returns NULL-terminated array of names (caller frees each + array) */
char **platform_directory(const char *pattern, int *count_out);

/* Resolve path to absolute — returns static buffer or NULL on error */
const char *platform_realpath(const char *path, char *buf, int bufsize);

/* Expand leading ~ to home directory ($HOME on POSIX, PROGDIR: on Amiga).
 * Returns buf if expansion occurred, or the original path if no ~ prefix. */
const char *platform_expand_home(const char *path, char *buf, int bufsize);

/* Environment */
const char *platform_getenv(const char *name, char *buf, int bufsize);

/* Subprocess execution */
int platform_system(const char *command);

/* Current working directory (returns length, 0 on error) */
int platform_getcwd(char *buf, int bufsize);

/* Lifecycle */
void  platform_init(void);
void  platform_shutdown(void);

/* =============================================================
 * Generic FFI: foreign memory access
 * ============================================================= */

/* Foreign memory allocation/deallocation.
 * Returns a handle (POSIX: side-table index, Amiga: raw address).
 * Returns 0 on failure. */
uint32_t platform_ffi_alloc(uint32_t size);
void     platform_ffi_free(uint32_t handle, uint32_t size);

/* Resolve handle to a dereferenceable address.
 * On Amiga: identity (handle IS the address).
 * On POSIX: looks up side table.
 * Returns NULL on invalid handle. */
void    *platform_ffi_resolve(uint32_t handle);

/* Register an externally-owned real pointer (dlsym result, value returned
 * from a foreign call, computed via pointer arithmetic, etc.) so it can be
 * referenced by a 32-bit handle.  The memory is NOT owned by us and must
 * not be freed when the handle is released.
 *   POSIX: inserts PTR into the side table, returns a fresh handle.
 *          PTR == NULL returns handle 0 (the canonical null pointer).
 *   Amiga: identity — returns (uint32_t)PTR (no table needed).
 * Returns 0 on table-full / NULL ptr. */
uint32_t platform_ffi_register(void *ptr);

/* Release a handle obtained from platform_ffi_register WITHOUT freeing the
 * underlying memory (it is owned elsewhere).  Used by the GC finalizer to
 * reclaim side-table slots for transient foreign pointers.
 *   POSIX: frees the side-table slot.
 *   Amiga: no-op (handles are raw addresses; nothing to reclaim). */
void     platform_ffi_release(uint32_t handle);

/* Peek/poke at handle + byte offset.
 * The handle must come from platform_ffi_alloc or (on Amiga) a raw address. */
uint32_t platform_ffi_peek32(uint32_t handle, uint32_t offset);
uint16_t platform_ffi_peek16(uint32_t handle, uint32_t offset);
uint8_t  platform_ffi_peek8(uint32_t handle, uint32_t offset);
void     platform_ffi_poke32(uint32_t handle, uint32_t offset, uint32_t val);
void     platform_ffi_poke16(uint32_t handle, uint32_t offset, uint16_t val);
void     platform_ffi_poke8(uint32_t handle, uint32_t offset, uint8_t val);

/* =============================================================
 * Generic FFI: dynamic libraries + foreign function calls (host)
 *
 * Implemented on POSIX via dlopen/dlsym + libffi.  On Amiga these are
 * stubs that signal "unsupported" (the Amiga path uses the library-vector
 * model — platform_amiga_call — instead).
 * ============================================================= */

/* dlopen NAME (NULL = the global/default symbol namespace).  Returns a
 * handle usable with platform_ffi_dlsym / platform_ffi_dlclose, or 0 on
 * failure.  The handle is a side-table entry (POSIX) like other pointers. */
uint32_t platform_ffi_dlopen(const char *name);

/* dlsym: look up symbol NAME.  LIB_HANDLE 0 searches the default namespace.
 * Returns a side-table handle to the symbol's address, or 0 if not found. */
uint32_t platform_ffi_dlsym(uint32_t lib_handle, const char *name);

/* dlclose a handle from platform_ffi_dlopen. */
void     platform_ffi_dlclose(uint32_t lib_handle);

/* Primitive C types the generic call/marshaling layer understands. */
typedef enum {
    CL_FFI_VOID = 0,
    CL_FFI_I8,  CL_FFI_U8,
    CL_FFI_I16, CL_FFI_U16,
    CL_FFI_I32, CL_FFI_U32,
    CL_FFI_I64, CL_FFI_U64,
    CL_FFI_FLOAT, CL_FFI_DOUBLE,
    CL_FFI_POINTER
} CLFFIType;

/* A single argument/return slot, interpreted per its CLFFIType. */
typedef union {
    int8_t   i8;  uint8_t  u8;
    int16_t  i16; uint16_t u16;
    int32_t  i32; uint32_t u32;
    int64_t  i64; uint64_t u64;
    float    f;   double   d;
    void    *p;
} CLFFIValue;

/* Upper bound on argument count for a single generic foreign call. */
#define CL_FFI_MAX_ARGS 32

/* Call the C function at FN with NARGS arguments (arg_types[i]/arg_vals[i]).
 * NFIXED is the count of fixed args for a variadic call (NFIXED == NARGS for
 * a non-variadic call).  The result is written to *ret_val, interpreted per
 * RET_TYPE.  Returns 0 on success, nonzero if FFI calls are unsupported on
 * this platform or the call could not be prepared. */
int platform_ffi_call(void *fn, CLFFIType ret_type, CLFFIValue *ret_val,
                      int nargs, int nfixed,
                      const CLFFIType *arg_types, const CLFFIValue *arg_vals);

/* Callback (Lisp-as-C-function) support.
 *
 * The handler is invoked when foreign code calls the trampoline: ARGS holds
 * the decoded C arguments (one per arg type), and the handler writes the
 * result into *RET (interpreted per the closure's return type).  USER_DATA
 * is passed through verbatim. */
typedef void (*platform_ffi_cb_handler)(void *user_data,
                                        const CLFFIValue *args,
                                        CLFFIValue *ret);

/* Build an executable trampoline callable from C with the given signature.
 * Returns the callable code address (wrap it as a foreign pointer), or NULL
 * on failure / unsupported.  *OUT_CLOSURE receives an opaque handle to pass
 * to platform_ffi_free_closure. */
void *platform_ffi_make_closure(CLFFIType ret_type, int nargs,
                                const CLFFIType *arg_types,
                                platform_ffi_cb_handler handler,
                                void *user_data, void **out_closure);

/* Free a closure created by platform_ffi_make_closure. */
void  platform_ffi_free_closure(void *closure);

/* =============================================================
 * Amiga-specific FFI: shared library calls
 * ============================================================= */

/* Open/close AmigaOS shared library.
 * Returns library base as uint32_t (0 on failure).
 * On POSIX: stubs that return 0 / do nothing. */
uint32_t platform_amiga_open_library(const char *name, uint32_t version);
void     platform_amiga_close_library(uint32_t lib_base);

/* Call an AmigaOS library function via register dispatch.
 * regs[0..7] = d0..d7, regs[8..13] = a0..a5.
 * reg_mask: bitmask of which registers to load (bit 0=d0, ..., bit 13=a5).
 * Returns d0 result.
 * On POSIX: returns 0 (not supported). */
uint32_t platform_amiga_call(uint32_t lib_base, int16_t offset,
                              uint32_t *regs, uint16_t reg_mask);

/* Amiga chip memory allocation (MEMF_CHIP|MEMF_CLEAR).
 * On POSIX: same as platform_ffi_alloc. */
uint32_t platform_amiga_alloc_chip(uint32_t size);
void     platform_amiga_free_chip(uint32_t addr, uint32_t size);

/* Flush I/D caches for a freshly written code buffer.
 * Required on AmigaOS 68040/060 after emitting JIT code — calls
 * CacheClearU() so the CPU doesn't execute stale instruction-cache
 * lines.  No-op on 68020/030 and on POSIX. */
void     platform_cache_clear(void *addr, uint32_t len);

#endif /* CL_PLATFORM_H */
