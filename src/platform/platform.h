#ifndef CL_PLATFORM_H
#define CL_PLATFORM_H

/*
 * Platform abstraction layer for CL-Amiga.
 * Every OS-specific call goes through these functions.
 * Implementations: platform_posix.c (Linux/macOS), platform_amiga.c (AmigaOS 3+)
 */

#include <stddef.h>

/* Memory */
void *platform_alloc(unsigned long size);
void  platform_free(void *ptr);

/* Console I/O */
void  platform_write_string(const char *str);
int   platform_read_line(char *buf, int bufsize);
int   platform_getchar(void);
void  platform_ungetchar(int ch);

/* Lifecycle */
void  platform_init(void);
void  platform_shutdown(void);

#endif /* CL_PLATFORM_H */
