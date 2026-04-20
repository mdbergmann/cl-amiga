#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "string_utils.h"
#include "../platform/platform.h"
#include <string.h>
#include <stdio.h>

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
static CL_Obj KW_TEST_NOT = CL_NIL;
static CL_Obj KW_KEY = CL_NIL;
static CL_Obj KW_START = CL_NIL;
static CL_Obj KW_END = CL_NIL;
static CL_Obj KW_COUNT = CL_NIL;
static CL_Obj KW_FROM_END = CL_NIL;
static CL_Obj KW_INITIAL_VALUE = CL_NIL;
static CL_Obj KW_START1 = CL_NIL;
static CL_Obj KW_END1 = CL_NIL;
static CL_Obj KW_START2 = CL_NIL;
static CL_Obj KW_END2 = CL_NIL;
static CL_Obj SYM_EQL_FN = CL_NIL;

/* --- Shared infrastructure --- */

/* Parsed keyword arguments for sequence functions */
typedef struct {
    CL_Obj test_fn;     /* :test function (default eql) */
    CL_Obj test_not_fn; /* :test-not function (default NIL) */
    CL_Obj key_fn;      /* :key function (default NIL = identity) */
    int32_t start;      /* :start (default 0) */
    int32_t end;        /* :end (default -1 = length) */
    int32_t count;      /* :count (default -1 = all) */
    int from_end;       /* :from-end (default 0) */
} SeqArgs;

static void parse_seq_args(CL_Obj *args, int n, int kw_start, SeqArgs *sa)
{
    int i;
    sa->test_fn = SYM_EQL_FN;
    sa->test_not_fn = CL_NIL;
    sa->key_fn = CL_NIL;
    sa->start = 0;
    sa->end = -1;
    sa->count = -1;
    sa->from_end = 0;

    for (i = kw_start; i + 1 < n; i += 2) {
        CL_Obj kw = args[i];
        CL_Obj val = args[i + 1];
        if (kw == KW_TEST) {
            sa->test_fn = cl_coerce_funcdesig(val, ":TEST");
        } else if (kw == KW_TEST_NOT) {
            sa->test_not_fn = cl_coerce_funcdesig(val, ":TEST-NOT");
        } else if (kw == KW_KEY) {
            sa->key_fn = CL_NULL_P(val) ? CL_NIL : cl_coerce_funcdesig(val, ":KEY");
        } else if (kw == KW_START) {
            if (CL_FIXNUM_P(val)) sa->start = CL_FIXNUM_VAL(val);
        } else if (kw == KW_END) {
            if (!CL_NULL_P(val) && CL_FIXNUM_P(val)) sa->end = CL_FIXNUM_VAL(val);
        } else if (kw == KW_COUNT) {
            if (!CL_NULL_P(val) && CL_FIXNUM_P(val)) sa->count = CL_FIXNUM_VAL(val);
        } else if (kw == KW_FROM_END) {
            sa->from_end = !CL_NULL_P(val);
        }
    }
}

/* Call a 2-arg test function */
static CL_Obj call_test(CL_Obj test_fn, CL_Obj a, CL_Obj b)
{
    CL_Obj targs[2];
    targs[0] = a;
    targs[1] = b;
    /* Resolve symbol function designator */
    if (CL_SYMBOL_P(test_fn)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(test_fn);
        test_fn = s->function;
    }
    /* Unwrap funcallable instances (e.g. a generic function) — the
     * discriminating-function slot is a closure, handled below. */
    if (cl_funcallable_instance_p(test_fn))
        test_fn = cl_unwrap_funcallable(test_fn);
    if (CL_FUNCTION_P(test_fn)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(test_fn);
        return f->func(targs, 2);
    }
    if (CL_BYTECODE_P(test_fn) || CL_CLOSURE_P(test_fn))
        return cl_vm_apply(test_fn, targs, 2);
    cl_error(CL_ERR_TYPE, "not a function (test)");
    return CL_NIL;
}

/* Call a 1-arg predicate/key function */
static CL_Obj call_1(CL_Obj fn, CL_Obj arg)
{
    CL_Obj pargs[1];
    pargs[0] = arg;
    /* Resolve symbol function designator */
    if (CL_SYMBOL_P(fn)) {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(fn);
        fn = s->function;
    }
    if (cl_funcallable_instance_p(fn))
        fn = cl_unwrap_funcallable(fn);
    if (CL_FUNCTION_P(fn)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(fn);
        return f->func(pargs, 1);
    }
    if (CL_BYTECODE_P(fn) || CL_CLOSURE_P(fn))
        return cl_vm_apply(fn, pargs, 1);
    cl_error(CL_ERR_TYPE, "not a function");
    return CL_NIL;
}

/* Apply :key if present, otherwise return element unchanged */
static CL_Obj apply_key(CL_Obj key_fn, CL_Obj elem)
{
    if (CL_NULL_P(key_fn)) return elem;
    return call_1(key_fn, elem);
}

/* Test item against element (applying :key to element) */
static int seq_test_match(SeqArgs *sa, CL_Obj item, CL_Obj elem)
{
    CL_Obj keyed = apply_key(sa->key_fn, elem);
    if (!CL_NULL_P(sa->test_not_fn))
        return CL_NULL_P(call_test(sa->test_not_fn, item, keyed));
    return !CL_NULL_P(call_test(sa->test_fn, item, keyed));
}

/* Test predicate against element (applying :key to element) */
static int seq_pred_match(CL_Obj pred, CL_Obj key_fn, CL_Obj elem)
{
    CL_Obj keyed = apply_key(key_fn, elem);
    return !CL_NULL_P(call_1(pred, keyed));
}

/* --- Sequence element access helpers --- */

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
/* FIND / FIND-IF / FIND-IF-NOT                            */
/* ======================================================= */

static CL_Obj bi_find(CL_Obj *args, int n)
{
    CL_Obj item = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        /* Forward scan */
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_test_match(&sa, item, cl_car(cur)))
                    return cl_car(cur);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (seq_test_match(&sa, item, elem))
                    return elem;
            }
        }
    } else {
        /* :from-end — forward scan tracking last match */
        CL_Obj found = CL_NIL;
        int found_p = 0;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_test_match(&sa, item, cl_car(cur))) {
                    found = cl_car(cur);
                    found_p = 1;
                }
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (seq_test_match(&sa, item, elem)) {
                    found = elem;
                    found_p = 1;
                }
            }
        }
        if (found_p) return found;
    }
    return CL_NIL;
}

static CL_Obj bi_find_if(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    return cl_car(cur);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (seq_pred_match(pred, sa.key_fn, elem))
                    return elem;
            }
        }
    } else {
        CL_Obj found = CL_NIL;
        int found_p = 0;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_pred_match(pred, sa.key_fn, cl_car(cur))) {
                    found = cl_car(cur);
                    found_p = 1;
                }
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (seq_pred_match(pred, sa.key_fn, elem)) {
                    found = elem;
                    found_p = 1;
                }
            }
        }
        if (found_p) return found;
    }
    return CL_NIL;
}

static CL_Obj bi_find_if_not(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && !seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    return cl_car(cur);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (!seq_pred_match(pred, sa.key_fn, elem))
                    return elem;
            }
        }
    } else {
        CL_Obj found = CL_NIL;
        int found_p = 0;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && !seq_pred_match(pred, sa.key_fn, cl_car(cur))) {
                    found = cl_car(cur);
                    found_p = 1;
                }
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (!seq_pred_match(pred, sa.key_fn, elem)) {
                    found = elem;
                    found_p = 1;
                }
            }
        }
        if (found_p) return found;
    }
    return CL_NIL;
}

/* ======================================================= */
/* POSITION / POSITION-IF / POSITION-IF-NOT                */
/* ======================================================= */

static CL_Obj bi_position(CL_Obj *args, int n)
{
    CL_Obj item = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_test_match(&sa, item, cl_car(cur)))
                    return CL_MAKE_FIXNUM(i);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                CL_Obj elem = seq_elt(seq, i);
                if (seq_test_match(&sa, item, elem))
                    return CL_MAKE_FIXNUM(i);
            }
        }
    } else {
        int32_t found = -1;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_test_match(&sa, item, cl_car(cur)))
                    found = i;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (seq_test_match(&sa, item, seq_elt(seq, i)))
                    found = i;
            }
        }
        if (found >= 0) return CL_MAKE_FIXNUM(found);
    }
    return CL_NIL;
}

static CL_Obj bi_position_if(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    return CL_MAKE_FIXNUM(i);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    return CL_MAKE_FIXNUM(i);
            }
        }
    } else {
        int32_t found = -1;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    found = i;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    found = i;
            }
        }
        if (found >= 0) return CL_MAKE_FIXNUM(found);
    }
    return CL_NIL;
}

static CL_Obj bi_position_if_not(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (!sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && !seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    return CL_MAKE_FIXNUM(i);
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (!seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    return CL_MAKE_FIXNUM(i);
            }
        }
    } else {
        int32_t found = -1;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && !seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    found = i;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (!seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    found = i;
            }
        }
        if (found >= 0) return CL_MAKE_FIXNUM(found);
    }
    return CL_NIL;
}

/* ======================================================= */
/* COUNT / COUNT-IF / COUNT-IF-NOT                         */
/* ======================================================= */

static CL_Obj bi_count(CL_Obj *args, int n)
{
    CL_Obj item = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end, cnt = 0;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            int32_t range = end - sa.start;
            if (range > 0) {
                CL_Obj cur = seq;
                CL_Obj *tmp = (CL_Obj *)platform_alloc(range * sizeof(CL_Obj));
                int32_t j = 0;
                for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                    if (i >= sa.start) tmp[j++] = cl_car(cur);
                }
                for (i = j - 1; i >= 0; i--) {
                    if (seq_test_match(&sa, item, tmp[i])) cnt++;
                }
                platform_free(tmp);
            }
        } else {
            for (i = end - 1; i >= sa.start; i--) {
                if (seq_test_match(&sa, item, seq_elt(seq, i)))
                    cnt++;
            }
        }
    } else {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_test_match(&sa, item, cl_car(cur)))
                    cnt++;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (seq_test_match(&sa, item, seq_elt(seq, i)))
                    cnt++;
            }
        }
    }
    return CL_MAKE_FIXNUM(cnt);
}

static CL_Obj bi_count_if(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end, cnt = 0;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (sa.from_end) {
        /* :from-end — iterate in reverse order (matters for stateful predicates) */
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            /* Collect elements in range into temp array, then iterate backwards */
            int32_t range = end - sa.start;
            if (range > 0) {
                CL_Obj cur = seq;
                CL_Obj *tmp = (CL_Obj *)platform_alloc(range * sizeof(CL_Obj));
                int32_t j = 0;
                for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                    if (i >= sa.start) tmp[j++] = cl_car(cur);
                }
                for (i = j - 1; i >= 0; i--) {
                    if (seq_pred_match(pred, sa.key_fn, tmp[i])) cnt++;
                }
                platform_free(tmp);
            }
        } else {
            for (i = end - 1; i >= sa.start; i--) {
                if (seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    cnt++;
            }
        }
    } else {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    cnt++;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    cnt++;
            }
        }
    }
    return CL_MAKE_FIXNUM(cnt);
}

static CL_Obj bi_count_if_not(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    int32_t i, len, end, cnt = 0;

    parse_seq_args(args, n, 2, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (sa.from_end) {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            int32_t range = end - sa.start;
            if (range > 0) {
                CL_Obj cur = seq;
                CL_Obj *tmp = (CL_Obj *)platform_alloc(range * sizeof(CL_Obj));
                int32_t j = 0;
                for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                    if (i >= sa.start) tmp[j++] = cl_car(cur);
                }
                for (i = j - 1; i >= 0; i--) {
                    if (!seq_pred_match(pred, sa.key_fn, tmp[i])) cnt++;
                }
                platform_free(tmp);
            }
        } else {
            for (i = end - 1; i >= sa.start; i--) {
                if (!seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    cnt++;
            }
        }
    } else {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; i < end && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && !seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                    cnt++;
            }
        } else {
            for (i = sa.start; i < end; i++) {
                if (!seq_pred_match(pred, sa.key_fn, seq_elt(seq, i)))
                    cnt++;
            }
        }
    }
    return CL_MAKE_FIXNUM(cnt);
}

/* ======================================================= */
/* REMOVE / REMOVE-IF / REMOVE-IF-NOT / REMOVE-DUPLICATES */
/* ======================================================= */

/* Helper: build a result list from elements, skipping marked positions.
   Works for list sequences by collecting non-removed elements. */
static CL_Obj remove_from_list(CL_Obj seq, int32_t start, int32_t end,
                               int32_t count, int from_end,
                               CL_Obj item, CL_Obj test_fn, CL_Obj key_fn,
                               int mode) /* 0=test-item, 1=pred, 2=pred-not, 3=test-not-item */
{
    CL_Obj result = CL_NIL, tail = CL_NIL;
    CL_Obj cur;
    int32_t i, removed = 0;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    if (from_end && count >= 0) {
        /* Two-pass: count matches from end */
        int32_t total_matches = 0;
        int32_t skip_count;
        cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            if (i >= start && (end < 0 || i < end)) {
                CL_Obj elem = cl_car(cur);
                int match = 0;
                if (mode == 0) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 3) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 1) {
                    match = seq_pred_match(item, key_fn, elem);
                } else {
                    match = !seq_pred_match(item, key_fn, elem);
                }
                if (match) total_matches++;
            }
        }
        /* Skip first (total_matches - count) matches from the front */
        skip_count = total_matches - count;
        if (skip_count < 0) skip_count = 0;

        cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int match = 0;
            if (i >= start && (end < 0 || i < end)) {
                if (mode == 0) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 3) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 1) {
                    match = seq_pred_match(item, key_fn, elem);
                } else {
                    match = !seq_pred_match(item, key_fn, elem);
                }
            }
            if (match) {
                if (skip_count > 0) {
                    skip_count--;
                    match = 0; /* keep this one */
                }
            }
            if (!match) {
                CL_Obj cell = cl_cons(elem, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
    } else {
        /* Forward removal */
        cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int should_remove = 0;

            if (i >= start && (end < 0 || i < end) && (count < 0 || removed < count)) {
                if (mode == 0) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    should_remove = !CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 3) {
                    CL_Obj keyed = apply_key(key_fn, elem);
                    should_remove = CL_NULL_P(call_test(test_fn, item, keyed));
                } else if (mode == 1) {
                    should_remove = seq_pred_match(item, key_fn, elem);
                } else {
                    should_remove = !seq_pred_match(item, key_fn, elem);
                }
            }

            if (should_remove) {
                removed++;
            } else {
                CL_Obj cell = cl_cons(elem, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
    }

    CL_GC_UNPROTECT(2);
    return result;
}

/* remove_from_string: shared string path for remove/remove-if/remove-if-not.
   mode: 0=remove (item+test), 1=remove-if (pred), 2=remove-if-not (pred) */
static CL_Obj remove_from_string(CL_Obj seq, CL_Obj item_or_pred,
                                  CL_Obj test_fn, CL_Obj key_fn,
                                  int32_t start, int32_t end,
                                  int32_t count, int mode)
{
    int32_t slen = (int32_t)cl_string_length(seq);
    char buf[1024];
    int32_t out = 0, i, removed = 0;

    if (end < 0) end = slen;

    for (i = 0; i < slen && out < (int32_t)sizeof(buf) - 1; i++) {
        CL_Obj elem = CL_MAKE_CHAR(cl_string_char_at(seq, (uint32_t)i));
        int should_remove = 0;
        if (i >= start && i < end && (count < 0 || removed < count)) {
            CL_Obj keyed = apply_key(key_fn, elem);
            if (mode == 0) {
                should_remove = !CL_NULL_P(call_test(test_fn, item_or_pred, keyed));
            } else if (mode == 1) {
                should_remove = seq_pred_match(item_or_pred, CL_NIL, elem);
            } else {
                should_remove = !seq_pred_match(item_or_pred, CL_NIL, elem);
            }
        }
        if (should_remove) {
            removed++;
        } else {
            buf[out++] = (char)cl_string_char_at(seq, (uint32_t)i);
        }
    }
    return cl_make_string(buf, (uint32_t)out);
}

/* remove_from_bitvector: shared bit-vector path for remove/remove-if/remove-if-not.
   mode: 0=test-item, 1=pred, 2=pred-not, 3=test-not-item */
static CL_Obj remove_from_bitvector(CL_Obj seq, int32_t start, int32_t end,
                                     int32_t count, int from_end,
                                     CL_Obj item, CL_Obj test_fn, CL_Obj key_fn,
                                     int mode)
{
    CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
    int32_t bvlen = (int32_t)cl_bv_active_length(bv);
    int32_t i, out = 0, removed = 0;
    uint8_t *keep;
    CL_Obj result;

    if (end < 0) end = bvlen;
    if (bvlen == 0) return cl_make_bit_vector(0);

    keep = (uint8_t *)platform_alloc((uint32_t)bvlen);
    memset(keep, 1, (uint32_t)bvlen);

    if (from_end && count >= 0) {
        /* Two-pass: count total matches, then skip first (total-count) from front */
        int32_t total_matches = 0, skip_count;
        for (i = 0; i < bvlen; i++) {
            if (i >= start && i < end) {
                CL_Obj elem, keyed;
                int match;
                bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
                elem = CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)i));
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    match = !CL_NULL_P(call_1(item, keyed));
                else
                    match = CL_NULL_P(call_1(item, keyed));
                if (match) total_matches++;
            }
        }
        skip_count = total_matches - count;
        if (skip_count < 0) skip_count = 0;
        for (i = 0; i < bvlen; i++) {
            if (i >= start && i < end) {
                CL_Obj elem, keyed;
                int match;
                bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
                elem = CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)i));
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    match = !CL_NULL_P(call_1(item, keyed));
                else
                    match = CL_NULL_P(call_1(item, keyed));
                if (match) {
                    if (skip_count > 0) {
                        skip_count--;
                    } else {
                        keep[i] = 0;
                    }
                }
            }
        }
    } else {
        /* Forward removal */
        for (i = 0; i < bvlen; i++) {
            int should_remove = 0;
            if (i >= start && i < end && (count < 0 || removed < count)) {
                CL_Obj elem, keyed;
                bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
                elem = CL_MAKE_FIXNUM(cl_bv_get_bit(bv, (uint32_t)i));
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    should_remove = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    should_remove = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    should_remove = !CL_NULL_P(call_1(item, keyed));
                else
                    should_remove = CL_NULL_P(call_1(item, keyed));
            }
            if (should_remove) {
                keep[i] = 0;
                removed++;
            }
        }
    }

    /* Count surviving bits */
    for (i = 0; i < bvlen; i++)
        if (keep[i]) out++;

    result = cl_make_bit_vector((uint32_t)out);
    bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
    {
        CL_BitVector *rbv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
        int32_t j = 0;
        for (i = 0; i < bvlen; i++) {
            if (keep[i]) {
                if (cl_bv_get_bit(bv, (uint32_t)i))
                    cl_bv_set_bit(rbv, (uint32_t)j, 1);
                j++;
            }
        }
    }

    platform_free(keep);
    return result;
}

/* remove_from_vector: shared vector path for remove/remove-if/remove-if-not.
   mode: 0=test-item, 1=pred, 2=pred-not, 3=test-not-item */
static CL_Obj remove_from_vector(CL_Obj seq, int32_t start, int32_t end,
                                  int32_t count, int from_end,
                                  CL_Obj item, CL_Obj test_fn, CL_Obj key_fn,
                                  int mode)
{
    CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
    int32_t vlen = (int32_t)cl_vector_active_length(v);
    int32_t i, out = 0, removed = 0;
    uint8_t *keep;
    CL_Obj result;

    if (end < 0) end = vlen;
    if (vlen == 0) return cl_make_vector(0);

    keep = (uint8_t *)platform_alloc((uint32_t)vlen);
    memset(keep, 1, (uint32_t)vlen);

    if (from_end && count >= 0) {
        int32_t total_matches = 0, skip_count;
        for (i = 0; i < vlen; i++) {
            if (i >= start && i < end) {
                CL_Obj elem, keyed;
                int match;
                v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                elem = cl_vector_data(v)[i];
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    match = !CL_NULL_P(call_1(item, keyed));
                else
                    match = CL_NULL_P(call_1(item, keyed));
                if (match) total_matches++;
            }
        }
        skip_count = total_matches - count;
        if (skip_count < 0) skip_count = 0;
        for (i = 0; i < vlen; i++) {
            if (i >= start && i < end) {
                CL_Obj elem, keyed;
                int match;
                v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                elem = cl_vector_data(v)[i];
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    match = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    match = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    match = !CL_NULL_P(call_1(item, keyed));
                else
                    match = CL_NULL_P(call_1(item, keyed));
                if (match) {
                    if (skip_count > 0) skip_count--;
                    else keep[i] = 0;
                }
            }
        }
    } else {
        for (i = 0; i < vlen; i++) {
            int should_remove = 0;
            if (i >= start && i < end && (count < 0 || removed < count)) {
                CL_Obj elem, keyed;
                v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                elem = cl_vector_data(v)[i];
                keyed = apply_key(key_fn, elem);
                if (mode == 0)
                    should_remove = !CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 3)
                    should_remove = CL_NULL_P(call_test(test_fn, item, keyed));
                else if (mode == 1)
                    should_remove = !CL_NULL_P(call_1(item, keyed));
                else
                    should_remove = CL_NULL_P(call_1(item, keyed));
            }
            if (should_remove) {
                keep[i] = 0;
                removed++;
            }
        }
    }

    for (i = 0; i < vlen; i++)
        if (keep[i]) out++;

    result = cl_make_vector((uint32_t)out);
    v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
    {
        CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
        CL_Obj *relts = cl_vector_data(rv);
        CL_Obj *elts = cl_vector_data(v);
        int32_t j = 0;
        for (i = 0; i < vlen; i++) {
            if (keep[i])
                relts[j++] = elts[i];
        }
    }

    platform_free(keep);
    return result;
}

static CL_Obj bi_remove(CL_Obj *args, int n)
{
    CL_Obj item = args[0], seq = args[1];
    SeqArgs sa;
    parse_seq_args(args, n, 2, &sa);

    if (CL_NULL_P(seq)) return CL_NIL;
    if (CL_CONS_P(seq)) {
        int32_t len = seq_length(seq);
        int32_t end = (sa.end < 0) ? len : sa.end;
        if (!CL_NULL_P(sa.test_not_fn))
            return remove_from_list(seq, sa.start, end, sa.count, sa.from_end,
                                    item, sa.test_not_fn, sa.key_fn, 3);
        return remove_from_list(seq, sa.start, end, sa.count, sa.from_end,
                                item, sa.test_fn, sa.key_fn, 0);
    }
    if (CL_ANY_STRING_P(seq)) {
        return remove_from_string(seq, item, sa.test_fn, sa.key_fn,
                                  sa.start, sa.end, sa.count, 0);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        if (!CL_NULL_P(sa.test_not_fn))
            return remove_from_bitvector(seq, sa.start, sa.end, sa.count, sa.from_end,
                                         item, sa.test_not_fn, sa.key_fn, 3);
        return remove_from_bitvector(seq, sa.start, sa.end, sa.count, sa.from_end,
                                     item, sa.test_fn, sa.key_fn, 0);
    }
    /* Vector path */
    if (!CL_NULL_P(sa.test_not_fn))
        return remove_from_vector(seq, sa.start, sa.end, sa.count, sa.from_end,
                                  item, sa.test_not_fn, sa.key_fn, 3);
    return remove_from_vector(seq, sa.start, sa.end, sa.count, sa.from_end,
                              item, sa.test_fn, sa.key_fn, 0);
}

static CL_Obj bi_remove_if(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    parse_seq_args(args, n, 2, &sa);

    if (CL_NULL_P(seq)) return CL_NIL;
    if (CL_CONS_P(seq)) {
        int32_t len = seq_length(seq);
        int32_t end = (sa.end < 0) ? len : sa.end;
        return remove_from_list(seq, sa.start, end, sa.count, sa.from_end,
                                pred, CL_NIL, sa.key_fn, 1);
    }
    if (CL_ANY_STRING_P(seq)) {
        return remove_from_string(seq, pred, CL_NIL, sa.key_fn,
                                  sa.start, sa.end, sa.count, 1);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        return remove_from_bitvector(seq, sa.start, sa.end, sa.count, sa.from_end,
                                     pred, CL_NIL, sa.key_fn, 1);
    }
    /* Vector path */
    return remove_from_vector(seq, sa.start, sa.end, sa.count, sa.from_end,
                              pred, CL_NIL, sa.key_fn, 1);
}

static CL_Obj bi_remove_if_not(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], seq = args[1];
    SeqArgs sa;
    parse_seq_args(args, n, 2, &sa);

    if (CL_NULL_P(seq)) return CL_NIL;
    if (CL_CONS_P(seq)) {
        int32_t len = seq_length(seq);
        int32_t end = (sa.end < 0) ? len : sa.end;
        return remove_from_list(seq, sa.start, end, sa.count, sa.from_end,
                                pred, CL_NIL, sa.key_fn, 2);
    }
    if (CL_ANY_STRING_P(seq)) {
        return remove_from_string(seq, pred, CL_NIL, sa.key_fn,
                                  sa.start, sa.end, sa.count, 2);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        return remove_from_bitvector(seq, sa.start, sa.end, sa.count, sa.from_end,
                                     pred, CL_NIL, sa.key_fn, 2);
    }
    /* Vector path */
    return remove_from_vector(seq, sa.start, sa.end, sa.count, sa.from_end,
                              pred, CL_NIL, sa.key_fn, 2);
}

static CL_Obj bi_remove_duplicates(CL_Obj *args, int n)
{
    CL_Obj seq = args[0];
    SeqArgs sa;
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int32_t i, j, len, end;

    parse_seq_args(args, n, 1, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        /* For lists: keep last occurrence (per CL spec with :from-end nil) */
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int dup = 0;

            if (i >= sa.start && i < end) {
                /* Check if elem appears later in the range */
                CL_Obj ahead = cl_cdr(cur);
                for (j = i + 1; !CL_NULL_P(ahead) && j < end; j++, ahead = cl_cdr(ahead)) {
                    CL_Obj keyed_i = apply_key(sa.key_fn, elem);
                    CL_Obj keyed_j = apply_key(sa.key_fn, cl_car(ahead));
                    if (!CL_NULL_P(call_test(sa.test_fn, keyed_i, keyed_j))) {
                        dup = 1;
                        break;
                    }
                }
            }

            if (!dup) {
                CL_Obj cell = cl_cons(elem, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        int32_t out_count = 0;
        uint8_t *keep = (uint8_t *)platform_alloc((uint32_t)vlen);
        memset(keep, 1, (uint32_t)vlen);

        for (i = 0; i < vlen; i++) {
            if (i >= sa.start && i < end && keep[i]) {
                v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                for (j = i + 1; j < end; j++) {
                    if (keep[j]) {
                        CL_Obj keyed_i, keyed_j;
                        v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                        keyed_i = apply_key(sa.key_fn, cl_vector_data(v)[i]);
                        v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                        keyed_j = apply_key(sa.key_fn, cl_vector_data(v)[j]);
                        if (!CL_NULL_P(call_test(sa.test_fn, keyed_i, keyed_j))) {
                            keep[i] = 0;
                            break;
                        }
                    }
                }
            }
        }

        for (i = 0; i < vlen; i++)
            if (keep[i]) out_count++;

        result = cl_make_vector((uint32_t)out_count);
        v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        {
            CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
            CL_Obj *relts = cl_vector_data(rv);
            CL_Obj *elts = cl_vector_data(v);
            int32_t k = 0;
            for (i = 0; i < vlen; i++) {
                if (keep[i])
                    relts[k++] = elts[i];
            }
        }
        platform_free(keep);
    }

    CL_GC_UNPROTECT(2);
    return result;
}

/* ======================================================= */
/* SUBSTITUTE / SUBSTITUTE-IF / SUBSTITUTE-IF-NOT          */
/* ======================================================= */

static CL_Obj bi_substitute(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], olditem = args[1], seq = args[2];
    SeqArgs sa;
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);
    CL_GC_PROTECT(newitem);

    if (sa.from_end && sa.count >= 0) {
        /* :from-end with :count — find all matches first, then keep only the last count */
        int32_t total_matches = 0;
        int32_t skip;
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                if (i >= sa.start && i < end && seq_test_match(&sa, olditem, cl_car(cur)))
                    total_matches++;
            }
            skip = total_matches - sa.count;
            if (skip < 0) skip = 0;
            cur = seq;
            for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                CL_Obj elem = cl_car(cur);
                int match = 0;
                if (i >= sa.start && i < end && seq_test_match(&sa, olditem, elem)) {
                    replaced++;
                    if (replaced > skip) match = 1;
                }
                {
                    CL_Obj val = match ? newitem : elem;
                    CL_Obj cell = cl_cons(val, CL_NIL);
                    if (CL_NULL_P(result)) result = cell;
                    else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                    tail = cell;
                }
            }
        } else if (CL_ANY_STRING_P(seq)) {
            /* String from-end: return string */
            uint32_t slen = cl_string_length(seq);
            result = cl_string_copy(seq);
            for (i = sa.start; i < end && i < (int32_t)slen; i++) {
                CL_Obj elem = CL_MAKE_CHAR(cl_string_char_at(seq, (uint32_t)i));
                if (seq_test_match(&sa, olditem, elem))
                    total_matches++;
            }
            skip = total_matches - sa.count;
            if (skip < 0) skip = 0;
            for (i = sa.start; i < end && i < (int32_t)slen; i++) {
                CL_Obj elem = CL_MAKE_CHAR(cl_string_char_at(result, (uint32_t)i));
                if (seq_test_match(&sa, olditem, elem)) {
                    replaced++;
                    if (replaced > skip && CL_CHAR_P(newitem))
                        cl_string_set_char_at(result, (uint32_t)i, CL_CHAR_VAL(newitem));
                }
            }
        } else if (CL_VECTOR_P(seq)) {
            int32_t vlen = seq_length(seq);
            for (i = sa.start; i < end && i < vlen; i++) {
                if (seq_test_match(&sa, olditem, seq_elt(seq, i)))
                    total_matches++;
            }
            skip = total_matches - sa.count;
            if (skip < 0) skip = 0;
            result = cl_make_vector((uint32_t)vlen);
            for (i = 0; i < vlen; i++) {
                CL_Obj elem = seq_elt(seq, i);
                int match = 0;
                if (i >= sa.start && i < end && seq_test_match(&sa, olditem, elem)) {
                    replaced++;
                    if (replaced > skip) match = 1;
                }
                {
                    CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
                    cl_vector_data(rv)[i] = match ? newitem : elem;
                }
            }
        }
    } else {
        if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
            CL_Obj cur = seq;
            for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
                CL_Obj elem = cl_car(cur);
                int match = 0;
                if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                    match = seq_test_match(&sa, olditem, elem);
                }
                {
                    CL_Obj val = match ? newitem : elem;
                    CL_Obj cell = cl_cons(val, CL_NIL);
                    if (CL_NULL_P(result)) result = cell;
                    else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                    tail = cell;
                    if (match) replaced++;
                }
            }
        } else if (CL_ANY_STRING_P(seq)) {
            /* String: return a new string with character substitutions */
            uint32_t slen = cl_string_length(seq);
            result = cl_string_copy(seq);
            for (i = sa.start; i < end && i < (int32_t)slen; i++) {
                if (sa.count >= 0 && replaced >= sa.count) break;
                {
                    CL_Obj elem = CL_MAKE_CHAR(cl_string_char_at(result, (uint32_t)i));
                    if (seq_test_match(&sa, olditem, elem)) {
                        if (CL_CHAR_P(newitem))
                            cl_string_set_char_at(result, (uint32_t)i, CL_CHAR_VAL(newitem));
                        replaced++;
                    }
                }
            }
        } else if (CL_VECTOR_P(seq)) {
            int32_t vlen = seq_length(seq);
            result = cl_make_vector((uint32_t)vlen);
            for (i = 0; i < vlen; i++) {
                CL_Obj elem = seq_elt(seq, i);
                int match = 0;
                if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                    match = seq_test_match(&sa, olditem, elem);
                }
                {
                    CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
                    cl_vector_data(rv)[i] = match ? newitem : elem;
                    if (match) replaced++;
                }
            }
        }
    }

    CL_GC_UNPROTECT(3);
    return result;
}

static CL_Obj bi_substitute_if(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], pred = args[1], seq = args[2];
    SeqArgs sa;
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);
    CL_GC_PROTECT(newitem);

    if (sa.from_end && sa.count >= 0 && (CL_CONS_P(seq) || CL_NULL_P(seq))) {
        /* :from-end with :count on list — two-pass */
        int32_t total_matches = 0, skip;
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            if (i >= sa.start && i < end && seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                total_matches++;
        }
        skip = total_matches - sa.count;
        if (skip < 0) skip = 0;
        cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int match = 0;
            if (i >= sa.start && i < end && seq_pred_match(pred, sa.key_fn, elem)) {
                replaced++;
                if (replaced > skip) match = 1;
            }
            {
                CL_Obj val = match ? newitem : elem;
                CL_Obj cell = cl_cons(val, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
    } else if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int match = 0;
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count))
                match = seq_pred_match(pred, sa.key_fn, elem);
            {
                CL_Obj val = match ? newitem : elem;
                CL_Obj cell = cl_cons(val, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
                if (match) replaced++;
            }
        }
    } else if (CL_ANY_STRING_P(seq)) {
        /* String: return a new string with substitutions */
        uint32_t slen = cl_string_length(seq);
        result = cl_string_copy(seq);
        for (i = sa.start; i < end && i < (int32_t)slen; i++) {
            if (sa.count >= 0 && replaced >= sa.count) break;
            {
                CL_Obj elem = CL_MAKE_CHAR(cl_string_char_at(result, (uint32_t)i));
                if (seq_pred_match(pred, sa.key_fn, elem)) {
                    if (CL_CHAR_P(newitem))
                        cl_string_set_char_at(result, (uint32_t)i, CL_CHAR_VAL(newitem));
                    replaced++;
                }
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        CL_Obj *elts;
        /* Build a new vector with substitutions */
        result = cl_make_vector((uint32_t)vlen);
        CL_GC_PROTECT(result);
        v = (CL_Vector *)CL_OBJ_TO_PTR(seq); /* refresh after alloc */
        elts = cl_vector_data(v);
        {
            CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
            CL_Obj *relts = cl_vector_data(rv);
            for (i = 0; i < vlen; i++) {
                CL_Obj elem = elts[i];
                int match = 0;
                if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count))
                    match = seq_pred_match(pred, sa.key_fn, elem);
                relts[i] = match ? newitem : elem;
                if (match) replaced++;
            }
        }
        CL_GC_UNPROTECT(1); /* extra protect for result */
    }

    CL_GC_UNPROTECT(3);
    return result;
}

static CL_Obj bi_substitute_if_not(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], pred = args[1], seq = args[2];
    SeqArgs sa;
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);
    CL_GC_PROTECT(newitem);

    if (sa.from_end && sa.count >= 0 && (CL_CONS_P(seq) || CL_NULL_P(seq))) {
        int32_t total_matches = 0, skip;
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            if (i >= sa.start && i < end && !seq_pred_match(pred, sa.key_fn, cl_car(cur)))
                total_matches++;
        }
        skip = total_matches - sa.count;
        if (skip < 0) skip = 0;
        cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int match = 0;
            if (i >= sa.start && i < end && !seq_pred_match(pred, sa.key_fn, elem)) {
                replaced++;
                if (replaced > skip) match = 1;
            }
            {
                CL_Obj val = match ? newitem : elem;
                CL_Obj cell = cl_cons(val, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
            }
        }
    } else if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            int match = 0;
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count))
                match = !seq_pred_match(pred, sa.key_fn, elem);
            {
                CL_Obj val = match ? newitem : elem;
                CL_Obj cell = cl_cons(val, CL_NIL);
                if (CL_NULL_P(result)) result = cell;
                else ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                tail = cell;
                if (match) replaced++;
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        result = cl_make_vector((uint32_t)vlen);
        v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        for (i = 0; i < vlen; i++) {
            CL_Obj elem = cl_vector_data(v)[i];
            int match = 0;
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count))
                match = !seq_pred_match(pred, sa.key_fn, elem);
            {
                CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
                v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
                cl_vector_data(rv)[i] = match ? newitem : cl_vector_data(v)[i];
                if (match) replaced++;
            }
        }
    }

    CL_GC_UNPROTECT(3);
    return result;
}

/* ======================================================= */
/* NSUBSTITUTE (destructive)                               */
/* ======================================================= */

static CL_Obj bi_nsubstitute(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], olditem = args[1], seq = args[2];
    SeqArgs sa;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (seq_test_match(&sa, olditem, elem)) {
                    ((CL_Cons *)CL_OBJ_TO_PTR(cur))->car = newitem;
                    replaced++;
                }
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        CL_Obj *elts = cl_vector_data(v);
        for (i = 0; i < vlen; i++) {
            CL_Obj elem = elts[i];
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (seq_test_match(&sa, olditem, elem)) {
                    elts[i] = newitem;
                    replaced++;
                }
            }
        }
    }
    return seq;
}

static CL_Obj bi_nsubstitute_if(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], pred = args[1], seq = args[2];
    SeqArgs sa;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (seq_pred_match(pred, sa.key_fn, elem)) {
                    ((CL_Cons *)CL_OBJ_TO_PTR(cur))->car = newitem;
                    replaced++;
                }
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        CL_Obj *elts = cl_vector_data(v);
        for (i = 0; i < vlen; i++) {
            CL_Obj elem = elts[i];
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (seq_pred_match(pred, sa.key_fn, elem)) {
                    elts[i] = newitem;
                    replaced++;
                }
            }
        }
    }
    return seq;
}

static CL_Obj bi_nsubstitute_if_not(CL_Obj *args, int n)
{
    CL_Obj newitem = args[0], pred = args[1], seq = args[2];
    SeqArgs sa;
    int32_t i, len, end, replaced = 0;

    parse_seq_args(args, n, 3, &sa);
    len = seq_length(seq);
    end = (sa.end < 0) ? len : sa.end;

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            CL_Obj elem = cl_car(cur);
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (!seq_pred_match(pred, sa.key_fn, elem)) {
                    ((CL_Cons *)CL_OBJ_TO_PTR(cur))->car = newitem;
                    replaced++;
                }
            }
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        CL_Obj *elts = cl_vector_data(v);
        for (i = 0; i < vlen; i++) {
            CL_Obj elem = elts[i];
            if (i >= sa.start && i < end && (sa.count < 0 || replaced < sa.count)) {
                if (!seq_pred_match(pred, sa.key_fn, elem)) {
                    elts[i] = newitem;
                    replaced++;
                }
            }
        }
    }
    return seq;
}

/* ======================================================= */
/* REDUCE                                                  */
/* ======================================================= */

static CL_Obj bi_reduce(CL_Obj *args, int n)
{
    CL_Obj func = args[0], seq = args[1];
    CL_Obj initial = CL_NIL;
    int has_initial = 0, from_end = 0;
    int32_t start = 0, end_val, len;
    CL_Obj key_fn = CL_NIL;
    CL_Obj accum;
    int i;

    /* Parse keyword args manually */
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_INITIAL_VALUE) {
            initial = args[i + 1];
            has_initial = 1;
        } else if (args[i] == KW_KEY) {
            key_fn = args[i + 1];
        } else if (args[i] == KW_START) {
            if (CL_FIXNUM_P(args[i + 1])) start = CL_FIXNUM_VAL(args[i + 1]);
        } else if (args[i] == KW_END) {
            /* handled below */
        } else if (args[i] == KW_FROM_END) {
            if (!CL_NULL_P(args[i + 1]))
                from_end = 1;
        }
    }

    len = seq_length(seq);
    /* Re-scan for :end */
    end_val = len;
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_END && !CL_NULL_P(args[i + 1]) && CL_FIXNUM_P(args[i + 1]))
            end_val = CL_FIXNUM_VAL(args[i + 1]);
    }

    if (start >= end_val) {
        /* Empty subsequence */
        if (has_initial) return initial;
        /* Call function with zero args */
        return call_1(func, CL_NIL); /* This isn't quite right but CL spec says identity */
    }

    if (from_end) {
        /* :from-end T — process from right to left.
         * For right-associative reduction: f(e1, f(e2, f(e3, init)))
         * Arguments to f are: (element, accumulator) */
        int32_t sub_len = end_val - start;

        if (has_initial) {
            accum = initial;
        } else {
            accum = apply_key(key_fn, seq_elt(seq, end_val - 1));
            end_val--;
            sub_len--;
        }

        CL_GC_PROTECT(accum);
        CL_GC_PROTECT(func);

        if (CL_VECTOR_P(seq)) {
            int32_t idx;
            for (idx = end_val - 1; idx >= start; idx--) {
                CL_Obj elem = apply_key(key_fn, seq_elt(seq, idx));
                accum = call_test(func, elem, accum);
            }
        } else {
            /* List: collect elements into temp array, then iterate backwards */
            CL_Obj *elts = (CL_Obj *)platform_alloc(
                (uint32_t)(sub_len * sizeof(CL_Obj)));
            CL_Obj cur = seq;
            int32_t idx = 0, j = 0;
            while (idx < start && !CL_NULL_P(cur)) { cur = cl_cdr(cur); idx++; }
            while (idx < end_val && !CL_NULL_P(cur)) {
                elts[j++] = cl_car(cur);
                cur = cl_cdr(cur);
                idx++;
            }
            for (idx = j - 1; idx >= 0; idx--) {
                CL_Obj elem = apply_key(key_fn, elts[idx]);
                accum = call_test(func, elem, accum);
            }
            platform_free(elts);
        }

        CL_GC_UNPROTECT(2);
        return accum;
    }

    /* Forward (default) reduction */
    if (has_initial) {
        accum = initial;
    } else {
        accum = apply_key(key_fn, seq_elt(seq, start));
        start++;
    }

    CL_GC_PROTECT(accum);
    CL_GC_PROTECT(func);

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        int32_t idx = 0;
        /* Skip to start */
        while (idx < start && !CL_NULL_P(cur)) { cur = cl_cdr(cur); idx++; }
        while (idx < end_val && !CL_NULL_P(cur)) {
            CL_Obj elem = apply_key(key_fn, cl_car(cur));
            accum = call_test(func, accum, elem);
            cur = cl_cdr(cur);
            idx++;
        }
    } else {
        int32_t idx;
        for (idx = start; idx < end_val; idx++) {
            CL_Obj elem = apply_key(key_fn, seq_elt(seq, idx));
            accum = call_test(func, accum, elem);
        }
    }

    CL_GC_UNPROTECT(2);
    return accum;
}

/* ======================================================= */
/* FILL                                                    */
/* ======================================================= */

static CL_Obj bi_fill(CL_Obj *args, int n)
{
    CL_Obj seq = args[0], item = args[1];
    int32_t start = 0, end_val, len;
    int32_t i;

    /* Parse :start :end */
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_START && CL_FIXNUM_P(args[i + 1]))
            start = CL_FIXNUM_VAL(args[i + 1]);
        else if (args[i] == KW_END && !CL_NULL_P(args[i + 1]) && CL_FIXNUM_P(args[i + 1]))
            end_val = CL_FIXNUM_VAL(args[i + 1]);
    }

    len = seq_length(seq);
    /* Re-scan for :end (need to set default after knowing length) */
    end_val = len;
    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_END && !CL_NULL_P(args[i + 1]) && CL_FIXNUM_P(args[i + 1]))
            end_val = CL_FIXNUM_VAL(args[i + 1]);
    }

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        CL_Obj cur = seq;
        for (i = 0; i < end_val && !CL_NULL_P(cur); i++, cur = cl_cdr(cur)) {
            if (i >= start)
                ((CL_Cons *)CL_OBJ_TO_PTR(cur))->car = item;
        }
    } else if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        CL_Obj *elts = cl_vector_data(v);
        int32_t vlen = (int32_t)cl_vector_active_length(v);
        for (i = start; i < end_val && i < vlen; i++)
            elts[i] = item;
    } else if (CL_ANY_STRING_P(seq)) {
        int32_t slen = (int32_t)cl_string_length(seq);
        if (!CL_CHAR_P(item))
            cl_error(CL_ERR_TYPE, "FILL: string requires a character");
        for (i = start; i < end_val && i < slen; i++)
            cl_string_set_char_at(seq, (uint32_t)i, CL_CHAR_VAL(item));
    } else if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        int32_t bvlen = (int32_t)cl_bv_active_length(bv);
        int32_t val;
        if (!CL_FIXNUM_P(item))
            cl_error(CL_ERR_TYPE, "FILL: bit vector requires 0 or 1");
        val = CL_FIXNUM_VAL(item);
        if (val != 0 && val != 1)
            cl_error(CL_ERR_TYPE, "FILL: bit vector requires 0 or 1");
        for (i = start; i < end_val && i < bvlen; i++)
            cl_bv_set_bit(bv, (uint32_t)i, val);
    }

    return seq;
}

/* ======================================================= */
/* REPLACE                                                 */
/* ======================================================= */

static CL_Obj bi_replace(CL_Obj *args, int n)
{
    CL_Obj seq1 = args[0], seq2 = args[1];
    int32_t start1 = 0, end1, start2 = 0, end2;
    int32_t len1, len2;
    int32_t i, j, count;

    /* Parse keyword args */
    len1 = seq_length(seq1);
    len2 = seq_length(seq2);
    end1 = len1;
    end2 = len2;

    for (i = 2; i + 1 < n; i += 2) {
        if (args[i] == KW_START1 && CL_FIXNUM_P(args[i + 1]))
            start1 = CL_FIXNUM_VAL(args[i + 1]);
        else if (args[i] == KW_END1 && !CL_NULL_P(args[i + 1]) && CL_FIXNUM_P(args[i + 1]))
            end1 = CL_FIXNUM_VAL(args[i + 1]);
        else if (args[i] == KW_START2 && CL_FIXNUM_P(args[i + 1]))
            start2 = CL_FIXNUM_VAL(args[i + 1]);
        else if (args[i] == KW_END2 && !CL_NULL_P(args[i + 1]) && CL_FIXNUM_P(args[i + 1]))
            end2 = CL_FIXNUM_VAL(args[i + 1]);
    }

    count = end1 - start1;
    if (end2 - start2 < count) count = end2 - start2;

    if (CL_ANY_STRING_P(seq1)) {
        /* String target */
        for (i = 0; i < count; i++) {
            int ch;
            if (CL_ANY_STRING_P(seq2))
                ch = cl_string_char_at(seq2, (uint32_t)(start2 + i));
            else {
                CL_Obj chobj = seq_elt(seq2, start2 + i);
                ch = CL_CHAR_VAL(chobj);
            }
            cl_string_set_char_at(seq1, (uint32_t)(start1 + i), ch);
        }
    } else if (CL_VECTOR_P(seq1) && CL_VECTOR_P(seq2)) {
        CL_Obj *elts1 = cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(seq1));
        CL_Obj *elts2 = cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(seq2));
        for (i = 0; i < count; i++)
            elts1[start1 + i] = elts2[start2 + i];
    } else {
        /* General case: use seq_elt + mutation */
        /* For lists, walk to position first */
        if (CL_CONS_P(seq1)) {
            CL_Obj cur1 = seq1;
            j = start2;
            for (i = 0; !CL_NULL_P(cur1); i++, cur1 = cl_cdr(cur1)) {
                if (i >= start1 && i < start1 + count) {
                    ((CL_Cons *)CL_OBJ_TO_PTR(cur1))->car = seq_elt(seq2, j);
                    j++;
                }
            }
        } else if (CL_VECTOR_P(seq1)) {
            CL_Obj *elts1 = cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(seq1));
            for (i = 0; i < count; i++)
                elts1[start1 + i] = seq_elt(seq2, start2 + i);
        }
    }

    return seq1;
}

/* --- Phase 8 Step 3: elt, (setf elt), copy-seq, map-into --- */

static CL_Obj bi_elt(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "ELT: index must be an integer");
    return seq_elt(args[0], CL_FIXNUM_VAL(args[1]));
}

static CL_Obj bi_setf_elt(CL_Obj *args, int n)
{
    /* args: sequence index new-value */
    CL_Obj seq = args[0];
    int32_t idx;
    CL_Obj val = args[2];
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[1]))
        cl_error(CL_ERR_TYPE, "(SETF ELT): index must be an integer");
    idx = CL_FIXNUM_VAL(args[1]);

    if (CL_CONS_P(seq) || CL_NULL_P(seq)) {
        while (idx > 0 && !CL_NULL_P(seq)) { seq = cl_cdr(seq); idx--; }
        if (CL_NULL_P(seq))
            cl_error(CL_ERR_ARGS, "(SETF ELT): index out of bounds");
        ((CL_Cons *)CL_OBJ_TO_PTR(seq))->car = val;
        return val;
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        if ((uint32_t)idx >= cl_vector_active_length(v))
            cl_error(CL_ERR_ARGS, "(SETF ELT): index out of bounds");
        cl_vector_data(v)[idx] = val;
        return val;
    }
    if (CL_ANY_STRING_P(seq)) {
        if ((uint32_t)idx >= cl_string_length(seq))
            cl_error(CL_ERR_ARGS, "(SETF ELT): index out of bounds");
        if (!CL_CHAR_P(val))
            cl_error(CL_ERR_TYPE, "(SETF ELT): value must be a character for string");
        cl_string_set_char_at(seq, (uint32_t)idx, CL_CHAR_VAL(val));
        return val;
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        int32_t v;
        if ((uint32_t)idx >= cl_bv_active_length(bv))
            cl_error(CL_ERR_ARGS, "(SETF ELT): index out of bounds");
        if (!CL_FIXNUM_P(val))
            cl_error(CL_ERR_TYPE, "(SETF ELT): value must be 0 or 1 for bit vector");
        v = CL_FIXNUM_VAL(val);
        if (v != 0 && v != 1)
            cl_error(CL_ERR_TYPE, "(SETF ELT): value must be 0 or 1 for bit vector");
        cl_bv_set_bit(bv, (uint32_t)idx, v);
        return val;
    }
    cl_error(CL_ERR_TYPE, "(SETF ELT): not a sequence");
    return CL_NIL;
}

static CL_Obj bi_copy_seq(CL_Obj *args, int n)
{
    CL_Obj seq = args[0];
    CL_UNUSED(n);

    if (CL_NULL_P(seq)) return CL_NIL;

    if (CL_CONS_P(seq)) {
        /* Copy list */
        CL_Obj result = CL_NIL, tail = CL_NIL;
        CL_GC_PROTECT(result);
        CL_GC_PROTECT(tail);
        while (CL_CONS_P(seq)) {
            CL_Obj cell = cl_cons(cl_car(seq), CL_NIL);
            if (CL_NULL_P(result))
                result = cell;
            else
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            tail = cell;
            seq = cl_cdr(seq);
        }
        CL_GC_UNPROTECT(2);
        return result;
    }
    if (CL_VECTOR_P(seq)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(seq);
        uint32_t alen = cl_vector_active_length(v);
        CL_Obj result = cl_make_vector(alen);
        CL_Vector *rv = (CL_Vector *)CL_OBJ_TO_PTR(result);
        memcpy(cl_vector_data(rv), cl_vector_data(v), alen * sizeof(CL_Obj));
        return result;
    }
    if (CL_ANY_STRING_P(seq)) {
        return cl_string_copy(seq);
    }
    if (CL_BIT_VECTOR_P(seq)) {
        CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(seq);
        uint32_t nwords = CL_BV_WORDS(bv->length);
        CL_Obj result = cl_make_bit_vector(bv->length);
        CL_BitVector *rv = (CL_BitVector *)CL_OBJ_TO_PTR(result);
        memcpy(rv->data, bv->data, nwords * sizeof(uint32_t));
        return result;
    }
    cl_error(CL_ERR_TYPE, "COPY-SEQ: not a sequence");
    return CL_NIL;
}

static CL_Obj bi_map_into(CL_Obj *args, int n)
{
    CL_Obj result_seq = args[0];
    CL_Obj func = cl_coerce_funcdesig(args[1], "MAP-INTO");
    int n_seqs = n - 2;
    CL_Obj seqs[16];
    CL_Obj call_args[16];
    int32_t i, idx = 0, result_len;
    int j;

    if (n_seqs > 16) n_seqs = 16;
    for (j = 0; j < n_seqs; j++)
        seqs[j] = args[j + 2];

    result_len = seq_length(result_seq);

    /* Precompute source sequence lengths for non-list types */
    {
        int32_t src_lens[16];
        for (j = 0; j < n_seqs; j++) {
            if (CL_CONS_P(seqs[j]) || CL_NULL_P(seqs[j]))
                src_lens[j] = -1; /* list — track via CL_NULL_P */
            else
                src_lens[j] = seq_length(seqs[j]);
        }

    for (idx = 0; idx < result_len; idx++) {
        CL_Obj val;
        /* Check if any source sequence is exhausted */
        for (j = 0; j < n_seqs; j++) {
            if (src_lens[j] >= 0) {
                if (idx >= src_lens[j]) goto map_into_done;
            } else {
                if (CL_NULL_P(seqs[j])) goto map_into_done;
            }
        }
        /* Gather arguments */
        for (j = 0; j < n_seqs; j++) {
            if (src_lens[j] < 0) {
                call_args[j] = cl_car(seqs[j]);
                seqs[j] = cl_cdr(seqs[j]);
            } else {
                call_args[j] = seq_elt(seqs[j], idx);
            }
        }
        /* Call function */
        if (n_seqs == 0) {
            /* CL spec: call with 0 arguments when no source sequences */
            if (CL_FUNCTION_P(func)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
                val = f->func(call_args, 0);
            } else {
                val = cl_vm_apply(func, call_args, 0);
            }
        } else {
            if (CL_FUNCTION_P(func)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
                val = f->func(call_args, n_seqs);
            } else {
                val = cl_vm_apply(func, call_args, n_seqs);
            }
        }
        /* Store into result */
        if (CL_VECTOR_P(result_seq)) {
            cl_vector_data((CL_Vector *)CL_OBJ_TO_PTR(result_seq))[idx] = val;
        } else if (CL_ANY_STRING_P(result_seq)) {
            if (CL_CHAR_P(val)) cl_string_set_char_at(result_seq, (uint32_t)idx, CL_CHAR_VAL(val));
        }
        /* For list result, handled below */
    }
    } /* end src_lens block */

map_into_done:
    /* For list: re-traverse and store */
    if (CL_CONS_P(result_seq)) {
        /* Re-run to fill list (simpler approach) */
        CL_Obj cur = result_seq;
        /* Reset source seqs */
        for (j = 0; j < n_seqs; j++)
            seqs[j] = args[j + 2];
        for (i = 0; i < idx && !CL_NULL_P(cur); i++) {
            CL_Obj val;
            for (j = 0; j < n_seqs; j++) {
                if (CL_CONS_P(seqs[j])) {
                    call_args[j] = cl_car(seqs[j]);
                    seqs[j] = cl_cdr(seqs[j]);
                } else {
                    call_args[j] = seq_elt(seqs[j], i);
                }
            }
            if (CL_FUNCTION_P(func)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
                val = f->func(call_args, n_seqs);
            } else {
                val = cl_vm_apply(func, call_args, n_seqs);
            }
            ((CL_Cons *)CL_OBJ_TO_PTR(cur))->car = val;
            cur = cl_cdr(cur);
        }
    }

    return result_seq;
}

/* ======================================================= */
/* Registration                                            */
/* ======================================================= */

void cl_builtins_sequence_init(void)
{
    /* Pre-intern keyword symbols */
    KW_TEST          = cl_intern_keyword("TEST", 4);
    KW_TEST_NOT      = cl_intern_keyword("TEST-NOT", 8);
    KW_KEY           = cl_intern_keyword("KEY", 3);
    KW_START         = cl_intern_keyword("START", 5);
    KW_END           = cl_intern_keyword("END", 3);
    KW_COUNT         = cl_intern_keyword("COUNT", 5);
    KW_FROM_END      = cl_intern_keyword("FROM-END", 8);
    KW_INITIAL_VALUE = cl_intern_keyword("INITIAL-VALUE", 13);
    KW_START1        = cl_intern_keyword("START1", 6);
    KW_END1          = cl_intern_keyword("END1", 4);
    KW_START2        = cl_intern_keyword("START2", 6);
    KW_END2          = cl_intern_keyword("END2", 4);

    /* Cache eql function */
    {
        CL_Obj eql_sym = cl_intern_in("EQL", 3, cl_package_cl);
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(eql_sym);
        SYM_EQL_FN = s->function;
    }

    /* Find family */
    defun("FIND", bi_find, 2, -1);
    defun("FIND-IF", bi_find_if, 2, -1);
    defun("FIND-IF-NOT", bi_find_if_not, 2, -1);

    /* Position family */
    defun("POSITION", bi_position, 2, -1);
    defun("POSITION-IF", bi_position_if, 2, -1);
    defun("POSITION-IF-NOT", bi_position_if_not, 2, -1);

    /* Count family */
    defun("COUNT", bi_count, 2, -1);
    defun("COUNT-IF", bi_count_if, 2, -1);
    defun("COUNT-IF-NOT", bi_count_if_not, 2, -1);

    /* Remove family */
    defun("REMOVE", bi_remove, 2, -1);
    defun("REMOVE-IF", bi_remove_if, 2, -1);
    defun("REMOVE-IF-NOT", bi_remove_if_not, 2, -1);
    defun("REMOVE-DUPLICATES", bi_remove_duplicates, 1, -1);
    defun("DELETE-DUPLICATES", bi_remove_duplicates, 1, -1);

    /* Substitute family */
    defun("SUBSTITUTE", bi_substitute, 3, -1);
    defun("SUBSTITUTE-IF", bi_substitute_if, 3, -1);
    defun("SUBSTITUTE-IF-NOT", bi_substitute_if_not, 3, -1);
    defun("NSUBSTITUTE", bi_nsubstitute, 3, -1);
    defun("NSUBSTITUTE-IF", bi_nsubstitute_if, 3, -1);
    defun("NSUBSTITUTE-IF-NOT", bi_nsubstitute_if_not, 3, -1);

    /* Reduce */
    defun("REDUCE", bi_reduce, 2, -1);

    /* Fill, Replace */
    defun("FILL", bi_fill, 2, -1);
    defun("REPLACE", bi_replace, 2, -1);

    /* Phase 8 Step 3 */
    defun("ELT", bi_elt, 2, 2);
    cl_register_builtin("%SETF-ELT", bi_setf_elt, 3, 3, cl_package_clamiga);
    defun("COPY-SEQ", bi_copy_seq, 1, 1);
    defun("MAP-INTO", bi_map_into, 2, -1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&KW_TEST);
    cl_gc_register_root(&KW_TEST_NOT);
    cl_gc_register_root(&KW_KEY);
    cl_gc_register_root(&KW_START);
    cl_gc_register_root(&KW_END);
    cl_gc_register_root(&KW_COUNT);
    cl_gc_register_root(&KW_FROM_END);
    cl_gc_register_root(&KW_INITIAL_VALUE);
    cl_gc_register_root(&KW_START1);
    cl_gc_register_root(&KW_END1);
    cl_gc_register_root(&KW_START2);
    cl_gc_register_root(&KW_END2);
    cl_gc_register_root(&SYM_EQL_FN);
}
