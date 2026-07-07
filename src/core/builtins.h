#ifndef CL_BUILTINS_H
#define CL_BUILTINS_H

#include "types.h"

/*
 * Built-in CL functions implemented in C.
 * Registered into the CL package during initialization.
 */

void cl_builtins_init(void);

/* Register a builtin function in a specific package */
void cl_register_builtin(const char *name, CL_CFunc func,
                          int min, int max, CL_Obj package);

/* COMMON-LISP package handle (defined in package.c); declared here so the
 * shared defun() helper below can register into it without pulling in
 * package.h. */
extern CL_Obj cl_package_cl;

/* Shared helper: register a builtin into the COMMON-LISP package.
 * Every builtins_*.c used to carry its own byte-identical copy of this
 * (a GC-protected intern + make_function + set-function-cell); they now
 * share this one.  static inline so each TU that does not call it emits
 * nothing and no linker symbol is produced. */
static inline void defun(const char *name, CL_CFunc func, int min, int max)
{
    cl_register_builtin(name, func, min, max, cl_package_cl);
}

/* Define a one-argument type-predicate builtin whose body is just
 * `return PRED(args[0]) ? SYM_T : CL_NIL;`.  PRED is a predicate macro such as
 * CL_CHAR_P / CL_HASHTABLE_P.  Expands to the exact hand-written body (same
 * codegen), folding the ~20 near-identical `bi_*p` one-liners across the
 * builtins_*.c files into a single declaration each. */
#define DEFINE_TYPE_PREDICATE(fn, PRED)              \
    static CL_Obj fn(CL_Obj *args, int n)            \
    {                                                \
        CL_UNUSED(n);                                \
        return PRED(args[0]) ? SYM_T : CL_NIL;       \
    }

/* Static descriptor used by table-driven init.
 * min/max are int16 to keep the entry compact (max == -1 means &rest). */
typedef struct {
    const char *name;
    CL_CFunc    func;
    int16_t     min;
    int16_t     max;
} CL_BuiltinDesc;

/* Register an entire table of builtins into the given package.
 * Replaces hundreds of per-call defun() sequences with a single loop. */
void cl_register_builtins(const CL_BuiltinDesc *table, uint32_t count,
                          CL_Obj package);

/* Coerce a function designator (function or symbol) to a callable function.
   If obj is already a function/closure/bytecode, returns it unchanged.
   If obj is a symbol, returns its function binding.
   Otherwise signals an error with the given context string. */
CL_Obj cl_coerce_funcdesig(CL_Obj obj, const char *context);

/* GENSYM a fresh uninterned symbol with the given prefix. NULL prefix
 * defaults to "G". Shares the counter with CL GENSYM. */
CL_Obj cl_gensym_with_name(const char *prefix);

/* Non-interactive inspect display: write object and its components to
 * *standard-output*. Used by (inspect …) and exposed for testing. */
void cl_inspect_show_obj(CL_Obj obj);

/* --- Sequence-function keyword validation (CLHS 3.4.1.4) --- */
/* Allowed-keyword bit flags for cl_check_seq_keywords. */
#define SK_TEST          0x0001u
#define SK_TEST_NOT      0x0002u
#define SK_KEY           0x0004u
#define SK_START         0x0008u
#define SK_END           0x0010u
#define SK_COUNT         0x0020u
#define SK_FROM_END      0x0040u
#define SK_INITIAL_VALUE 0x0080u
#define SK_START1        0x0100u
#define SK_END1          0x0200u
#define SK_START2        0x0400u
#define SK_END2          0x0800u
/* Union of every sequence keyword — used by callers that accept the full set
 * and only need :bad / odd-cell / non-symbol-key rejection. */
#define SK_ALL           0x0FFFu
/* Per-function subsets for strict CLHS keyword validation. */
#define SK_FIND_KEYS    (SK_TEST | SK_TEST_NOT | SK_KEY | SK_START | SK_END | SK_FROM_END)
#define SK_FIND_IF_KEYS (SK_KEY | SK_START | SK_END | SK_FROM_END)

/* Validate the keyword plist in args[kw_start..n).  Signals a program-error
 * for an odd number of cells, a non-symbol key, or an unrecognized keyword
 * (unless :allow-other-keys is supplied non-nil).  `allowed` is an OR of the
 * SK_* flags naming the keywords this function accepts. */
void cl_check_seq_keywords(CL_Obj *args, int n, int kw_start, unsigned allowed);

#endif /* CL_BUILTINS_H */
