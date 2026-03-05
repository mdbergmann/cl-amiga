#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "printer.h"
#include "vm.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Helper to register a builtin */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    s->value = fn;
}

/* --- List utility helpers --- */

/* Call a 2-arg test function (default eql) */
static CL_Obj call_test(CL_Obj test_fn, CL_Obj a, CL_Obj b)
{
    CL_Obj targs[2];
    targs[0] = a;
    targs[1] = b;
    if (CL_FUNCTION_P(test_fn)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(test_fn);
        return f->func(targs, 2);
    }
    if (CL_BYTECODE_P(test_fn) || CL_CLOSURE_P(test_fn))
        return cl_vm_apply(test_fn, targs, 2);
    cl_error(CL_ERR_TYPE, "not a function (test)");
    return CL_NIL;
}

/* Call a 1-arg predicate function */
static CL_Obj call_pred(CL_Obj pred_fn, CL_Obj item)
{
    CL_Obj pargs[1];
    pargs[0] = item;
    if (CL_FUNCTION_P(pred_fn)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(pred_fn);
        return f->func(pargs, 1);
    }
    if (CL_BYTECODE_P(pred_fn) || CL_CLOSURE_P(pred_fn))
        return cl_vm_apply(pred_fn, pargs, 1);
    cl_error(CL_ERR_TYPE, "not a function (predicate)");
    return CL_NIL;
}

/* Extract :test keyword argument from args[start..n-1], return default (eql) if absent */
static CL_Obj extract_test_arg(CL_Obj *args, int n, int start)
{
    int i;
    CL_Obj kw_test = cl_intern_in("TEST", 4, cl_package_keyword);
    for (i = start; i < n - 1; i += 2) {
        if (args[i] == kw_test)
            return cl_coerce_funcdesig(args[i + 1], ":TEST");
    }
    /* Default: eql */
    {
        CL_Obj eql_sym = cl_intern_in("EQL", 3, cl_package_cl);
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(eql_sym);
        return s->function;
    }
}

/* --- List utilities --- */

static CL_Obj bi_nthcdr(CL_Obj *args, int n)
{
    int32_t idx;
    CL_Obj list;
    CL_UNUSED(n);
    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "NTHCDR: index must be a number");
    idx = CL_FIXNUM_VAL(args[0]);
    list = args[1];
    while (idx > 0 && !CL_NULL_P(list)) {
        list = cl_cdr(list);
        idx--;
    }
    return list;
}

static CL_Obj bi_last(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    int32_t count = 1;
    int32_t len = 0;
    CL_Obj p;

    if (n >= 2) {
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "LAST: n must be a number");
        count = CL_FIXNUM_VAL(args[1]);
    }
    if (CL_NULL_P(list)) return CL_NIL;

    /* Count length */
    for (p = list; CL_CONS_P(p); p = cl_cdr(p))
        len++;

    /* Skip (len - count) conses */
    {
        int32_t skip = len - count;
        if (skip < 0) skip = 0;
        while (skip > 0) {
            list = cl_cdr(list);
            skip--;
        }
    }
    return list;
}

static CL_Obj bi_acons(CL_Obj *args, int n)
{
    CL_Obj pair;
    CL_UNUSED(n);
    CL_GC_PROTECT(args[2]);
    pair = cl_cons(args[0], args[1]);
    CL_GC_UNPROTECT(1);
    return cl_cons(pair, args[2]);
}

static CL_Obj bi_copy_list(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    CL_UNUSED(n);

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    while (CL_CONS_P(list)) {
        CL_Obj cell = cl_cons(cl_car(list), CL_NIL);
        if (CL_NULL_P(result)) {
            result = cell;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
        }
        tail = cell;
        list = cl_cdr(list);
    }
    /* Handle dotted list */
    if (!CL_NULL_P(list) && !CL_NULL_P(tail)) {
        ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = list;
    }

    CL_GC_UNPROTECT(2);
    return result;
}

static CL_Obj bi_pairlis(CL_Obj *args, int n)
{
    CL_Obj keys = args[0], data = args[1];
    CL_Obj result = (n >= 3) ? args[2] : CL_NIL;
    CL_Obj pairs = CL_NIL, ptail = CL_NIL;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(pairs);
    CL_GC_PROTECT(ptail);

    while (!CL_NULL_P(keys) && !CL_NULL_P(data)) {
        CL_Obj pair = cl_cons(cl_car(keys), cl_car(data));
        CL_Obj cell = cl_cons(pair, CL_NIL);
        if (CL_NULL_P(pairs)) {
            pairs = cell;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(ptail))->cdr = cell;
        }
        ptail = cell;
        keys = cl_cdr(keys);
        data = cl_cdr(data);
    }

    /* Append existing alist */
    if (!CL_NULL_P(ptail)) {
        ((CL_Cons *)CL_OBJ_TO_PTR(ptail))->cdr = result;
        result = pairs;
    }

    CL_GC_UNPROTECT(3);
    return result;
}

/* --- Functions with :test keyword --- */

static CL_Obj bi_assoc(CL_Obj *args, int n)
{
    CL_Obj key = args[0], alist = args[1];
    CL_Obj test_fn = extract_test_arg(args, n, 2);

    while (!CL_NULL_P(alist)) {
        CL_Obj pair = cl_car(alist);
        if (CL_CONS_P(pair)) {
            if (!CL_NULL_P(call_test(test_fn, key, cl_car(pair))))
                return pair;
        }
        alist = cl_cdr(alist);
    }
    return CL_NIL;
}

static CL_Obj bi_rassoc(CL_Obj *args, int n)
{
    CL_Obj item = args[0], alist = args[1];
    CL_Obj test_fn = extract_test_arg(args, n, 2);

    while (!CL_NULL_P(alist)) {
        CL_Obj pair = cl_car(alist);
        if (CL_CONS_P(pair)) {
            if (!CL_NULL_P(call_test(test_fn, item, cl_cdr(pair))))
                return pair;
        }
        alist = cl_cdr(alist);
    }
    return CL_NIL;
}

static CL_Obj bi_getf(CL_Obj *args, int n)
{
    CL_Obj plist = args[0], indicator = args[1];
    CL_Obj def = (n >= 3) ? args[2] : CL_NIL;

    while (!CL_NULL_P(plist)) {
        CL_Obj key = cl_car(plist);
        plist = cl_cdr(plist);
        if (CL_NULL_P(plist)) break;
        if (key == indicator)
            return cl_car(plist);
        plist = cl_cdr(plist);
    }
    return def;
}

/* %SETF-GETF: (setf (getf plist indicator) value)
 * Destructively modifies the value cell for INDICATOR in PLIST.
 * If not found, prepends indicator/value to the front and returns new plist.
 * Returns the value. */
static CL_Obj bi_setf_getf(CL_Obj *args, int n)
{
    CL_Obj plist = args[0], indicator = args[1], value = args[2];
    CL_Obj p = plist;
    CL_UNUSED(n);
    while (!CL_NULL_P(p)) {
        CL_Obj key = cl_car(p);
        CL_Obj val_cell = cl_cdr(p);
        if (CL_NULL_P(val_cell)) break;
        if (key == indicator) {
            /* Found — destructively update the value cell */
            ((CL_Cons *)CL_OBJ_TO_PTR(val_cell))->car = value;
            return value;
        }
        p = cl_cdr(val_cell);
    }
    /* Not found — this case can't update the caller's variable,
     * but we return value anyway. The caller should handle this. */
    return value;
}

static CL_Obj bi_subst(CL_Obj *args, int n)
{
    CL_Obj new_obj = args[0], old_obj = args[1], tree = args[2];
    CL_Obj test_fn = extract_test_arg(args, n, 3);
    CL_Obj car_r, cdr_r;

    if (!CL_NULL_P(call_test(test_fn, old_obj, tree)))
        return new_obj;
    if (!CL_CONS_P(tree))
        return tree;

    CL_GC_PROTECT(new_obj);
    CL_GC_PROTECT(old_obj);
    CL_GC_PROTECT(test_fn);

    {
        CL_Obj sub_args[5];
        sub_args[0] = new_obj;
        sub_args[1] = old_obj;
        sub_args[2] = cl_car(tree);
        /* Pass :test through */
        if (n > 3) { sub_args[3] = args[3]; sub_args[4] = args[4]; }
        car_r = bi_subst(sub_args, n);
    }
    CL_GC_PROTECT(car_r);
    {
        CL_Obj sub_args[5];
        sub_args[0] = new_obj;
        sub_args[1] = old_obj;
        sub_args[2] = cl_cdr(tree);
        if (n > 3) { sub_args[3] = args[3]; sub_args[4] = args[4]; }
        cdr_r = bi_subst(sub_args, n);
    }
    CL_GC_UNPROTECT(4);

    if (car_r == cl_car(tree) && cdr_r == cl_cdr(tree))
        return tree;
    return cl_cons(car_r, cdr_r);
}

static CL_Obj bi_sublis(CL_Obj *args, int n)
{
    CL_Obj alist = args[0], tree = args[1];
    CL_Obj test_fn = extract_test_arg(args, n, 2);
    CL_Obj pair, car_r, cdr_r;

    /* Check each alist entry against tree */
    {
        CL_Obj al = alist;
        while (!CL_NULL_P(al)) {
            pair = cl_car(al);
            if (CL_CONS_P(pair)) {
                if (!CL_NULL_P(call_test(test_fn, cl_car(pair), tree)))
                    return cl_cdr(pair);
            }
            al = cl_cdr(al);
        }
    }

    if (!CL_CONS_P(tree))
        return tree;

    CL_GC_PROTECT(alist);
    CL_GC_PROTECT(test_fn);

    {
        CL_Obj sub_args[4];
        sub_args[0] = alist;
        sub_args[1] = cl_car(tree);
        if (n > 2) { sub_args[2] = args[2]; sub_args[3] = args[3]; }
        car_r = bi_sublis(sub_args, n);
    }
    CL_GC_PROTECT(car_r);
    {
        CL_Obj sub_args[4];
        sub_args[0] = alist;
        sub_args[1] = cl_cdr(tree);
        if (n > 2) { sub_args[2] = args[2]; sub_args[3] = args[3]; }
        cdr_r = bi_sublis(sub_args, n);
    }
    CL_GC_UNPROTECT(3);

    if (car_r == cl_car(tree) && cdr_r == cl_cdr(tree))
        return tree;
    return cl_cons(car_r, cdr_r);
}

static CL_Obj bi_adjoin(CL_Obj *args, int n)
{
    CL_Obj item = args[0], list = args[1];
    CL_Obj test_fn = extract_test_arg(args, n, 2);
    CL_Obj l;

    /* Check if item is already a member */
    for (l = list; !CL_NULL_P(l); l = cl_cdr(l)) {
        if (!CL_NULL_P(call_test(test_fn, item, cl_car(l))))
            return list;
    }
    return cl_cons(item, list);
}

/* --- Destructive operations --- */

static CL_Obj bi_nconc(CL_Obj *args, int n)
{
    CL_Obj result = CL_NIL;
    CL_Obj tail = CL_NIL;
    int i;

    for (i = 0; i < n; i++) {
        CL_Obj list = args[i];
        if (CL_NULL_P(list)) continue;

        if (CL_NULL_P(result)) {
            result = list;
        } else {
            ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = list;
        }

        /* Walk to end of this list */
        while (CL_CONS_P(list) && CL_CONS_P(cl_cdr(list)))
            list = cl_cdr(list);
        tail = list;
    }
    return result;
}

static CL_Obj bi_nreverse(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    CL_Obj prev = CL_NIL;
    CL_UNUSED(n);

    while (CL_CONS_P(list)) {
        CL_Obj next = cl_cdr(list);
        ((CL_Cons *)CL_OBJ_TO_PTR(list))->cdr = prev;
        prev = list;
        list = next;
    }
    return prev;
}

static CL_Obj bi_delete(CL_Obj *args, int n)
{
    CL_Obj item = args[0], list = args[1];
    CL_Obj test_fn = extract_test_arg(args, n, 2);
    CL_Obj prev = CL_NIL, curr = list;

    while (!CL_NULL_P(curr)) {
        if (!CL_NULL_P(call_test(test_fn, item, cl_car(curr)))) {
            if (CL_NULL_P(prev)) {
                list = cl_cdr(curr);
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = cl_cdr(curr);
            }
            curr = cl_cdr(curr);
        } else {
            prev = curr;
            curr = cl_cdr(curr);
        }
    }
    return list;
}

static CL_Obj bi_delete_if(CL_Obj *args, int n)
{
    CL_Obj pred = args[0], list = args[1];
    CL_Obj prev = CL_NIL, curr = list;
    CL_UNUSED(n);

    while (!CL_NULL_P(curr)) {
        if (!CL_NULL_P(call_pred(pred, cl_car(curr)))) {
            if (CL_NULL_P(prev)) {
                list = cl_cdr(curr);
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = cl_cdr(curr);
            }
            curr = cl_cdr(curr);
        } else {
            prev = curr;
            curr = cl_cdr(curr);
        }
    }
    return list;
}

static CL_Obj bi_nsubst(CL_Obj *args, int n)
{
    CL_Obj new_obj = args[0], old_obj = args[1], tree = args[2];
    CL_Obj test_fn = extract_test_arg(args, n, 3);

    if (!CL_NULL_P(call_test(test_fn, old_obj, tree)))
        return new_obj;
    if (!CL_CONS_P(tree))
        return tree;

    {
        CL_Obj sub_args[5];
        CL_Obj car_r;
        sub_args[0] = new_obj;
        sub_args[1] = old_obj;
        sub_args[2] = cl_car(tree);
        if (n > 3) { sub_args[3] = args[3]; sub_args[4] = args[4]; }
        car_r = bi_nsubst(sub_args, n);
        ((CL_Cons *)CL_OBJ_TO_PTR(tree))->car = car_r;
    }
    {
        CL_Obj sub_args[5];
        CL_Obj cdr_r;
        sub_args[0] = new_obj;
        sub_args[1] = old_obj;
        sub_args[2] = cl_cdr(tree);
        if (n > 3) { sub_args[3] = args[3]; sub_args[4] = args[4]; }
        cdr_r = bi_nsubst(sub_args, n);
        ((CL_Cons *)CL_OBJ_TO_PTR(tree))->cdr = cdr_r;
    }
    return tree;
}

/* --- Copy/construction --- */

static CL_Obj bi_butlast(CL_Obj *args, int n)
{
    CL_Obj list = args[0];
    int32_t count = 1;
    int32_t len = 0;
    CL_Obj p, result = CL_NIL, tail = CL_NIL;

    if (n >= 2) {
        if (!CL_FIXNUM_P(args[1]))
            cl_error(CL_ERR_TYPE, "BUTLAST: n must be a number");
        count = CL_FIXNUM_VAL(args[1]);
    }
    if (CL_NULL_P(list)) return CL_NIL;

    for (p = list; CL_CONS_P(p); p = cl_cdr(p))
        len++;

    {
        int32_t keep = len - count;
        if (keep <= 0) return CL_NIL;

        CL_GC_PROTECT(result);
        CL_GC_PROTECT(tail);

        p = list;
        while (keep > 0) {
            CL_Obj cell = cl_cons(cl_car(p), CL_NIL);
            if (CL_NULL_P(result)) {
                result = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            }
            tail = cell;
            p = cl_cdr(p);
            keep--;
        }

        CL_GC_UNPROTECT(2);
    }
    return result;
}

static CL_Obj bi_copy_tree(CL_Obj *args, int n)
{
    CL_Obj tree = args[0];
    CL_Obj car_r, cdr_r;
    CL_UNUSED(n);

    if (!CL_CONS_P(tree))
        return tree;

    CL_GC_PROTECT(tree);
    {
        CL_Obj sub[1];
        sub[0] = cl_car(tree);
        car_r = bi_copy_tree(sub, 1);
    }
    CL_GC_PROTECT(car_r);
    {
        CL_Obj sub[1];
        sub[0] = cl_cdr(tree);
        cdr_r = bi_copy_tree(sub, 1);
    }
    CL_GC_UNPROTECT(2);
    return cl_cons(car_r, cdr_r);
}

/* --- Mapping variants --- */

/* Helper: call a function on args (same as mapcar dispatch) */
static CL_Obj call_func(CL_Obj func, CL_Obj *call_args, int nargs)
{
    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        return f->func(call_args, nargs);
    }
    if (CL_BYTECODE_P(func) || CL_CLOSURE_P(func))
        return cl_vm_apply(func, call_args, nargs);
    cl_error(CL_ERR_TYPE, "not a function");
    return CL_NIL;
}

static CL_Obj bi_mapc(CL_Obj *args, int n)
{
    CL_Obj func = cl_coerce_funcdesig(args[0], "MAPC");
    CL_Obj first_list = args[1];
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    for (;;) {
        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) return first_list;
        }
        for (i = 0; i < nlists; i++) {
            call_args[i] = cl_car(lists[i]);
            lists[i] = cl_cdr(lists[i]);
        }
        call_func(func, call_args, nlists);
    }
}

static CL_Obj bi_mapcan(CL_Obj *args, int n)
{
    CL_Obj func = cl_coerce_funcdesig(args[0], "MAPCAN");
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        CL_Obj val;

        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) goto mapcan_done;
        }
        for (i = 0; i < nlists; i++) {
            call_args[i] = cl_car(lists[i]);
            lists[i] = cl_cdr(lists[i]);
        }

        val = call_func(func, call_args, nlists);

        /* nconc val onto result */
        if (!CL_NULL_P(val)) {
            if (CL_NULL_P(result)) {
                result = val;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = val;
            }
            /* Walk to end of val */
            while (CL_CONS_P(val) && CL_CONS_P(cl_cdr(val)))
                val = cl_cdr(val);
            tail = val;
        }
    }

mapcan_done:
    CL_GC_UNPROTECT(3);
    return result;
}

static CL_Obj bi_maplist(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        CL_Obj val;

        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) goto maplist_done;
        }

        /* Pass the cdrs (sublists), not cars */
        for (i = 0; i < nlists; i++)
            call_args[i] = lists[i];

        val = call_func(func, call_args, nlists);

        {
            CL_Obj cell = cl_cons(val, CL_NIL);
            if (CL_NULL_P(result)) {
                result = cell;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
            }
            tail = cell;
        }

        for (i = 0; i < nlists; i++)
            lists[i] = cl_cdr(lists[i]);
    }

maplist_done:
    CL_GC_UNPROTECT(3);
    return result;
}

static CL_Obj bi_mapl(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    CL_Obj first_list = args[1];
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    for (;;) {
        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) return first_list;
        }
        for (i = 0; i < nlists; i++)
            call_args[i] = lists[i];

        call_func(func, call_args, nlists);

        for (i = 0; i < nlists; i++)
            lists[i] = cl_cdr(lists[i]);
    }
}

static CL_Obj bi_mapcon(CL_Obj *args, int n)
{
    CL_Obj func = args[0];
    int nlists = n - 1;
    CL_Obj lists[16];
    CL_Obj call_args[16];
    CL_Obj result = CL_NIL, tail = CL_NIL;
    int i;

    if (nlists > 16) nlists = 16;
    for (i = 0; i < nlists; i++)
        lists[i] = args[i + 1];

    CL_GC_PROTECT(func);
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(tail);

    for (;;) {
        CL_Obj val;

        for (i = 0; i < nlists; i++) {
            if (CL_NULL_P(lists[i])) goto mapcon_done;
        }

        for (i = 0; i < nlists; i++)
            call_args[i] = lists[i];

        val = call_func(func, call_args, nlists);

        /* nconc val onto result */
        if (!CL_NULL_P(val)) {
            if (CL_NULL_P(result)) {
                result = val;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = val;
            }
            while (CL_CONS_P(val) && CL_CONS_P(cl_cdr(val)))
                val = cl_cdr(val);
            tail = val;
        }

        for (i = 0; i < nlists; i++)
            lists[i] = cl_cdr(lists[i]);
    }

mapcon_done:
    CL_GC_UNPROTECT(3);
    return result;
}

/* --- list*, make-list, remf (Phase 8 Step 1) --- */

static CL_Obj bi_list_star(CL_Obj *args, int n)
{
    CL_Obj result;
    int i;
    if (n == 0)
        cl_error(CL_ERR_ARGS, "LIST*: at least 1 argument required");
    if (n == 1)
        return args[0];
    /* Last arg becomes the final cdr */
    result = args[n - 1];
    CL_GC_PROTECT(result);
    for (i = n - 2; i >= 0; i--)
        result = cl_cons(args[i], result);
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_make_list(CL_Obj *args, int n)
{
    int32_t count;
    CL_Obj init_elem = CL_NIL;
    CL_Obj result = CL_NIL;
    int i;
    CL_Obj kw_init;

    if (!CL_FIXNUM_P(args[0]))
        cl_error(CL_ERR_TYPE, "MAKE-LIST: size must be a fixnum");
    count = CL_FIXNUM_VAL(args[0]);
    if (count < 0)
        cl_error(CL_ERR_ARGS, "MAKE-LIST: size must be non-negative");

    /* Parse :initial-element keyword */
    kw_init = cl_intern_in("INITIAL-ELEMENT", 15, cl_package_keyword);
    for (i = 1; i < n - 1; i += 2) {
        if (args[i] == kw_init)
            init_elem = args[i + 1];
    }

    CL_GC_PROTECT(result);
    for (i = 0; i < count; i++)
        result = cl_cons(init_elem, result);
    CL_GC_UNPROTECT(1);
    return result;
}

static CL_Obj bi_remf(CL_Obj *args, int n)
{
    /* (remf place indicator) — destructive plist removal
       Returns the modified plist. Second value: T if found. */
    CL_Obj plist = args[0], indicator = args[1];
    CL_Obj prev = CL_NIL, curr = plist;
    CL_UNUSED(n);

    while (!CL_NULL_P(curr)) {
        CL_Obj key = cl_car(curr);
        CL_Obj next = cl_cdr(curr);
        if (CL_NULL_P(next)) break;
        if (key == indicator) {
            CL_Obj rest = cl_cdr(next);
            if (CL_NULL_P(prev)) {
                /* Removing from head */
                cl_mv_count = 2;
                cl_mv_values[0] = rest;
                cl_mv_values[1] = SYM_T;
                return rest;
            } else {
                ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = rest;
                cl_mv_count = 2;
                cl_mv_values[0] = plist;
                cl_mv_values[1] = SYM_T;
                return plist;
            }
        }
        prev = next;
        curr = cl_cdr(next);
    }
    cl_mv_count = 2;
    cl_mv_values[0] = plist;
    cl_mv_values[1] = CL_NIL;
    return plist;
}

/* --- Registration --- */

void cl_builtins_lists_init(void)
{
    /* List utilities */
    defun("NTHCDR", bi_nthcdr, 2, 2);
    defun("LAST", bi_last, 1, 2);
    defun("ACONS", bi_acons, 3, 3);
    defun("COPY-LIST", bi_copy_list, 1, 1);
    defun("PAIRLIS", bi_pairlis, 2, 3);
    defun("ASSOC", bi_assoc, 2, -1);
    defun("RASSOC", bi_rassoc, 2, -1);
    defun("GETF", bi_getf, 2, 3);
    defun("%SETF-GETF", bi_setf_getf, 3, 3);
    defun("SUBST", bi_subst, 3, -1);
    defun("SUBLIS", bi_sublis, 2, -1);
    defun("ADJOIN", bi_adjoin, 2, -1);

    /* Destructive list ops */
    defun("NCONC", bi_nconc, 0, -1);
    defun("NREVERSE", bi_nreverse, 1, 1);
    defun("DELETE", bi_delete, 2, -1);
    defun("DELETE-IF", bi_delete_if, 2, 2);
    defun("NSUBST", bi_nsubst, 3, -1);

    /* Copy/construction */
    defun("BUTLAST", bi_butlast, 1, 2);
    defun("COPY-TREE", bi_copy_tree, 1, 1);

    /* Phase 8 Step 1 */
    defun("LIST*", bi_list_star, 1, -1);
    defun("MAKE-LIST", bi_make_list, 1, -1);
    defun("REMF", bi_remf, 2, 2);

    /* Mapping variants */
    defun("MAPC", bi_mapc, 2, -1);
    defun("MAPCAN", bi_mapcan, 2, -1);
    defun("MAPLIST", bi_maplist, 2, -1);
    defun("MAPL", bi_mapl, 2, -1);
    defun("MAPCON", bi_mapcon, 2, -1);
}
