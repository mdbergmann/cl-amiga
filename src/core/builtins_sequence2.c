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

static CL_Obj bi_every(CL_Obj *args, int n)
{
    CL_Obj pred = cl_coerce_funcdesig(args[0], "EVERY");
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    int i;
    CL_Obj last_result = SYM_T;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    for (;;) {
        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) return last_result;
        }
        for (i = 0; i < nlists; i++) {
            call_args[i] = cl_car(lists[i]);
            lists[i] = cl_cdr(lists[i]);
        }
        last_result = call_func(pred, call_args, nlists);
        if (CL_NULL_P(last_result)) return CL_NIL;
    }
}

static CL_Obj bi_some(CL_Obj *args, int n)
{
    CL_Obj pred = cl_coerce_funcdesig(args[0], "SOME");
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    for (;;) {
        CL_Obj result;
        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) return CL_NIL;
        }
        for (i = 0; i < nlists; i++) {
            call_args[i] = cl_car(lists[i]);
            lists[i] = cl_cdr(lists[i]);
        }
        result = call_func(pred, call_args, nlists);
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

static CL_Obj bi_map(CL_Obj *args, int n)
{
    /* (map result-type function &rest sequences) */
    CL_Obj result_type = args[0];
    CL_Obj func = cl_coerce_funcdesig(args[1], "MAP");
    int nseqs = n - 2;
    CL_Obj seqs[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;
    int is_list;

    if (nseqs > 16) nseqs = 16;
    for (i = 0; i < nseqs; i++)
        seqs[i] = args[i + 2];

    /* Determine result type: nil means discard, list means build list */
    is_list = 0;
    if (CL_NULL_P(result_type)) {
        /* nil result-type: just iterate for side-effects */
        for (;;) {
            for (i = 0; i < nseqs; i++) {
                if (CL_NULL_P(seqs[i])) return CL_NIL;
            }
            for (i = 0; i < nseqs; i++) {
                call_args[i] = cl_car(seqs[i]);
                seqs[i] = cl_cdr(seqs[i]);
            }
            call_func(func, call_args, nseqs);
        }
    }

    /* Check if result-type is 'list */
    if (CL_SYMBOL_P(result_type)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(result_type);
        CL_String *name = (CL_String *)CL_OBJ_TO_PTR(s->name);
        if (name->length == 4 && memcmp(name->data, "LIST", 4) == 0)
            is_list = 1;
        else if (name->length == 4 && memcmp(name->data, "NULL", 4) == 0) {
            /* Same as nil */
            for (;;) {
                for (i = 0; i < nseqs; i++) {
                    if (CL_NULL_P(seqs[i])) return CL_NIL;
                }
                for (i = 0; i < nseqs; i++) {
                    call_args[i] = cl_car(seqs[i]);
                    seqs[i] = cl_cdr(seqs[i]);
                }
                call_func(func, call_args, nseqs);
            }
        }
    }

    if (!is_list) {
        cl_error(CL_ERR_ARGS, "MAP: only result-type LIST and NIL supported");
        return CL_NIL;
    }

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        CL_Obj val, cell;

        for (i = 0; i < nseqs; i++) {
            if (CL_NULL_P(seqs[i])) goto map_done;
        }
        for (i = 0; i < nseqs; i++) {
            call_args[i] = cl_car(seqs[i]);
            seqs[i] = cl_cdr(seqs[i]);
        }

        val = call_func(func, call_args, nseqs);
        cell = cl_cons(val, CL_NIL);
        if (CL_NULL_P(result)) result = cell;
        else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
        tail = cell;
    }

map_done:
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

    return result;
}

static CL_Obj list_merge_sort(CL_Obj list, CL_Obj pred, CL_Obj key_fn)
{
    CL_Obj slow, fast, mid;

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

    list = list_merge_sort(list, pred, key_fn);
    mid = list_merge_sort(mid, pred, key_fn);

    return list_merge(list, mid, pred, key_fn);
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
