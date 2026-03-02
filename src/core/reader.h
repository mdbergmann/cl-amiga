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
    int line;          /* Current line number (1-based) */
} CL_ReadStream;

CL_Obj cl_read_from_string(CL_ReadStream *stream);

/* Check if last read hit EOF (for load loop) */
int cl_reader_eof(void);

/* Source location tracking */

/* Source location table: maps cons cell offsets to line numbers.
 * Fixed-size hash table — entries overwrite on collision. */
#define CL_SRCLOC_SIZE 2048

typedef struct {
    CL_Obj cons_obj;   /* Arena offset of cons cell (key) */
    uint16_t line;     /* Line number */
    uint16_t file_id;  /* File identifier (0 = REPL, 1+ = load files) */
} CL_SrcLoc;

extern CL_SrcLoc cl_srcloc_table[CL_SRCLOC_SIZE];

/* Look up source line for a cons cell. Returns 0 if not found. */
int cl_srcloc_lookup(CL_Obj cons_obj);

/* Current source file tracking (set by load) */
extern const char *cl_current_source_file;
extern uint16_t cl_current_file_id;

void cl_reader_init(void);

#endif /* CL_READER_H */
