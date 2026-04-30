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

/* Peek/poke at handle + byte offset.
 * The handle must come from platform_ffi_alloc or (on Amiga) a raw address. */
uint32_t platform_ffi_peek32(uint32_t handle, uint32_t offset);
uint16_t platform_ffi_peek16(uint32_t handle, uint32_t offset);
uint8_t  platform_ffi_peek8(uint32_t handle, uint32_t offset);
void     platform_ffi_poke32(uint32_t handle, uint32_t offset, uint32_t val);
void     platform_ffi_poke16(uint32_t handle, uint32_t offset, uint16_t val);
void     platform_ffi_poke8(uint32_t handle, uint32_t offset, uint8_t val);

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

#endif /* CL_PLATFORM_H */
