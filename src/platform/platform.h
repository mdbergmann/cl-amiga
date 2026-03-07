#ifndef CL_PLATFORM_H
#define CL_PLATFORM_H

/*
 * Platform abstraction layer for CL-Amiga.
 * Every OS-specific call goes through these functions.
 * Implementations: platform_posix.c (Linux/macOS), platform_amiga.c (AmigaOS 3+)
 */

#include <stddef.h>
#include <stdint.h>

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
int          platform_file_eof(PlatformFile fh);

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

/* Environment */
const char *platform_getenv(const char *name, char *buf, int bufsize);

/* Subprocess execution */
int platform_system(const char *command);

/* Current working directory (returns length, 0 on error) */
int platform_getcwd(char *buf, int bufsize);

/* Lifecycle */
void  platform_init(void);
void  platform_shutdown(void);

#endif /* CL_PLATFORM_H */
