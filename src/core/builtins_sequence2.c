#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include <string.h>

/* From compiler.c — expand a (def)type name/specifier; CL_NIL if none. */
extern CL_Obj cl_get_type_expander(CL_Obj name);

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
}

/* --- Pre-interned keyword symbols --- */

static CL_Obj KW_TEST = CL_NIL;
static CL_Obj KW_TEST_NOT = CL_NIL;
static CL_Obj KW_KEY = CL_NIL;
static CL_Obj KW_START1 = CL_NIL;
static CL_Obj KW_END1 = CL_NIL;
static CL_Obj KW_START2 = CL_NIL;
static CL_Obj KW_END2 = CL_NIL;
static CL_Obj KW_FROM_END = CL_NIL;
static CL_Obj SYM_EQL_FN = CL_NIL;
static CL_Obj SYM_LIST = CL_NIL;

/* --- Shared helpers (same as builtins_sequence.c, static per file) --- */

static CL_Obj call_func(CL_Obj func, CL_Obj *call_args, int nargs)
{
    /* Resolve symbol function designator */
    if (CL_SYMBOL_P(func)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(func);
        func = s->function;
    }
    if (CL_FUNCTION_P(func) || CL_BYTECODE_P(func) || CL_CLOSURE_P(func))
        /* cl_vm_apply GC-roots call_args across the call (the function may
         * compact while reading its own args). */
        return cl_vm_apply(func, call_args, nargs);
    cl_error(CL_ERR_TYPE, "not a function");
    return CL_NIL;
}

static CL_Obj call_test(CL_Obj test_fn, CL_Obj a, CL_Obj b)
{
    CL_Obj targs[2];
    targs[0] = a;
    targs[1] = b;
    return call_func(test_fn, targs, 2);
}

static CL_Obj call_1(CL_Obj fn, CL_Obj arg)
{
    CL_Obj pargs[1];
    pargs[0] = arg;
    return call_func(fn, pargs, 1);
}

static CL_Obj apply_key(CL_Obj key_fn, CL_Obj elem)
{
    if (CL_NULL_P(key_fn)) return elem;
    return call_1(key_fn, elem);
}

static int32_t seq_length(CL_Obj seq)
{
    if (CL_NULL_P(seq)) return 0;
    if (CL_CONS_P(seq)) {
        int32_t len = 0;
        while (!CL_NULL_P(seq)) { len++; seq = cl_cdr(seq); }
        return len;
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        return (int32_t)cl_vector_active_length(v);
    }
    if (CL_ANY_STRING_P(seq)) {
        return (int32_t)cl_string_length(seq);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        return (int32_t)cl_bv_active_length(bv);
    }
    cl_error(CL_ERR_TYPE, "not a sequence");
    return 0;
}

static CL_Obj seq_elt(CL_Obj seq, int32_t idx)
{
    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        while (idx > 0 && !CL_NULL_P(seq)) { seq = cl_cdr(seq); idx--; }
        return CL_NULL_P(seq) ? CL_NIL : cl_car(seq);
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        if ((uint32_t)idx >= cl_vector_active_length(v)) cl_error(CL_ERR_ARGS, "index out of bounds");
        return cl_vector_data(v)[idx];
    }
    if (CL_ANY_STRING_P(seq)) {
        if ((uint32_t)idx >= cl_string_length(seq)) cl_error(CL_ERR_ARGS, "index out of bounds");
        return CL_MAKE_CHAR(cl_string_char_at(seq, (uint32_t)idx));
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        if ((uint32_t)idx >= cl_bv_active_length(bv)) cl_error(CL_ERR_ARGS, "index out of bounds");
        return CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)idx));
    }
    cl_error(CL_ERR_TYPE, "not a sequence");
    return CL_NIL;
}

/* ======================================================= */
/* EVERY / SOME / NOTANY / NOTEVERY                        */
/* ======================================================= */

/* Helper: get sequence length for any sequence type */
static int32_t every_seq_len(CL_Obj seq)
{
    if (CL_NULL_P(seq)) return 0;
    if (CL_ANY_STRING_P(seq))
        return (int32_t)cl_string_length(seq);
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        return (int32_t)cl_vector_active_length(v);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        return (int32_t)cl_bv_active_length(bv);
    }
    return -1; /* list — length unknown */
}

/* Helper: get element at index for non-list sequences */
static CL_Obj every_seq_elt(CL_Obj seq, int32_t idx)
{
    if (CL_ANY_STRING_P(seq)) {
        return CL_MAKE_CHAR(cl_string_char_at(seq, (uint32_t)idx));
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        return cl_vector_data(v)[idx];
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        return CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)idx));
    }
    return CL_NIL;
}

static CL_Obj bi_every(CL_Obj *args, int n)
{
    CL_Obj pred = cl_coerce_funcdesig(args[0], "EVERY");
    int nseqs = n - 1;
    CL_Obj seqs[16];    /* current list tails (for list seqs) or original seq */
    int32_t lens[16];   /* -1 for list, >= 0 for vector/string */
    CL_Obj call_args[16];
    int i;
    int32_t idx = 0;
    CL_Obj last_result = SYM_T;

    if (nseqs > 16) nseqs = 16;
    for (i = 0; i < nseqs; i++) {
        seqs[i] = args[i + 1];
        lens[i] = every_seq_len(seqs[i]);
    }

    for (;;) {
        /* Check exhaustion */
        for (i = 0; i < nseqs; i++) {
            if (lens[i] >= 0) {
                if (idx >= lens[i]) return last_result;
            } else {
                if (CL_NULL_P(seqs[i])) return last_result;
            }
        }
        /* Collect elements */
        for (i = 0; i < nseqs; i++) {
            if (lens[i] >= 0) {
                call_args[i] = every_seq_elt(args[i + 1], idx);
            } else {
                call_args[i] = cl_car(seqs[i]);
                seqs[i] = cl_cdr(seqs[i]);
            }
        }
        idx++;
        last_result = call_func(pred, call_args, nseqs);
        if (CL_NULL_P(last_result)) return CL_NIL;
    }
}

static CL_Obj bi_some(CL_Obj *args, int n)
{
    CL_Obj pred = cl_coerce_funcdesig(args[0], "SOME");
    int nseqs = n - 1;
    CL_Obj seqs[16];
    int32_t lens[16];
    CL_Obj call_args[16];
    int i;
    int32_t idx = 0;

    if (nseqs > 16) nseqs = 16;
    for (i = 0; i < nseqs; i++) {
        seqs[i] = args[i + 1];
        lens[i] = every_seq_len(seqs[i]);
    }

    for (;;) {
        CL_Obj result;
        for (i = 0; i < nseqs; i++) {
            if (lens[i] >= 0) {
                if (idx >= lens[i]) return CL_NIL;
            } else {
                if (CL_NULL_P(seqs[i])) return CL_NIL;
            }
        }
        for (i = 0; i < nseqs; i++) {
            if (lens[i] >= 0) {
                call_args[i] = every_seq_elt(args[i + 1], idx);
            } else {
                call_args[i] = cl_car(seqs[i]);
                seqs[i] = cl_cdr(seqs[i]);
            }
        }
        idx++;
        result = call_func(pred, call_args, nseqs);
        if (!CL_NULL_P(result)) return result;
    }
}

static CL_Obj bi_notany(CL_Obj *args, int n)
{
    /* (notany pred seqs...) = (not (some pred seqs...)) */
    CL_Obj result = bi_some(args, n);
    return CL_NULL_P(result) ? SYM_T : CL_NIL;
}

static CL_Obj bi_notevery(CL_Obj *args, int n)
{
    /* (notevery pred seqs...) = (not (every pred seqs...)) */
    CL_Obj result = bi_every(args, n);
    return CL_NULL_P(result) ? SYM_T : CL_NIL;
}

/* ======================================================= */
/* MAP                                                     */
/* ======================================================= */

/* Helper: check if any sequence is exhausted */
static int map_exhausted(CL_Obj *seqs, int32_t *lens, int nseqs, int32_t idx)
{
    int i;
    for (i = 0; i < nseqs; i++) {
        if (lens[i] >= 0) {
            if (idx >= lens[i]) return 1;
        } else {
            if (CL_NULL_P(seqs[i])) return 1;
        }
    }
    return 0;
}

/* Helper: collect elements from sequences at current index */
static void map_collect_args(CL_Obj *seqs, int32_t *lens, CL_Obj *orig_seqs,
                             CL_Obj *call_args, int nseqs, int32_t idx)
{
    int i;
    for (i = 0; i < nseqs; i++) {
        if (lens[i] >= 0) {
            call_args[i] = every_seq_elt(orig_seqs[i], idx);
        } else {
            call_args[i] = cl_car(seqs[i]);
            seqs[i] = cl_cdr(seqs[i]);
        }
    }
}

/* Helper: match result-type symbol name */
/* True if TYPE denotes a character subtype (CHARACTER/BASE-CHAR/... or a
 * deftype that expands to one).  Used to decide whether a (vector ...)
 * result-type should yield a string. DEPTH bounds deftype recursion. */
static int seq_elt_type_is_char(CL_Obj type, int depth)
{
    if (depth <= 0) return 0;
    if (CL_SYMBOL_P(type)) {
        const char *nm = cl_symbol_name(type);
        if (strcmp(nm, "CHARACTER") == 0 || strcmp(nm, "BASE-CHAR") == 0 ||
            strcmp(nm, "STANDARD-CHAR") == 0 || strcmp(nm, "EXTENDED-CHAR") == 0)
            return 1;
        if (strcmp(nm, "*") == 0) return 0;
        {
            CL_Obj ex = cl_get_type_expander(type);
            if (!CL_NULL_P(ex))
                return seq_elt_type_is_char(cl_vm_apply(ex, NULL, 0), depth - 1);
        }
    }
    return 0;
}

/* True if TYPE denotes the BIT type (or a deftype expanding to it). */
static int seq_elt_type_is_bit(CL_Obj type, int depth)
{
    if (depth <= 0) return 0;
    if (CL_SYMBOL_P(type)) {
        const char *nm = cl_symbol_name(type);
        if (strcmp(nm, "BIT") == 0) return 1;
        if (strcmp(nm, "*") == 0) return 0;
        {
            CL_Obj ex = cl_get_type_expander(type);
            if (!CL_NULL_P(ex))
                return seq_elt_type_is_bit(cl_vm_apply(ex, NULL, 0), depth - 1);
        }
    }
    return 0;
}

/* Classify a sequence constructor result-type (for MERGE / MAKE-SEQUENCE).
 *   0 = nil/null, 1 = list, 2 = string, 3 = (general) vector,
 *   4 = bit-vector, -1 = unknown.
 * *len_out is set to the declared length, or -1 when unspecified / `*`.
 * DEPTH bounds deftype-expansion recursion. */
static int seq_ctor_type_class(CL_Obj rt, int depth, int32_t *len_out)
{
    *len_out = -1;
    if (depth <= 0) return -1;
    if (CL_NULL_P(rt)) return 0;
    if (CL_SYMBOL_P(rt)) {
        const char *nm = cl_symbol_name(rt);
        if (strcmp(nm, "NULL") == 0) return 0;
        if (strcmp(nm, "LIST") == 0 || strcmp(nm, "CONS") == 0) return 1;
        if (strcmp(nm, "STRING") == 0 || strcmp(nm, "SIMPLE-STRING") == 0 ||
            strcmp(nm, "BASE-STRING") == 0 || strcmp(nm, "SIMPLE-BASE-STRING") == 0)
            return 2;
        if (strcmp(nm, "BIT-VECTOR") == 0 || strcmp(nm, "SIMPLE-BIT-VECTOR") == 0)
            return 4;
        if (strcmp(nm, "VECTOR") == 0 || strcmp(nm, "SIMPLE-VECTOR") == 0 ||
            strcmp(nm, "ARRAY") == 0 || strcmp(nm, "SIMPLE-ARRAY") == 0)
            return 3;
        {
            CL_Obj ex = cl_get_type_expander(rt);
            if (!CL_NULL_P(ex))
                return seq_ctor_type_class(cl_vm_apply(ex, NULL, 0), depth - 1, len_out);
        }
        return -1;
    }
    if (CL_CONS_P(rt)) {
        CL_Obj head = cl_car(rt);
        if (CL_SYMBOL_P(head)) {
            const char *nm = cl_symbol_name(head);
            CL_Obj rest = cl_cdr(rt);
            int cls = -1;
            CL_Obj lenarg = CL_NIL;
            if (strcmp(nm, "LIST") == 0 || strcmp(nm, "CONS") == 0) return 1;
            if (strcmp(nm, "STRING") == 0 || strcmp(nm, "SIMPLE-STRING") == 0 ||
                strcmp(nm, "BASE-STRING") == 0 || strcmp(nm, "SIMPLE-BASE-STRING") == 0) {
                cls = 2;
                lenarg = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
            } else if (strcmp(nm, "BIT-VECTOR") == 0 ||
                       strcmp(nm, "SIMPLE-BIT-VECTOR") == 0) {
                cls = 4;
                lenarg = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
            } else if (strcmp(nm, "VECTOR") == 0 || strcmp(nm, "SIMPLE-VECTOR") == 0) {
                /* (vector [elt-type [length]]) */
                CL_Obj eltarg = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
                lenarg = (CL_NULL_P(rest) || CL_NULL_P(cl_cdr(rest)))
                             ? CL_NIL : cl_car(cl_cdr(rest));
                if (!CL_NULL_P(eltarg) && seq_elt_type_is_char(eltarg, 8)) cls = 2;
                else if (!CL_NULL_P(eltarg) && seq_elt_type_is_bit(eltarg, 8)) cls = 4;
                else cls = 3;
            } else if (strcmp(nm, "ARRAY") == 0 || strcmp(nm, "SIMPLE-ARRAY") == 0) {
                CL_Obj eltarg = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
                if (!CL_NULL_P(eltarg) && seq_elt_type_is_char(eltarg, 8)) cls = 2;
                else if (!CL_NULL_P(eltarg) && seq_elt_type_is_bit(eltarg, 8)) cls = 4;
                else cls = 3;
            } else {
                CL_Obj ex = cl_get_type_expander(head);
                if (!CL_NULL_P(ex)) {
                    CL_Obj arg_array[16];
                    int na = 0;
                    CL_Obj r = rest;
                    while (!CL_NULL_P(r) && na < 16) {
                        arg_array[na++] = cl_car(r);
                        r = cl_cdr(r);
                    }
                    return seq_ctor_type_class(cl_vm_apply(ex, arg_array, na),
                                               depth - 1, len_out);
                }
                return -1;
            }
            if (CL_FIXNUM_P(lenarg)) *len_out = CL_FIXNUM_VAL(lenarg);
            return cls;
        }
    }
    return -1;
}

/* Classify a MAP/MERGE result-type specifier into:
 *   0 = nil/null, 1 = list, 2 = string, 3 = (general) vector, -1 = unknown.
 * Expands deftypes and understands compound (vector ...) / (array ...) /
 * (string ...) specifiers, choosing string when the element-type is a
 * character subtype (so e.g. babel's (vector unicode-char *) maps to a
 * string).  DEPTH bounds deftype-expansion recursion. */
static int seq_result_type_class(CL_Obj rt, int depth)
{
    if (depth <= 0) return -1;
    if (CL_NULL_P(rt)) return 0;
    if (CL_SYMBOL_P(rt)) {
        const char *nm = cl_symbol_name(rt);
        if (strcmp(nm, "NULL") == 0) return 0;
        if (strcmp(nm, "LIST") == 0 || strcmp(nm, "CONS") == 0) return 1;
        if (strcmp(nm, "STRING") == 0 || strcmp(nm, "SIMPLE-STRING") == 0 ||
            strcmp(nm, "BASE-STRING") == 0 || strcmp(nm, "SIMPLE-BASE-STRING") == 0)
            return 2;
        if (strcmp(nm, "VECTOR") == 0 || strcmp(nm, "SIMPLE-VECTOR") == 0 ||
            strcmp(nm, "ARRAY") == 0 || strcmp(nm, "SIMPLE-ARRAY") == 0 ||
            strcmp(nm, "BIT-VECTOR") == 0 || strcmp(nm, "SIMPLE-BIT-VECTOR") == 0)
            return 3;
        {
            CL_Obj ex = cl_get_type_expander(rt);
            if (!CL_NULL_P(ex))
                return seq_result_type_class(cl_vm_apply(ex, NULL, 0), depth - 1);
        }
        return -1;
    }
    if (CL_CONS_P(rt)) {
        CL_Obj head = cl_car(rt);
        if (CL_SYMBOL_P(head)) {
            const char *nm = cl_symbol_name(head);
            if (strcmp(nm, "LIST") == 0 || strcmp(nm, "CONS") == 0) return 1;
            if (strcmp(nm, "STRING") == 0 || strcmp(nm, "SIMPLE-STRING") == 0 ||
                strcmp(nm, "BASE-STRING") == 0 ||
                strcmp(nm, "SIMPLE-BASE-STRING") == 0)
                return 2;
            if (strcmp(nm, "VECTOR") == 0 || strcmp(nm, "SIMPLE-VECTOR") == 0 ||
                strcmp(nm, "ARRAY") == 0 || strcmp(nm, "SIMPLE-ARRAY") == 0) {
                CL_Obj eargs = cl_cdr(rt);
                if (!CL_NULL_P(eargs) && seq_elt_type_is_char(cl_car(eargs), 8))
                    return 2;
                return 3;
            }
            if (strcmp(nm, "BIT-VECTOR") == 0 ||
                strcmp(nm, "SIMPLE-BIT-VECTOR") == 0)
                return 3;
            {
                CL_Obj ex = cl_get_type_expander(head);
                if (!CL_NULL_P(ex)) {
                    CL_Obj arg_array[16];
                    int nargs = 0;
                    CL_Obj r = cl_cdr(rt);
                    while (!CL_NULL_P(r) && nargs < 16) {
                        arg_array[nargs++] = cl_car(r);
                        r = cl_cdr(r);
                    }
                    return seq_result_type_class(cl_vm_apply(ex, arg_array, nargs),
                                                 depth - 1);
                }
            }
        }
    }
    return -1;
}

static CL_Obj bi_map(CL_Obj *args, int n)
{
    /* (map result-type function &rest sequences) */
    CL_Obj result_type = args[0];
    CL_Obj func;
    int nseqs = n - 2;
    CL_Obj seqs[16];
    CL_Obj orig_seqs[16];
    int32_t lens[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;
    int32_t idx = 0;
    int rt; /* 0=nil, 1=list, 2=string, 3=vector */

    /* Classify the result-type FIRST: seq_result_type_class may expand a
     * deftype via cl_vm_apply, which can allocate/compact.  Only args[]
     * (GC-rooted) is live at this point, so no offsets are stale across it. */
    rt = seq_result_type_class(result_type, 16);
    if (rt < 0) {
        cl_error(CL_ERR_ARGS, "MAP: unsupported result-type");
        return CL_NIL;
    }

    func = cl_coerce_funcdesig(args[1], "MAP");
    if (nseqs > 16) nseqs = 16;
    for (i = 0; i < nseqs; i++) {
        seqs[i] = args[i + 2];
        orig_seqs[i] = seqs[i];
        lens[i] = every_seq_len(seqs[i]);
    }

    if (rt == 0) {
        /* nil/null result-type: iterate for side-effects */
        for (idx = 0; ; idx++) {
            if (map_exhausted(seqs, lens, nseqs, idx)) return CL_NIL;
            map_collect_args(seqs, lens, orig_seqs, call_args, nseqs, idx);
            call_func(func, call_args, nseqs);
        }
    }

    if (rt == 2 || rt == 3) {
        /* STRING or VECTOR result: need to know length first */
        int32_t min_len = 0x7FFFFFFF;
        for (i = 0; i < nseqs; i++) {
            int32_t l = (lens[i] >= 0) ? lens[i] : seq_length(seqs[i]);
            if (l < min_len) min_len = l;
        }

        if (rt == 2) {
            /* STRING result: allocate the (possibly wide) string up front
             * and fill it char-by-char.  This avoids any fixed buffer cap
             * and preserves characters > 255 (cl_string_set_char_at handles
             * the wide-string case). */
            CL_GC_PROTECT(func);
            result = cl_make_string(NULL, (uint32_t)min_len);
            CL_GC_PROTECT(result);
            for (idx = 0; idx < min_len; idx++) {
                CL_Obj val;
                map_collect_args(seqs, lens, orig_seqs, call_args, nseqs, idx);
                val = call_func(func, call_args, nseqs);
                cl_string_set_char_at(result, (uint32_t)idx,
                                      CL_CHAR_P(val) ? CL_CHAR_VAL(val) : 0);
            }
            CL_GC_UNPROTECT(2);
            return result;
        } else {
            /* VECTOR */
            CL_GC_PROTECT(func);
            result = cl_make_vector((uint32_t)min_len);
            CL_GC_PROTECT(result);
            for (idx = 0; idx < min_len; idx++) {
                CL_Obj val;
                map_collect_args(seqs, lens, orig_seqs, call_args, nseqs, idx);
                val = call_func(func, call_args, nseqs);
                cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(result))[idx] = val;
            }
            CL_GC_UNPROTECT(2);
            return result;
        }
    }

    /* LIST result type */
    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (idx = 0; ; idx++) {
        CL_Obj val, cell;

        if (map_exhausted(seqs, lens, nseqs, idx)) break;
        map_collect_args(seqs, lens, orig_seqs, call_args, nseqs, idx);

        val = call_func(func, call_args, nseqs);
        cell = cl_cons(val, CL_NIL);
        if (CL_NULL_P(result)) result = cell;
        else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
        tail = cell;
    }

    CL_GC_UNPROTECT(3);
    return result;
}

/* ======================================================= */
/* MISMATCH                                                */
/* ======================================================= */

static CL_Obj bi_mismatch(CL_Obj *args, int n)
{
    CL_Obj seq1 = args[0], seq2 = args[1];
    CL_Obj test_fn = SYM_EQL_FN;
    CL_Obj test_not_fn = CL_NIL;
    CL_Obj key_fn = CL_NIL;
    int32_t start1 = 0, end1, start2 = 0, end2;
    int32_t len1, len2;
    int32_t i, j;
    int ki;
    int from_end = 0;

    cl_check_seq_keywords(args, n, 2,
        SK_TEST | SK_TEST_NOT | SK_KEY | SK_START1 | SK_END1 |
        SK_START2 | SK_END2 | SK_FROM_END);

    /* Parse keywords */
    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_TEST) test_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST");
        else if (args[ki] == KW_TEST_NOT) test_not_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST-NOT");
        else if (args[ki] == KW_KEY) key_fn = CL_NULL_P(args[ki + 1]) ? CL_NIL : cl_coerce_funcdesig(args[ki + 1], ":KEY");
        else if (args[ki] == KW_START1 && CL_FIXNUM_P(args[ki + 1])) start1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_END1 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1])) end1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_START2 && CL_FIXNUM_P(args[ki + 1])) start2 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_END2 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1])) end2 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_FROM_END && !CL_NULL_P(args[ki + 1])) from_end = 1;
    }

    len1 = seq_length(seq1);
    len2 = seq_length(seq2);

    /* Set defaults for end if not provided */
    end1 = len1;
    end2 = len2;
    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_END1 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1]))
            end1 = CL_FIXNUM_VAL(args[ki + 1]);
        if (args[ki] == KW_END2 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1]))
            end2 = CL_FIXNUM_VAL(args[ki + 1]);
    }

    if (from_end) {
        /* Search from the end; return position in seq1 of rightmost mismatch + 1,
         * or NIL if subsequences are identical. Per CL spec, the returned index
         * is the leftmost position such that elements at or after it don't all match. */
        int32_t sublen1 = end1 - start1;
        int32_t sublen2 = end2 - start2;
        int32_t mismatch_pos = -1;
        i = end1 - 1;
        j = end2 - 1;
        while (i >= start1 && j >= start2) {
            CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, i));
            CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, j));
            int match = !CL_NULL_P(test_not_fn)
                ? CL_NULL_P(call_test(test_not_fn, e1, e2))
                : !CL_NULL_P(call_test(test_fn, e1, e2));
            if (!match) {
                mismatch_pos = i + 1;
                break;
            }
            i--;
            j--;
        }
        if (mismatch_pos >= 0) return CL_MAKE_FIXNUM(mismatch_pos);
        /* All compared elements matched; check if lengths differ */
        if (sublen1 != sublen2) {
            /* Shorter sequence ran out first */
            if (sublen1 < sublen2) return CL_MAKE_FIXNUM(start1);
            else return CL_MAKE_FIXNUM(start1 + (sublen1 - sublen2));
        }
        return CL_NIL;
    }

    i = start1;
    j = start2;
    while (i < end1 && j < end2) {
        CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, i));
        CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, j));
        int match = !CL_NULL_P(test_not_fn)
            ? CL_NULL_P(call_test(test_not_fn, e1, e2))
            : !CL_NULL_P(call_test(test_fn, e1, e2));
        if (!match)
            return CL_MAKE_FIXNUM(i);
        i++;
        j++;
    }

    /* If both subsequences exhausted, no mismatch */
    if (i == end1 && j == end2) return CL_NIL;
    /* Otherwise mismatch at position where one ended */
    return CL_MAKE_FIXNUM(i);
}

/* ======================================================= */
/* SEARCH                                                  */
/* ======================================================= */

static CL_Obj bi_search(CL_Obj *args, int n)
{
    CL_Obj seq1 = args[0], seq2 = args[1];
    CL_Obj test_fn = SYM_EQL_FN;
    CL_Obj test_not_fn = CL_NIL;
    CL_Obj key_fn = CL_NIL;
    int32_t start1 = 0, end1, start2 = 0, end2;
    int32_t len1, len2, sublen;
    int32_t i, j;
    int ki, from_end = 0;

    cl_check_seq_keywords(args, n, 2,
        SK_TEST | SK_TEST_NOT | SK_KEY | SK_START1 | SK_END1 |
        SK_START2 | SK_END2 | SK_FROM_END);

    /* Parse keywords */
    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_TEST) test_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST");
        else if (args[ki] == KW_TEST_NOT) test_not_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST-NOT");
        else if (args[ki] == KW_KEY) key_fn = CL_NULL_P(args[ki + 1]) ? CL_NIL : cl_coerce_funcdesig(args[ki + 1], ":KEY");
        else if (args[ki] == KW_START1 && CL_FIXNUM_P(args[ki + 1])) start1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_START2 && CL_FIXNUM_P(args[ki + 1])) start2 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_FROM_END && !CL_NULL_P(args[ki + 1])) from_end = 1;
    }

    len1 = seq_length(seq1);
    len2 = seq_length(seq2);
    end1 = len1;
    end2 = len2;

    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_END1 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1]))
            end1 = CL_FIXNUM_VAL(args[ki + 1]);
        if (args[ki] == KW_END2 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1]))
            end2 = CL_FIXNUM_VAL(args[ki + 1]);
    }

    sublen = end1 - start1;
    /* Empty needle matches at start2 (or end2 searching from the end). */
    if (sublen <= 0)
        return CL_MAKE_FIXNUM(from_end ? end2 : start2);

    /* Brute-force search.  With :from-end, scan right-to-left and return the
     * leftmost index of the rightmost match. */
    if (from_end) {
        for (i = end2 - sublen; i >= start2; i--) {
            int match = 1;
            for (j = 0; j < sublen; j++) {
                CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, start1 + j));
                CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, i + j));
                int m = !CL_NULL_P(test_not_fn)
                    ? CL_NULL_P(call_test(test_not_fn, e1, e2))
                    : !CL_NULL_P(call_test(test_fn, e1, e2));
                if (!m) { match = 0; break; }
            }
            if (match) return CL_MAKE_FIXNUM(i);
        }
        return CL_NIL;
    }

    for (i = start2; i + sublen <= end2; i++) {
        int match = 1;
        for (j = 0; j < sublen; j++) {
            CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, start1 + j));
            CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, i + j));
            int m = !CL_NULL_P(test_not_fn)
                ? CL_NULL_P(call_test(test_not_fn, e1, e2))
                : !CL_NULL_P(call_test(test_fn, e1, e2));
            if (!m) { match = 0; break; }
        }
        if (match) return CL_MAKE_FIXNUM(i);
    }

    return CL_NIL;
}

/* ======================================================= */
/* SORT / STABLE-SORT                                      */
/* ======================================================= */

/* Merge sort for lists — O(n log n) time, O(1) extra space */

static CL_Obj list_merge(CL_Obj a, CL_Obj b, CL_Obj pred, CL_Obj key_fn)
{
    CL_Obj result = CL_NIL, tail = CL_NIL;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);
    CL_GC_PROTECT(a);
    CL_GC_PROTECT(b);

    while (!CL_NULL_P(a) && !CL_NULL_P(b)) {
        CL_Obj ka = apply_key(key_fn, cl_car(a));
        CL_Obj kb = apply_key(key_fn, cl_car(b));
        CL_Obj pick;
        /* Stable merge: pick `b` only when pred(kb, ka) is true, i.e.
           b is strictly less than a.  Otherwise (a strictly less, or
           equal, or incomparable) pick `a` — this preserves the
           original order of elements the predicate considers equal. */
        if (!CL_NULL_P(call_test(pred, kb, ka))) {
            pick = b;
            b = cl_cdr(b);
        } else {
            pick = a;
            a = cl_cdr(a);
        }
        if (CL_NULL_P(result)) {
            result = pick;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = pick;
        }
        tail = pick;
    }

    /* Append remainder */
    if (!CL_NULL_P(a)) {
        if (CL_NULL_P(result)) result = a;
        else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = a;
    } else if (!CL_NULL_P(b)) {
        if (CL_NULL_P(result)) result = b;
        else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = b;
    }

    CL_GC_UNPROTECT(4);
    return result;
}

static CL_Obj list_merge_sort(CL_Obj list, CL_Obj pred, CL_Obj key_fn)
{
    CL_Obj slow, fast, mid, result;

    if (CL_NULL_P(list) || CL_NULL_P(cl_cdr(list)))
        return list;

    /* Split into two halves with fast/slow pointer */
    slow = list;
    fast = cl_cdr(list);
    while (!CL_NULL_P(fast) && !CL_NULL_P(cl_cdr(fast))) {
        slow = cl_cdr(slow);
        fast = cl_cdr(cl_cdr(fast));
    }
    mid = cl_cdr(slow);
    ((CL_Cons *)CL_OBJ_TO_PTR(slow))->cdr = CL_NIL;

    /* GC-protect list and mid across recursive calls:
       each recursive sort triggers merges that call key/pred functions,
       which can allocate and trigger GC */
    CL_GC_PROTECT(list);
    CL_GC_PROTECT(mid);

    list = list_merge_sort(list, pred, key_fn);
    mid = list_merge_sort(mid, pred, key_fn);

    result = list_merge(list, mid, pred, key_fn);
    CL_GC_UNPROTECT(2);
    return result;
}

/* ======================================================= */
/* MERGE                                                   */
/* ======================================================= */

/* Helper: collect sequence elements into platform_alloc'd array. */
static void seq_to_array(CL_Obj seq, CL_Obj **out, int32_t *out_len)
{
    int32_t len, i;
    CL_Obj *arr;
    CL_Obj cur;

    if (CL_NULL_P(seq)) { *out = NULL; *out_len = 0; return; }

    if (CL_CONS_P(seq)) {
        len = 0;
        cur = seq;
        while (!CL_NULL_P(cur)) { len++; cur = cl_cdr(cur); }
        arr = (CL_Obj *)platform_alloc((uint32_t)(len * (int32_t)sizeof(CL_Obj)));
        cur = seq;
        for (i = 0; i < len; i++) { arr[i] = cl_car(cur); cur = cl_cdr(cur); }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        len = (int32_t)cl_vector_active_length(v);
        arr = (CL_Obj *)platform_alloc((uint32_t)(len * (int32_t)sizeof(CL_Obj)));
        for (i = 0; i < len; i++) arr[i] = cl_vector_data(v)[i];
    } else if (CL_ANY_STRING_P(seq)) {
        len = (int32_t)cl_string_length(seq);
        arr = (CL_Obj *)platform_alloc((uint32_t)(len * (int32_t)sizeof(CL_Obj)));
        for (i = 0; i < len; i++)
            arr[i] = CL_MAKE_CHAR(cl_string_char_at(seq, (uint32_t)i));
    } else if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        len = (int32_t)cl_bv_active_length(bv);
        arr = (CL_Obj *)platform_alloc((uint32_t)(len * (int32_t)sizeof(CL_Obj)));
        for (i = 0; i < len; i++)
            arr[i] = CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)i));
    } else {
        *out = NULL; *out_len = 0;
        cl_error(CL_ERR_TYPE, "MERGE: not a sequence");
        return;
    }
    *out = arr;
    *out_len = len;
}

/* (merge result-type sequence1 sequence2 predicate &key key)
 * Merge two sorted sequences into one sorted sequence of result-type. */
static CL_Obj bi_merge(CL_Obj *args, int n)
{
    CL_Obj result_type = args[0];
    CL_Obj seq1 = args[1];
    CL_Obj seq2 = args[2];
    CL_Obj pred = cl_coerce_funcdesig(args[3], "MERGE");
    CL_Obj key_fn = CL_NIL;
    int i, rt;
    int32_t len_constraint = -1;

    cl_check_seq_keywords(args, n, 4, SK_KEY);
    for (i = 4; i + 1 < n; i += 2) {
        if (args[i] == KW_KEY)
            key_fn = CL_NULL_P(args[i + 1]) ? CL_NIL : cl_coerce_funcdesig(args[i + 1], ":KEY");
    }

    /* Classify the result-type: 0=null 1=list 2=string 3=vector 4=bit-vector.
     * len_constraint is the declared length, or -1 when unspecified / `*`. */
    rt = seq_ctor_type_class(result_type, 16, &len_constraint);
    if (rt < 0)
        cl_error(CL_ERR_ARGS, "MERGE: unsupported result-type");

    /* Fast path: list merge when both inputs are lists */
    if (rt == 1 && (CL_NULL_P(seq1) || CL_CONS_P(seq1)) &&
                   (CL_NULL_P(seq2) || CL_CONS_P(seq2))) {
        CL_GC_PROTECT(seq1);
        CL_GC_PROTECT(seq2);
        CL_GC_PROTECT(pred);
        CL_GC_PROTECT(key_fn);
        {
            CL_Obj r = list_merge(seq1, seq2, pred, key_fn);
            CL_GC_UNPROTECT(4);
            return r;
        }
    }

    /* General path: collect into arrays, merge, build result */
    {
        CL_Obj *a1 = NULL, *a2 = NULL, *out = NULL;
        int32_t n1 = 0, n2 = 0, ntotal, ia, ib, io;
        CL_Obj result = CL_NIL, tail = CL_NIL;

        seq_to_array(seq1, &a1, &n1);
        seq_to_array(seq2, &a2, &n2);
        ntotal = n1 + n2;
        if (ntotal > 0)
            out = (CL_Obj *)platform_alloc((uint32_t)(ntotal * (int32_t)sizeof(CL_Obj)));

        /* Protect pred/key_fn across CL user function calls in the merge loop */
        CL_GC_PROTECT(pred);
        CL_GC_PROTECT(key_fn);

        /* Merge a1 and a2 into out[] using stable merge.
         * Read each element into a GC-protected local before calling user
         * functions — apply_key/call_test can trigger compaction, which would
         * leave the raw a1[]/a2[] pointers holding stale arena offsets. */
        ia = 0; ib = 0; io = 0;
        while (ia < n1 && ib < n2) {
            CL_Obj ea = a1[ia], eb = a2[ib], ka, kb;
            CL_GC_PROTECT(ea);
            CL_GC_PROTECT(eb);
            ka = apply_key(key_fn, ea);
            CL_GC_PROTECT(ka);
            kb = apply_key(key_fn, eb);
            /* Take from b only if pred(kb, ka) is true */
            if (!CL_NULL_P(call_test(pred, kb, ka)))
                { out[io++] = eb; ib++; }
            else
                { out[io++] = ea; ia++; }
            CL_GC_UNPROTECT(3); /* ea, eb, ka */
        }
        while (ia < n1) out[io++] = a1[ia++];
        while (ib < n2) out[io++] = a2[ib++];

        CL_GC_UNPROTECT(2); /* pred, key_fn */

        if (a1) platform_free(a1);
        if (a2) platform_free(a2);

        /* A declared length must match the actual result length. */
        if (len_constraint >= 0 && len_constraint != ntotal) {
            if (out) platform_free(out);
            cl_error(CL_ERR_TYPE, "MERGE: result length does not match result-type");
        }

        if (rt == 0) {
            /* result-type NULL: only the empty merge is well typed. */
            if (out) platform_free(out);
            if (ntotal != 0)
                cl_error(CL_ERR_TYPE, "MERGE: non-empty result for result-type NULL");
            return CL_NIL;
        } else if (rt == 1) {
            /* Build list result, reading each out[] element into a protected
             * local so cl_cons compaction cannot make later out[i] reads stale */
            CL_Obj elem = CL_NIL;
            CL_GC_PROTECT(result);
            CL_GC_PROTECT(tail);
            CL_GC_PROTECT(elem);
            for (i = 0; i < ntotal; i++) {
                CL_Obj cell;
                elem = out[i];
                cell = cl_cons(elem, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
            CL_GC_UNPROTECT(3);
        } else if (rt == 2) {
            /* String result.  Merged elements are characters (immediate
             * values), so out[] cannot go stale across the alloc. */
            result = cl_make_string(NULL, (uint32_t)ntotal);
            for (i = 0; i < ntotal; i++)
                cl_string_set_char_at(result, (uint32_t)i,
                                      CL_CHAR_P(out[i]) ? CL_CHAR_VAL(out[i]) : 0);
        } else if (rt == 4) {
            /* Bit-vector result.  Merged elements are fixnums 0/1 (immediate). */
            result = cl_make_bit_vector((uint32_t)ntotal);
            {
                CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
                for (i = 0; i < ntotal; i++)
                    cl_bv_set_bit(bv, (uint32_t)i,
                                  (CL_FIXNUM_P(out[i]) && CL_FIXNUM_VAL(out[i]) != 0));
            }
        } else {
            /* General vector result */
            result = cl_make_vector((uint32_t)ntotal);
            {
                CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(result);
                for (i = 0; i < ntotal; i++)
                    cl_vector_data(v)[i] = out[i];
            }
        }

        if (out) platform_free(out);
        return result;
    }
}

/* Insertion sort for vectors — in-place, stable */
static void vector_insertion_sort(CL_Obj *data, int32_t len, CL_Obj pred, CL_Obj key_fn)
{
    int32_t i, j;
    for (i = 1; i < len; i++) {
        CL_Obj val = data[i];
        CL_Obj kval = apply_key(key_fn, val);
        j = i - 1;
        while (j >= 0) {
            CL_Obj kj = apply_key(key_fn, data[j]);
            if (CL_NULL_P(call_test(pred, kval, kj)))
                break;
            data[j + 1] = data[j];
            j--;
        }
        data[j + 1] = val;
    }
}

/* Set element I of a string / bit-vector / vector sequence. */
static void array_seq_set(CL_Obj seq, int32_t i, CL_Obj v)
{
    if (CL_ANY_STRING_P(seq)) {
        if (CL_CHAR_P(v)) cl_string_set_char_at(seq, (uint32_t)i, CL_CHAR_VAL(v));
    } else if (CL_BIT_VECTOR_P(seq)) {
        uint32_t p = (uint32_t)i;  /* cl_bv_set_bit evaluates its index twice */
        cl_bv_set_bit((CL_BitVector *)CL_OBJ_TO_PTR(seq), p,
                      (CL_FIXNUM_P(v) && CL_FIXNUM_VAL(v) != 0));
    } else if (CL_VECTOR_P(seq)) {
        cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(seq))[i] = v;
    }
}

/* In-place stable insertion sort for string / bit-vector sequences.  Elements
 * are snapshotted into a C array first; for these element types the values are
 * immediate (characters / bits), so the snapshot is unaffected by a compacting
 * GC triggered inside the :key / predicate calls. */
static void array_seq_insertion_sort(CL_Obj seq, int32_t len, CL_Obj pred, CL_Obj key_fn)
{
    int32_t i, j;
    CL_Obj *tmp;
    if (len <= 1) return;
    tmp = (CL_Obj *)platform_alloc((uint32_t)(len * (int32_t)sizeof(CL_Obj)));
    CL_GC_PROTECT(seq);    /* writeback after sort may compact via apply_key/call_test */
    CL_GC_PROTECT(pred);
    CL_GC_PROTECT(key_fn);
    for (i = 0; i < len; i++) tmp[i] = seq_elt(seq, i);
    for (i = 1; i < len; i++) {
        CL_Obj val = tmp[i];
        CL_Obj kval = apply_key(key_fn, val);
        j = i - 1;
        while (j >= 0) {
            CL_Obj kj = apply_key(key_fn, tmp[j]);
            if (CL_NULL_P(call_test(pred, kval, kj))) break;
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = val;
    }
    for (i = 0; i < len; i++) array_seq_set(seq, i, tmp[i]);
    CL_GC_UNPROTECT(3);
    platform_free(tmp);
}

static CL_Obj bi_sort(CL_Obj *args, int n)
{
    CL_Obj seq = args[0], pred = cl_coerce_funcdesig(args[1], "SORT");
    CL_Obj key_fn = CL_NIL;
    int i;

    cl_check_seq_keywords(args, n, 2, SK_KEY);
    /* Parse :key */
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_KEY)
            key_fn = CL_NULL_P(args[i + 1]) ? CL_NIL : cl_coerce_funcdesig(args[i + 1], ":KEY");
    }

    if (CL_NULL_P(seq)) return CL_NIL;

    if (CL_CONS_P(seq)) {
        return list_merge_sort(seq, pred, key_fn);
    }

    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        vector_insertion_sort(cl_vector_data(v), (int32_t)cl_vector_active_length(v), pred, key_fn);
        return args[0];
    }

    if (CL_ANY_STRING_P(seq) || CL_BIT_VECTOR_P(seq)) {
        array_seq_insertion_sort(seq, seq_length(seq), pred, key_fn);
        return args[0];
    }

    cl_error(CL_ERR_TYPE, "SORT: not a sequence");
    return CL_NIL;
}

static CL_Obj bi_stable_sort(CL_Obj *args, int n)
{
    /* Both list merge sort and vector insertion sort are stable */
    return bi_sort(args, n);
}

/* ======================================================= */
/* Registration                                            */
/* ======================================================= */

void cl_builtins_sequence2_init(void)
{
    /* Pre-intern keyword symbols */
    KW_TEST   = cl_intern_keyword("TEST", 4);
    KW_TEST_NOT = cl_intern_keyword("TEST-NOT", 8);
    KW_KEY    = cl_intern_keyword("KEY", 3);
    KW_START1 = cl_intern_keyword("START1", 6);
    KW_END1   = cl_intern_keyword("END1", 4);
    KW_START2 = cl_intern_keyword("START2", 6);
    KW_END2   = cl_intern_keyword("END2", 4);
    KW_FROM_END = cl_intern_keyword("FROM-END", 8);

    /* Cache eql function */
    {
        CL_Obj eql_sym = cl_intern_in("EQL", 3, cl_package_cl);
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(eql_sym);
        SYM_EQL_FN = s->function;
    }

    /* Cache 'list symbol */
    SYM_LIST = cl_intern_in("LIST", 4, cl_package_cl);

    /* Quantifiers */
    defun("EVERY", bi_every, 2, -1);
    defun("SOME", bi_some, 2, -1);
    defun("NOTANY", bi_notany, 2, -1);
    defun("NOTEVERY", bi_notevery, 2, -1);

    /* Map (generic) */
    defun("MAP", bi_map, 3, -1);

    /* Comparison/search */
    defun("MISMATCH", bi_mismatch, 2, -1);
    defun("SEARCH", bi_search, 2, -1);

    /* Merge */
    defun("MERGE", bi_merge, 4, -1);

    /* Sorting */
    defun("SORT", bi_sort, 2, -1);
    defun("STABLE-SORT", bi_stable_sort, 2, -1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_TEST);
    cl_gc_register_root(&KW_TEST_NOT);
    cl_gc_register_root(&KW_KEY);
    cl_gc_register_root(&KW_START1);
    cl_gc_register_root(&KW_END1);
    cl_gc_register_root(&KW_START2);
    cl_gc_register_root(&KW_END2);
    cl_gc_register_root(&KW_FROM_END);
    cl_gc_register_root(&SYM_EQL_FN);
    cl_gc_register_root(&SYM_LIST);
}
