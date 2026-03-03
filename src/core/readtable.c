#include "readtable.h"
#include "symbol.h"
#include "mem.h"
#include <string.h>

CL_Readtable cl_readtable_pool[CL_RT_POOL_SIZE];
uint32_t cl_readtable_alloc_mask = 0;

void cl_readtable_init(void)
{
    CL_Readtable *std;
    int i;

    memset(cl_readtable_pool, 0, sizeof(cl_readtable_pool));
    cl_readtable_alloc_mask = 0;

    /* Set up slot 0: standard readtable */
    std = &cl_readtable_pool[0];

    /* Default: all constituent */
    for (i = 0; i < CL_RT_CHARS; i++) {
        std->syntax[i] = CL_CHAR_CONSTITUENT;
        std->macro_fn[i] = CL_NIL;
        std->dispatch_fn[i] = CL_NIL;
    }

    /* Whitespace characters */
    std->syntax[' ']  = CL_CHAR_WHITESPACE;
    std->syntax['\t'] = CL_CHAR_WHITESPACE;
    std->syntax['\n'] = CL_CHAR_WHITESPACE;
    std->syntax['\r'] = CL_CHAR_WHITESPACE;
    std->syntax['\f'] = CL_CHAR_WHITESPACE;

    /* Terminating macro characters (macro_fn = CL_NIL means built-in) */
    std->syntax['(']  = CL_CHAR_TERM_MACRO;
    std->syntax[')']  = CL_CHAR_TERM_MACRO;
    std->syntax['\''] = CL_CHAR_TERM_MACRO;
    std->syntax[';']  = CL_CHAR_TERM_MACRO;
    std->syntax['"']  = CL_CHAR_TERM_MACRO;
    std->syntax['`']  = CL_CHAR_TERM_MACRO;
    std->syntax[',']  = CL_CHAR_TERM_MACRO;

    /* Non-terminating macro character: # */
    std->syntax['#']  = CL_CHAR_NONTERM_MACRO;

    /* Single escape: backslash */
    std->syntax['\\'] = CL_CHAR_ESCAPE;

    /* Multiple escape: pipe */
    std->syntax['|']  = CL_CHAR_MULTI_ESCAPE;

    /* Copy slot 0 -> slot 1 (user-modifiable initial readtable) */
    memcpy(&cl_readtable_pool[1], &cl_readtable_pool[0], sizeof(CL_Readtable));

    /* Mark slots 0 and 1 as allocated */
    cl_readtable_alloc_mask = 0x03; /* bits 0 and 1 */
}

CL_Readtable *cl_readtable_get(int idx)
{
    if (idx < 0 || idx >= CL_RT_POOL_SIZE)
        return &cl_readtable_pool[1]; /* fallback to default user */
    return &cl_readtable_pool[idx];
}

CL_Readtable *cl_readtable_current(void)
{
    CL_Obj rt_val;
    int idx;
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_STAR_READTABLE);
    rt_val = sym->value;
    if (CL_FIXNUM_P(rt_val)) {
        idx = CL_FIXNUM_VAL(rt_val);
        if (idx >= 0 && idx < CL_RT_POOL_SIZE)
            return &cl_readtable_pool[idx];
    }
    return &cl_readtable_pool[1]; /* fallback */
}

int cl_readtable_copy(int from, int to)
{
    if (from < 0 || from >= CL_RT_POOL_SIZE)
        return -1;

    if (to == -1) {
        /* Allocate a new slot */
        int i;
        for (i = 2; i < CL_RT_POOL_SIZE; i++) {
            if (!(cl_readtable_alloc_mask & (1u << i))) {
                to = i;
                break;
            }
        }
        if (to == -1)
            return -1; /* No free slots */
    }

    if (to < 0 || to >= CL_RT_POOL_SIZE)
        return -1;

    memcpy(&cl_readtable_pool[to], &cl_readtable_pool[from], sizeof(CL_Readtable));
    cl_readtable_alloc_mask |= (1u << to);
    return to;
}

void cl_readtable_free(int idx)
{
    if (idx < 2 || idx >= CL_RT_POOL_SIZE)
        return; /* Don't free slots 0 or 1 */
    cl_readtable_alloc_mask &= ~(1u << idx);
}

