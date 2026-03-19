#include "readtable.h"
#include "symbol.h"
#include "vm.h"
#include "mem.h"
#include "thread.h"
#include "../platform/platform_thread.h"
#include <string.h>

CL_Readtable cl_readtable_pool[CL_RT_POOL_SIZE];
uint32_t cl_readtable_alloc_mask = 0;
void *cl_readtable_rwlock = NULL;

void cl_readtable_init(void)
{
    CL_Readtable *std;
    int i;

    if (!cl_readtable_rwlock)
        platform_rwlock_init(&cl_readtable_rwlock);

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
    rt_val = cl_symbol_value(SYM_STAR_READTABLE);
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

    if (CL_MT()) platform_rwlock_wrlock(cl_readtable_rwlock);

    if (to == -1) {
        /* Allocate a new slot */
        int i;
        for (i = 2; i < CL_RT_POOL_SIZE; i++) {
            if (!(cl_readtable_alloc_mask & (1u << i))) {
                to = i;
                break;
            }
        }
        if (to == -1) {
            /* Try to reclaim unreferenced slots */
            cl_readtable_reclaim();
            for (i = 2; i < CL_RT_POOL_SIZE; i++) {
                if (!(cl_readtable_alloc_mask & (1u << i))) {
                    to = i;
                    break;
                }
            }
        }
        if (to == -1) {
            if (CL_MT()) platform_rwlock_unlock(cl_readtable_rwlock);
            return -1; /* No free slots */
        }
    }

    if (to < 0 || to >= CL_RT_POOL_SIZE) {
        if (CL_MT()) platform_rwlock_unlock(cl_readtable_rwlock);
        return -1;
    }

    memcpy(&cl_readtable_pool[to], &cl_readtable_pool[from], sizeof(CL_Readtable));
    cl_readtable_alloc_mask |= (1u << to);
    if (CL_MT()) platform_rwlock_unlock(cl_readtable_rwlock);
    return to;
}

void cl_readtable_free(int idx)
{
    if (idx < 2 || idx >= CL_RT_POOL_SIZE)
        return; /* Don't free slots 0 or 1 */
    if (CL_MT()) platform_rwlock_wrlock(cl_readtable_rwlock);
    cl_readtable_alloc_mask &= ~(1u << idx);
    if (CL_MT()) platform_rwlock_unlock(cl_readtable_rwlock);
}

/* Reclaim readtable slots not referenced by *readtable* or
   any saved binding on the dynamic binding stack. */
void cl_readtable_reclaim(void)
{
    uint32_t in_use = 0x03; /* slots 0 and 1 are always in use */
    int i, idx;
    CL_Obj rt_cur;

    /* Mark current *readtable* value */
    rt_cur = cl_symbol_value(SYM_STAR_READTABLE);
    if (CL_FIXNUM_P(rt_cur)) {
        idx = CL_FIXNUM_VAL(rt_cur);
        if (idx >= 0 && idx < CL_RT_POOL_SIZE)
            in_use |= (1u << idx);
    }

    /* Mark slots referenced by saved bindings on the dynamic stack */
    for (i = 0; i < cl_dyn_top; i++) {
        if (cl_dyn_stack[i].symbol == SYM_STAR_READTABLE) {
            CL_Obj val = cl_dyn_stack[i].old_value;
            if (CL_FIXNUM_P(val)) {
                idx = CL_FIXNUM_VAL(val);
                if (idx >= 0 && idx < CL_RT_POOL_SIZE)
                    in_use |= (1u << idx);
            }
        }
    }

    /* Free any allocated slot that's not in use */
    cl_readtable_alloc_mask &= in_use;
}

