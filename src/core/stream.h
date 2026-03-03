#ifndef CL_STREAM_H
#define CL_STREAM_H

#include "types.h"

/*
 * CL Stream support.
 *
 * Streams are heap objects (TYPE_STREAM) supporting three backends:
 *   CONSOLE — wraps platform_getchar/platform_write_string
 *   FILE    — wraps platform_file_* handle-based I/O
 *   STRING  — reads from CL string or writes to growable C buffer
 *
 * String output buffers live in platform_alloc'd memory (outside GC arena)
 * and are tracked via a side table indexed by out_buf_handle.
 */

/* --- Output buffer side table --- */

#define CL_STREAM_BUF_TABLE_SIZE 64

/* Initialize stream subsystem */
void cl_stream_init(void);

/* Shutdown stream subsystem (free buffers) */
void cl_stream_shutdown(void);

/* Allocate a stream object on the heap */
CL_Obj cl_make_stream(uint32_t direction, uint32_t stream_type);

/* Allocate an output buffer, return handle (0 = invalid) */
uint32_t cl_stream_alloc_outbuf(uint32_t initial_size);

/* Free an output buffer by handle */
void cl_stream_free_outbuf(uint32_t handle);

/* Get pointer to output buffer data */
char *cl_stream_outbuf_data(uint32_t handle);

/* Get output buffer length */
uint32_t cl_stream_outbuf_len(uint32_t handle);

/* Append a char to output buffer, growing if needed.
 * Updates the stream's out_buf_size on growth. */
void cl_stream_outbuf_putchar(CL_Stream *st, int ch);

/* Append a string to output buffer */
void cl_stream_outbuf_write(CL_Stream *st, const char *str);

/* Reset output buffer (for get-output-stream-string) */
void cl_stream_outbuf_reset(uint32_t handle);

#endif /* CL_STREAM_H */
