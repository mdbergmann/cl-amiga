#ifndef CL_READTABLE_H
#define CL_READTABLE_H

#include "types.h"

/*
 * Readtable: maps characters to syntax types and reader macro functions.
 *
 * Uses a fixed pool of 4 readtable C structs (~1.5KB each, ~6KB total).
 * *READTABLE* holds a fixnum index (0-3) into the pool.
 *
 * Slot 0: standard readtable (default, never modified by user)
 * Slot 1: initial *READTABLE* (copy of default, user-modifiable)
 * Slots 2-3: available for copy-readtable
 */

#define CL_RT_POOL_SIZE  16
#define CL_RT_CHARS      128

/* Character syntax types */
#define CL_CHAR_CONSTITUENT   0
#define CL_CHAR_WHITESPACE    1
#define CL_CHAR_TERM_MACRO    2
#define CL_CHAR_NONTERM_MACRO 3
#define CL_CHAR_ESCAPE        4
#define CL_CHAR_MULTI_ESCAPE  5

typedef struct {
    uint8_t  syntax[CL_RT_CHARS];      /* Syntax type per char */
    CL_Obj   macro_fn[CL_RT_CHARS];    /* Reader macro closure or CL_NIL (= built-in) */
    CL_Obj   dispatch_fn[CL_RT_CHARS]; /* Sub-dispatch fn for dispatch macro chars */
} CL_Readtable;

extern CL_Readtable cl_readtable_pool[CL_RT_POOL_SIZE];
extern uint32_t cl_readtable_alloc_mask; /* Bitmask: which slots are in use */

/* Initialize default (slot 0) and initial (slot 1) readtables */
void cl_readtable_init(void);

/* Get pointer to readtable at pool index */
CL_Readtable *cl_readtable_get(int idx);

/* Get the currently active readtable (from *READTABLE* symbol value) */
CL_Readtable *cl_readtable_current(void);

/* Copy readtable. If 'to' == -1, allocate a new slot. Returns slot index, or -1 on failure. */
int cl_readtable_copy(int from, int to);

/* Free a readtable slot (not 0 or 1) */
void cl_readtable_free(int idx);

/* Reclaim readtable slots not referenced by *readtable* or dynamic stack */
void cl_readtable_reclaim(void);


#endif /* CL_READTABLE_H */
