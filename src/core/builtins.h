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

/* Classify a general (non-bit, non-char) array/vector element-type symbol into
 * a CL_VEC_ELT_* code, expanding user deftypes up to DEPTH levels.  Recognizes
 * only the numeric types clamiga specializes (FIXNUM, SINGLE/DOUBLE-FLOAT);
 * everything else → CL_VEC_ELT_T.  Defined in builtins_array.c and shared with
 * COERCE in builtins_type.c. */
uint8_t cl_classify_vec_elt_code(CL_Obj type, int depth);

/* Classify an array element-type specifier into the specialized storage
 * classes make-array uses, expanding user deftypes up to DEPTH levels:
 * *is_char (CHARACTER subtype → string), *is_wide_char, *is_bit
 * (→ bit-vector), *is_u8 / *is_s8 (→ packed byte vector; specialization
 * order bit > u8 > s8).  Flags it cannot determine stay untouched; T, * and
 * unrecognized specifiers set nothing (general storage).  Defined in
 * builtins_array.c; shared with TYPEP/SUBTYPEP in builtins_type.c so type
 * tests agree with what MAKE-ARRAY actually builds.
 * GC: may apply deftype expanders (cl_vm_apply) → objects can move; callers
 * must GC-protect live CL_Obj locals across the call. */
void cl_classify_array_elt_type(CL_Obj type, int depth,
                                int *is_char, int *is_wide_char, int *is_bit,
                                int *is_u8, int *is_s8,
                                int *is_u16, int *is_s16);

/* Range-check a packed-vector element VALUE against the vector's width
 * (elt_shift: 0 = 8-bit, 1 = 16-bit) and signedness; returns the raw value
 * for cl_bytevec_set or signals a catchable TYPE-ERROR.  Shared by every
 * byte-vector store path (arrays, sequences, VM/JIT ASET). */
int32_t cl_bytevec_check_value(CL_Obj value, int is_signed, int elt_shift,
                               const char *ctx);
/* "(UNSIGNED-BYTE 8)" etc. — the exact specifier name for diagnostics. */
const char *cl_bytevec_type_name(int is_signed, int elt_shift);

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
