#ifndef CL_READER_H
#define CL_READER_H

#include "types.h"

/*
 * S-expression reader (parser).
 * Reads from platform I/O, returns CL_Obj.
 * Supports: lists, dotted pairs, atoms, integers, strings, characters,
 *           ' (quote), ` (quasiquote), , (unquote), ,@ (unquote-splicing),
 *           #' (function), ; (line comments)
 */

/* Read one S-expression, return CL_NIL on EOF */
CL_Obj cl_read(void);

/* Read from a string buffer (for eval-string, testing) */
typedef struct {
    const char *buf;
    int pos;
    int len;
} CL_ReadStream;

CL_Obj cl_read_from_string(CL_ReadStream *stream);

void cl_reader_init(void);

#endif /* CL_READER_H */
