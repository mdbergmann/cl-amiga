#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- Pre-interned keyword symbols --- */

static CL_Obj KW_TEST = CL_NIL;
static CL_Obj KW_KEY = CL_NIL;
static CL_Obj KW_START1 = CL_NIL;
static CL_Obj KW_END1 = CL_NIL;
static CL_Obj KW_START2 = CL_NIL;
static CL_Obj KW_END2 = CL_NIL;
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
    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        return f->func(call_args, nargs);
    }
    if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func))
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
    if (CL_STRING_P(seq)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(seq);
        return (int32_t)s->length;
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
    if (CL_STRING_P(seq)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(seq);
        if ((uint32_t)idx >= s->length) cl_error(CL_ERR_ARGS, "index out of bounds");
        return CL_MAKE_CHAR((unsigned char)s->data[idx]);
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
    if (CL_STRING_P(seq))
        return (int32_t)((CL_String *)CL_OBJ_TO_PTR(seq))->length;
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
    if (CL_STRING_P(seq)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(seq);
        return CL_MAKE_CHAR((unsigned char)s->data[idx]);
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
static int map_result_type_match(CL_Obj result_type, const char *name, uint32_t len)
{
    if (CL_SYMBOL_P(result_type)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(result_type);
        CL_String *sn = (CL_String *)CL_OBJ_TO_PTR(s->name);
        return sn->length == len && memcmp(sn->data, name, len) == 0;
    }
    return 0;
}

static CL_Obj bi_map(CL_Obj *args, int n)
{
    /* (map result-type function &rest sequences) */
    CL_Obj result_type = args[0];
    CL_Obj func = cl_coerce_funcdesig(args[1], "MAP");
    int nseqs = n - 2;
    CL_Obj seqs[16];
    CL_Obj orig_seqs[16];
    int32_t lens[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;
    int32_t idx = 0;
    int rt; /* 0=nil, 1=list, 2=string, 3=vector */

    if (nseqs > 16) nseqs = 16;
    for (i = 0; i < nseqs; i++) {
        seqs[i] = args[i + 2];
        orig_seqs[i] = seqs[i];
        lens[i] = every_seq_len(seqs[i]);
    }

    /* Determine result type */
    rt = -1;
    if (CL_NULL_P(result_type))
        rt = 0;
    else if (map_result_type_match(result_type, "NULL", 4))
        rt = 0;
    else if (map_result_type_match(result_type, "LIST", 4))
        rt = 1;
    else if (map_result_type_match(result_type, "STRING", 6))
        rt = 2;
    else if (map_result_type_match(result_type, "VECTOR", 6))
        rt = 3;

    if (rt < 0) {
        cl_error(CL_ERR_ARGS, "MAP: unsupported result-type");
        return CL_NIL;
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
            result = cl_make_string("", 0);
            /* Resize to min_len */
            {
                CL_String *s = (CL_String *)CL_OBJ_TO_PTR(result);
                (void)s;
                /* Build up by collecting results first */
                result = CL_NIL;
            }
            /* Collect into a list, then build string */
            CL_GC_PROTECT(func);
            CL_GC_PROTECT(result);
            CL_GC_PROTECT(tail);
            for (idx = 0; idx < min_len; idx++) {
                CL_Obj val, cell;
                map_collect_args(seqs, lens, orig_seqs, call_args, nseqs, idx);
                val = call_func(func, call_args, nseqs);
                cell = cl_cons(val, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
            /* Build string from char list */
            {
                char buf[4096];
                CL_Obj cur = result;
                int32_t si = 0;
                while (!CL_NULL_P(cur) && si < 4095) {
                    CL_Obj ch = cl_car(cur);
                    if (CL_CHAR_P(ch))
                        buf[si++] = (char)CL_CHAR_VAL(ch);
                    cur = cl_cdr(cur);
                }
                buf[si] = '\0';
                CL_GC_UNPROTECT(3);
                return cl_make_string(buf, (uint32_t)si);
            }
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
    CL_Obj key_fn = CL_NIL;
    int32_t start1 = 0, end1, start2 = 0, end2;
    int32_t len1, len2;
    int32_t i, j;
    int ki;

    /* Parse keywords */
    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_TEST) test_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST");
        else if (args[ki] == KW_KEY) key_fn = cl_coerce_funcdesig(args[ki + 1], ":KEY");
        else if (args[ki] == KW_START1 && CL_FIXNUM_P(args[ki + 1])) start1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_END1 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1])) end1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_START2 && CL_FIXNUM_P(args[ki + 1])) start2 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_END2 && !CL_NULL_P(args[ki + 1]) && CL_FIXNUM_P(args[ki + 1])) end2 = CL_FIXNUM_VAL(args[ki + 1]);
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

    i = start1;
    j = start2;
    while (i < end1 && j < end2) {
        CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, i));
        CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, j));
        if (CL_NULL_P(call_test(test_fn, e1, e2)))
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
    CL_Obj key_fn = CL_NIL;
    int32_t start1 = 0, end1, start2 = 0, end2;
    int32_t len1, len2, sublen;
    int32_t i, j;
    int ki;

    /* Parse keywords */
    for (ki = 2; ki + 1 < n; ki += 2) {
        if (args[ki] == KW_TEST) test_fn = cl_coerce_funcdesig(args[ki + 1], ":TEST");
        else if (args[ki] == KW_KEY) key_fn = cl_coerce_funcdesig(args[ki + 1], ":KEY");
        else if (args[ki] == KW_START1 && CL_FIXNUM_P(args[ki + 1])) start1 = CL_FIXNUM_VAL(args[ki + 1]);
        else if (args[ki] == KW_START2 && CL_FIXNUM_P(args[ki + 1])) start2 = CL_FIXNUM_VAL(args[ki + 1]);
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
    if (sublen <= 0) return CL_MAKE_FIXNUM(start2); /* Empty needle matches at start */

    /* Brute-force search */
    for (i = start2; i + sublen <= end2; i++) {
        int match = 1;
        for (j = 0; j < sublen; j++) {
            CL_Obj e1 = apply_key(key_fn, seq_elt(seq1, start1 + j));
            CL_Obj e2 = apply_key(key_fn, seq_elt(seq2, i + j));
            if (CL_NULL_P(call_test(test_fn, e1, e2))) {
                match = 0;
                break;
            }
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
        /* Pick a if pred(ka, kb) is true, otherwise pick b */
        if (!CL_NULL_P(call_test(pred, ka, kb))) {
            pick = a;
            a = cl_cdr(a);
        } else {
            pick = b;
            b = cl_cdr(b);
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

static CL_Obj bi_sort(CL_Obj *args, int n)
{
    CL_Obj seq = args[0], pred = cl_coerce_funcdesig(args[1], "SORT");
    CL_Obj key_fn = CL_NIL;
    int i;

    /* Parse :key */
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_KEY)
            key_fn = cl_coerce_funcdesig(args[i + 1], ":KEY");
    }

    if (CL_NULL_P(seq)) return CL_NIL;

    if (CL_CONS_P(seq)) {
        return list_merge_sort(seq, pred, key_fn);
    }

    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        vector_insertion_sort(cl_vector_data(v), (int32_t)cl_vector_active_length(v), pred, key_fn);
        return seq;
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
    KW_KEY    = cl_intern_keyword("KEY", 3);
    KW_START1 = cl_intern_keyword("START1", 6);
    KW_END1   = cl_intern_keyword("END1", 4);
    KW_START2 = cl_intern_keyword("START2", 6);
    KW_END2   = cl_intern_keyword("END2", 4);

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

    /* Sorting */
    defun("SORT", bi_sort, 2, -1);
    defun("STABLE-SORT", bi_stable_sort, 2, -1);
}
