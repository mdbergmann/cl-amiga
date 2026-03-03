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

/* Singleton console streams */
extern CL_Obj cl_stdin_stream;   /* Console input stream */
extern CL_Obj cl_stdout_stream;  /* Console output stream */
extern CL_Obj cl_stderr_stream;  /* Console error output stream */

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

/* --- Stream I/O operations --- */

/* Read one character from stream. Returns -1 on EOF. */
int  cl_stream_read_char(CL_Obj stream);

/* Write one character to stream. */
void cl_stream_write_char(CL_Obj stream, int ch);

/* Write a string (len bytes) to stream. */
void cl_stream_write_string(CL_Obj stream, const char *str, uint32_t len);

/* Peek at next character without consuming. Returns -1 on EOF. */
int  cl_stream_peek_char(CL_Obj stream);

/* Push a character back onto stream (only one level). */
void cl_stream_unread_char(CL_Obj stream, int ch);

/* Close a stream. */
void cl_stream_close(CL_Obj stream);

/* Create string input stream reading from string[start..end). */
CL_Obj cl_make_string_input_stream(CL_Obj string, uint32_t start, uint32_t end);

/* Create string output stream with growable buffer. */
CL_Obj cl_make_string_output_stream(void);

/* Extract accumulated string from string output stream and reset buffer. */
CL_Obj cl_get_output_stream_string(CL_Obj stream);

#endif /* CL_STREAM_H */
