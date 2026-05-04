/*
 * compiler_extra.c — Conditionals, case, quasiquote, MV, defs
 *
 * Split from compiler.c. Contains: and, or, cond, quasiquote,
 * case, typecase, multiple-value-*, eval-when, defsetf,
 * defvar, defparameter, defun, defmacro.
 */

#include "compiler_internal.h"
#include "../platform/platform_thread.h"
#include <stdio.h>

/* Maximum number of clauses/arguments in AND/OR/COND/CASE/TYPECASE.
 * Each clause needs one patch entry (jump offset to fix up later).
 * Stack usage: MAX_PATCHES * sizeof(int) = 2KB per array. */
#define MAX_PATCHES 512

/* --- And / Or --- */

void compile_and(CL_Compiler *c, CL_Obj form)
{
    CL_Obj args = cl_cdr(form);
    int saved_tail = c->in_tail;
    int nil_patches[MAX_PATCHES];
    int n_patches = 0;

    if (CL_NULL_P(args)) {
        /* (and) => T */
        cl_emit(c, OP_T);
        return;
    }

    if (CL_NULL_P(cl_cdr(args))) {
        /* (and x) => just compile x */
        compile_expr(c, cl_car(args));
        return;
    }

    /* Multiple args: short-circuit chain */
    CL_GC_PROTECT(args);
    while (!CL_NULL_P(args)) {
        int is_last = CL_NULL_P(cl_cdr(args));
        if (is_last) {
            c->in_tail = saved_tail;
            compile_expr(c, cl_car(args));
        } else {
            c->in_tail = 0;
            compile_expr(c, cl_car(args));
            cl_emit(c, OP_DUP);
            if (n_patches >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "AND: too many arguments (max %d)", MAX_PATCHES);
            nil_patches[n_patches++] = cl_emit_jump(c, OP_JNIL);
            cl_emit(c, OP_POP);
            cl_emit(c, OP_MV_RESET);
        }
        args = cl_cdr(args);
    }
    CL_GC_UNPROTECT(1);

    /* Patch all nil-jumps to here (done label) */
    {
        int i;
        for (i = 0; i < n_patches; i++)
            cl_patch_jump(c, nil_patches[i]);
    }
}

void compile_or(CL_Compiler *c, CL_Obj form)
{
    CL_Obj args = cl_cdr(form);
    int saved_tail = c->in_tail;
    int true_patches[MAX_PATCHES];
    int n_patches = 0;

    if (CL_NULL_P(args)) {
        /* (or) => NIL */
        cl_emit(c, OP_NIL);
        return;
    }

    if (CL_NULL_P(cl_cdr(args))) {
        /* (or x) => just compile x */
        compile_expr(c, cl_car(args));
        return;
    }

    /* Multiple args: short-circuit chain */
    CL_GC_PROTECT(args);
    while (!CL_NULL_P(args)) {
        int is_last = CL_NULL_P(cl_cdr(args));
        if (is_last) {
            c->in_tail = saved_tail;
            compile_expr(c, cl_car(args));
        } else {
            c->in_tail = 0;
            compile_expr(c, cl_car(args));
            cl_emit(c, OP_DUP);
            if (n_patches >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "OR: too many arguments (max %d)", MAX_PATCHES);
            true_patches[n_patches++] = cl_emit_jump(c, OP_JTRUE);
            cl_emit(c, OP_POP);
            cl_emit(c, OP_MV_RESET);
        }
        args = cl_cdr(args);
    }
    CL_GC_UNPROTECT(1);

    /* Patch all true-jumps to here (done label) */
    {
        int i;
        for (i = 0; i < n_patches; i++)
            cl_patch_jump(c, true_patches[i]);
    }
}

/* --- Cond --- */

void compile_cond(CL_Compiler *c, CL_Obj form)
{
    CL_Obj clauses = cl_cdr(form);
    int saved_tail = c->in_tail;
    int done_patches[MAX_PATCHES];
    int n_done = 0;

    if (CL_NULL_P(clauses)) {
        cl_emit(c, OP_NIL);
        return;
    }

    CL_GC_PROTECT(clauses);
    while (!CL_NULL_P(clauses)) {
        CL_Obj clause = cl_car(clauses);
        CL_Obj test = cl_car(clause);
        CL_Obj body = cl_cdr(clause);
        int is_last = CL_NULL_P(cl_cdr(clauses));

        if (test == SYM_T && is_last) {
            /* (t body...) — default clause, no test needed */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                cl_emit(c, OP_T);
            else
                compile_progn(c, body);
        } else {
            int jnil_pos;
            /* Compile test */
            c->in_tail = 0;
            compile_expr(c, test);

            if (CL_NULL_P(body)) {
                /* (cond (test)) with no body — return the test value itself.
                 * DUP before JNIL so value stays on stack if test is true. */
                cl_emit(c, OP_DUP);
                jnil_pos = cl_emit_jump(c, OP_JNIL);
            } else {
                jnil_pos = cl_emit_jump(c, OP_JNIL);

                /* Compile body */
                c->in_tail = saved_tail;
                compile_progn(c, body);
            }

            /* Always JMP past NIL fallthrough (even last non-t clause) */
            if (n_done >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "COND: too many clauses (max %d)", MAX_PATCHES);
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            cl_patch_jump(c, jnil_pos);
            if (CL_NULL_P(body)) {
                /* JNIL popped the DUP'd value (was NIL), pop the original too */
                cl_emit(c, OP_POP);
            }
        }

        clauses = cl_cdr(clauses);
    }
    CL_GC_UNPROTECT(1);

    /* If the last clause wasn't a t-default, we need a NIL fallthrough */
    {
        CL_Obj last_clause;
        CL_Obj c2 = cl_cdr(form);
        /* Walk to last clause */
        while (!CL_NULL_P(cl_cdr(c2))) c2 = cl_cdr(c2);
        last_clause = cl_car(c2);
        if (cl_car(last_clause) != SYM_T)
            cl_emit(c, OP_NIL);
    }

    /* Patch all done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            cl_patch_jump(c, done_patches[i]);
    }
}

/* --- Quasiquote --- */

/* Expand a quasiquote template into LIST/CONS/APPEND forms.
 * Uses depth tracking for correct nested backquote handling.
 *
 * At depth 0: UNQUOTE evaluates, UNQUOTE-SPLICING splices.
 * At depth > 0: all QQ markers are reconstructed as data.
 * QUASIQUOTE increments depth, UNQUOTE/UNQUOTE-SPLICING decrement depth.
 */

static CL_Obj qq_expand(CL_Obj x, int depth);
static CL_Obj qq_expand_list(CL_Obj x, int depth);

/* Build (QUOTE x) */
static CL_Obj qq_quote(CL_Obj x)
{
    return cl_cons(SYM_QUOTE, cl_cons(x, CL_NIL));
}

/* Simplify (APPEND ...) forms */
static CL_Obj qq_append(CL_Obj args)
{
    CL_Obj sym_append = cl_intern_in("APPEND", 6, cl_package_cl);
    if (CL_NULL_P(args)) return CL_NIL;
    if (CL_NULL_P(cl_cdr(args))) return cl_car(args);
    return cl_cons(sym_append, args);
}

/* Build (LIST a b c ...) */
static CL_Obj qq_list(CL_Obj items)
{
    CL_Obj sym_list = cl_intern_in("LIST", 4, cl_package_cl);
    return cl_cons(sym_list, items);
}

/* Expand quasiquote template at the given nesting depth.
 * Returns a form that, when evaluated, produces the desired structure. */
static CL_Obj qq_expand(CL_Obj x, int depth)
{
    /* Atom: quote it (symbols need quoting, self-evaluating atoms don't) */
    if (!CL_CONS_P(x)) {
        if (CL_NULL_P(x))
            return CL_NIL;
        if (CL_SYMBOL_P(x))
            return qq_quote(x);
        /* Vector template: convert elements to a list, expand quasiquote
         * on the list, then wrap so it builds a vector at runtime.
         * Form returned: (COERCE <list-form> 'SIMPLE-VECTOR).  Splices
         * inside the vector work for free since the list expansion
         * already handles them. */
        if (CL_VECTOR_P(x)) {
            CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(x);
            uint32_t i, n = cl_vector_active_length(v);
            CL_Obj elems = CL_NIL, list_form;
            CL_Obj *data = cl_vector_data(v);
            CL_GC_PROTECT(elems);
            for (i = n; i > 0; i--)
                elems = cl_cons(data[i - 1], elems);
            list_form = qq_expand(elems, depth);
            CL_GC_UNPROTECT(1);
            {
                CL_Obj sym_coerce = cl_intern_in("COERCE", 6, cl_package_cl);
                CL_Obj sym_sv = cl_intern_in("SIMPLE-VECTOR", 13, cl_package_cl);
                return cl_cons(sym_coerce,
                               cl_cons(list_form,
                                       cl_cons(qq_quote(sym_sv), CL_NIL)));
            }
        }
        /* Self-evaluating: fixnum, char, string, etc. */
        return x;
    }

    {
        CL_Obj head = cl_car(x);

        /* (UNQUOTE expr) */
        if (head == SYM_UNQUOTE) {
            if (depth == 0)
                return cl_car(cl_cdr(x));  /* Evaluate expr */
            /* depth > 0: reconstruct as (LIST 'UNQUOTE (qq-expand expr depth-1)) */
            {
                CL_Obj inner = qq_expand(cl_car(cl_cdr(x)), depth - 1);
                return qq_list(cl_cons(qq_quote(SYM_UNQUOTE), cl_cons(inner, CL_NIL)));
            }
        }

        /* (UNQUOTE-SPLICING expr) */
        if (head == SYM_UNQUOTE_SPLICING) {
            if (depth == 0) {
                /* At top level, this means "evaluate expr for splicing".
                 * Return the expr — caller is responsible for splicing context. */
                return cl_car(cl_cdr(x));
            }
            /* depth > 0: reconstruct */
            {
                CL_Obj inner = qq_expand(cl_car(cl_cdr(x)), depth - 1);
                return qq_list(cl_cons(qq_quote(SYM_UNQUOTE_SPLICING), cl_cons(inner, CL_NIL)));
            }
        }

        /* (QUASIQUOTE expr) — nested backquote */
        if (head == SYM_QUASIQUOTE) {
            CL_Obj inner = qq_expand(cl_car(cl_cdr(x)), depth + 1);
            return qq_list(cl_cons(qq_quote(SYM_QUASIQUOTE), cl_cons(inner, CL_NIL)));
        }
    }

    /* Regular list: expand each element, combine with APPEND */
    {
        CL_Obj result = CL_NIL;
        CL_Obj cursor = x;

        CL_GC_PROTECT(result);

        while (CL_CONS_P(cursor)) {
            CL_Obj elem = cl_car(cursor);
            CL_Obj rest = cl_cdr(cursor);

            /* Detect dotted unquote: (... UNQUOTE expr) where UNQUOTE is bare symbol
             * This comes from reader: `(a b . ,x) → (a b UNQUOTE x)
             * Add tail_expr as the last APPEND segment and break out —
             * the normal APPEND path handles splices correctly. */
            if (depth == 0 && elem == SYM_UNQUOTE &&
                CL_CONS_P(rest) && CL_NULL_P(cl_cdr(rest))) {
                CL_Obj tail_expr = cl_car(rest);
                result = cl_cons(tail_expr, result);
                cursor = CL_NIL;  /* tail already handled */
                break;
            }

            {
                CL_Obj expanded = qq_expand_list(elem, depth);
                result = cl_cons(expanded, result);
            }
            cursor = rest;
        }

        /* Handle dotted tail (non-unquote atom as CDR) — shouldn't normally happen
         * since reader produces proper lists, but handle for safety */
        if (!CL_NULL_P(cursor)) {
            CL_Obj tail_exp = qq_expand(cursor, depth);
            result = cl_cons(tail_exp, result);
        }

        /* Reverse */
        {
            CL_Obj rev = CL_NIL;
            CL_GC_PROTECT(rev);
            while (!CL_NULL_P(result)) {
                rev = cl_cons(cl_car(result), rev);
                result = cl_cdr(result);
            }
            result = rev;
            CL_GC_UNPROTECT(1);
        }

        {
            CL_Obj val = qq_append(result);
            CL_GC_UNPROTECT(1);
            return val;
        }
    }
}

/* Expand a single list element for use in APPEND.
 * Returns a form that evaluates to a LIST (for non-splicing elements)
 * or directly to a list value (for splicing elements). */
static CL_Obj qq_expand_list(CL_Obj x, int depth)
{
    CL_Obj sym_list = cl_intern_in("LIST", 4, cl_package_cl);

    /* Atom: (LIST 'x) */
    if (!CL_CONS_P(x)) {
        if (CL_NULL_P(x))
            return cl_cons(sym_list, cl_cons(CL_NIL, CL_NIL));
        if (CL_SYMBOL_P(x))
            return cl_cons(sym_list, cl_cons(qq_quote(x), CL_NIL));
        return cl_cons(sym_list, cl_cons(x, CL_NIL));
    }

    {
        CL_Obj head = cl_car(x);

        /* (UNQUOTE expr) at depth 0: (LIST expr) */
        if (head == SYM_UNQUOTE && depth == 0)
            return cl_cons(sym_list, cl_cons(cl_car(cl_cdr(x)), CL_NIL));

        /* (UNQUOTE-SPLICING expr) at depth 0: expr (spliced into APPEND) */
        if (head == SYM_UNQUOTE_SPLICING && depth == 0)
            return cl_car(cl_cdr(x));

        /* (UNQUOTE inner) at depth > 0 */
        if (head == SYM_UNQUOTE && depth > 0) {
            CL_Obj inner_form = cl_car(cl_cdr(x));

            /* Special case: (UNQUOTE (UNQUOTE-SPLICING expr)) at depth 1
             * This is ,,@expr — evaluate expr and wrap each element in UNQUOTE.
             * Produces: (MAPCAR (LAMBDA (V) (LIST 'UNQUOTE V)) expr)
             * which is spliced into the APPEND call. */
            if (depth == 1 && CL_CONS_P(inner_form) &&
                cl_car(inner_form) == SYM_UNQUOTE_SPLICING) {
                /* Return expr directly — it gets spliced by APPEND.
                 * But each element needs to be wrapped in (UNQUOTE v).
                 * Build: (MAPCAR (LAMBDA (#:V) (LIST 'UNQUOTE #:V)) expr) */
                CL_Obj expr = cl_car(cl_cdr(inner_form));
                CL_Obj gv, name_str, lambda_form, mapcar_form;
                CL_Obj sym_mapcar = cl_intern_in("MAPCAR", 6, cl_package_cl);
                CL_Obj sym_lambda = cl_intern_in("LAMBDA", 6, cl_package_cl);

                name_str = cl_make_string("%QQV", 4);
                gv = cl_make_symbol(name_str);

                /* (LAMBDA (#:V) (LIST 'UNQUOTE #:V)) */
                lambda_form = cl_cons(sym_lambda,
                    cl_cons(cl_cons(gv, CL_NIL),
                        cl_cons(qq_list(cl_cons(qq_quote(SYM_UNQUOTE),
                                                cl_cons(gv, CL_NIL))),
                                CL_NIL)));

                /* (MAPCAR lambda expr) — result is spliced by APPEND */
                mapcar_form = cl_cons(sym_mapcar,
                    cl_cons(lambda_form, cl_cons(expr, CL_NIL)));

                return mapcar_form;
            }

            /* General case: reconstruct as (LIST (LIST 'UNQUOTE inner')) */
            {
                CL_Obj inner = qq_expand(inner_form, depth - 1);
                CL_Obj form = qq_list(cl_cons(qq_quote(SYM_UNQUOTE), cl_cons(inner, CL_NIL)));
                return cl_cons(sym_list, cl_cons(form, CL_NIL));
            }
        }

        /* (UNQUOTE-SPLICING inner) at depth > 0 */
        if (head == SYM_UNQUOTE_SPLICING && depth > 0) {
            CL_Obj inner_form = cl_car(cl_cdr(x));

            /* Special case: (UNQUOTE-SPLICING (UNQUOTE-SPLICING expr)) at depth 1
             * This is ,@,@expr — splice the result of expr.
             * Each element of expr is itself a list that gets spliced in.
             * Build: (APPLY 'APPEND (MAPCAR (LAMBDA (V) (LIST 'UNQUOTE-SPLICING V)) expr))
             * Actually, this is very rare. Just do the general case. */

            {
                CL_Obj inner = qq_expand(inner_form, depth - 1);
                CL_Obj form = qq_list(cl_cons(qq_quote(SYM_UNQUOTE_SPLICING), cl_cons(inner, CL_NIL)));
                return cl_cons(sym_list, cl_cons(form, CL_NIL));
            }
        }

        /* (QUASIQUOTE expr): increase depth, wrap in LIST */
        if (head == SYM_QUASIQUOTE) {
            CL_Obj inner = qq_expand(cl_car(cl_cdr(x)), depth + 1);
            CL_Obj form = qq_list(cl_cons(qq_quote(SYM_QUASIQUOTE), cl_cons(inner, CL_NIL)));
            return cl_cons(sym_list, cl_cons(form, CL_NIL));
        }
    }

    /* Nested list: (LIST (qq-expand x depth)) */
    {
        CL_Obj expanded = qq_expand(x, depth);
        return cl_cons(sym_list, cl_cons(expanded, CL_NIL));
    }
}

/* Compile a quasiquote form by expanding to LIST/APPEND forms,
 * then compiling the result as a normal expression. */
void compile_quasiquote(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tmpl = cl_car(cl_cdr(form));
    CL_Obj expanded = qq_expand(tmpl, 0);
    compile_expr(c, expanded);
}

/* --- Case forms --- */

void compile_case(CL_Compiler *c, CL_Obj form, int error_if_no_match)
{
    /* (case keyform (key-or-keys body...)... [(t|otherwise body...)]) */
    CL_Obj keyform = cl_car(cl_cdr(form));
    CL_Obj clauses = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int temp_slot;
    int done_patches[MAX_PATCHES];
    int n_done = 0;
    int had_default = 0;

    /* Allocate temp slot for keyform value */
    temp_slot = env->local_count;
    env->locals[temp_slot] = CL_NIL;  /* Clear stale binding */
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile and store keyform */
    c->in_tail = 0;
    compile_expr(c, keyform);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)temp_slot);
    cl_emit(c, OP_POP);

    /* Process clauses */
    CL_GC_PROTECT(clauses);
    while (!CL_NULL_P(clauses)) {
        CL_Obj clause = cl_car(clauses);
        CL_Obj keys = cl_car(clause);
        CL_Obj body = cl_cdr(clause);

        /* Check for default clause: T or OTHERWISE */
        if (keys == SYM_T || keys == SYM_OTHERWISE) {
            had_default = 1;
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                cl_emit(c, OP_NIL);
            else
                compile_progn(c, body);
            if (n_done >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "CASE: too many clauses (max %d)", MAX_PATCHES);
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);
        } else {
            int body_patches[MAX_PATCHES];
            int n_body = 0;
            int next_clause_pos;

            /* Emit EQ tests for each key */
            if (CL_CONS_P(keys)) {
                /* Multiple keys: ((k1 k2 k3) body...) */
                CL_Obj k = keys;
                while (!CL_NULL_P(k)) {
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)temp_slot);
                    cl_emit_const(c, cl_car(k));
                    cl_emit(c, OP_EQ);
                    if (n_body >= MAX_PATCHES)
                        cl_error(CL_ERR_GENERAL, "CASE: too many keys in clause (max %d)", MAX_PATCHES);
                    body_patches[n_body++] = cl_emit_jump(c, OP_JTRUE);
                    k = cl_cdr(k);
                }
            } else {
                /* Single key: (key body...) */
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)temp_slot);
                cl_emit_const(c, keys);
                cl_emit(c, OP_EQ);
                body_patches[n_body++] = cl_emit_jump(c, OP_JTRUE);
            }

            /* No key matched — jump to next clause */
            next_clause_pos = cl_emit_jump(c, OP_JMP);

            /* body: patch all key-match jumps here */
            {
                int j;
                for (j = 0; j < n_body; j++)
                    cl_patch_jump(c, body_patches[j]);
            }

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                cl_emit(c, OP_NIL);
            else
                compile_progn(c, body);
            if (n_done >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "CASE: too many clauses (max %d)", MAX_PATCHES);
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            /* next_clause: */
            cl_patch_jump(c, next_clause_pos);
        }

        clauses = cl_cdr(clauses);
    }
    CL_GC_UNPROTECT(1);

    /* No default matched */
    if (!had_default) {
        if (error_if_no_match) {
            /* ecase: signal error */
            CL_Obj sym_error = cl_intern_in("ERROR", 5, cl_package_cl);
            CL_Obj errmsg = cl_make_string("ECASE: no matching clause", 25);
            int idx = cl_add_constant(c, sym_error);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            cl_emit_const(c, errmsg);
            cl_emit(c, OP_CALL);
            cl_emit(c, 1);
        } else {
            cl_emit(c, OP_NIL);
        }
    }

    /* Patch all done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            cl_patch_jump(c, done_patches[i]);
    }

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore scope */
    env->local_count = saved_local_count;
}

void compile_typecase(CL_Compiler *c, CL_Obj form, int error_if_no_match)
{
    /* (typecase keyform (type-spec body...)... [(t|otherwise body...)])
     * Uses TYPEP for each clause — supports compound type specifiers. */
    CL_Obj keyform = cl_car(cl_cdr(form));
    CL_Obj clauses = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int temp_slot;
    int done_patches[MAX_PATCHES];
    int n_done = 0;
    int had_default = 0;
    CL_Obj sym_typep;
    int typep_idx;

    /* Allocate temp slot for keyform value */
    temp_slot = env->local_count;
    env->locals[temp_slot] = CL_NIL;  /* Clear stale binding */
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile and store keyform */
    c->in_tail = 0;
    compile_expr(c, keyform);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)temp_slot);
    cl_emit(c, OP_POP);

    /* Pre-load TYPEP symbol index — must use CL package explicitly,
     * not cl_current_package (which may be KEYWORD during #+#. eval) */
    sym_typep = cl_intern_in("TYPEP", 5, cl_package_cl);
    typep_idx = cl_add_constant(c, sym_typep);

    /* Process clauses */
    CL_GC_PROTECT(clauses);
    while (!CL_NULL_P(clauses)) {
        CL_Obj clause = cl_car(clauses);
        CL_Obj type_spec = cl_car(clause);
        CL_Obj body = cl_cdr(clause);

        if (type_spec == SYM_T || type_spec == SYM_OTHERWISE) {
            had_default = 1;
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                cl_emit(c, OP_NIL);
            else
                compile_progn(c, body);
            if (n_done >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "TYPECASE: too many clauses (max %d)", MAX_PATCHES);
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);
        } else {
            int jnil_pos;

            /* Emit: (typep keyform 'type-spec) */
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)typep_idx);
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)temp_slot);
            cl_emit_const(c, type_spec);
            cl_emit(c, OP_CALL);
            cl_emit(c, 2);
            jnil_pos = cl_emit_jump(c, OP_JNIL);

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                cl_emit(c, OP_NIL);
            else
                compile_progn(c, body);
            if (n_done >= MAX_PATCHES)
                cl_error(CL_ERR_GENERAL, "TYPECASE: too many clauses (max %d)", MAX_PATCHES);
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            cl_patch_jump(c, jnil_pos);
        }

        clauses = cl_cdr(clauses);
    }
    CL_GC_UNPROTECT(1);

    /* No default */
    if (!had_default) {
        if (error_if_no_match) {
            CL_Obj sym_error = cl_intern_in("ERROR", 5, cl_package_cl);
            CL_Obj errmsg = cl_make_string(
                "ETYPECASE: ~S fell through without matching any clause", 54);
            int idx = cl_add_constant(c, sym_error);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            cl_emit_const(c, errmsg);
            /* Push the keyform value so error message shows what failed */
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)temp_slot);
            cl_emit(c, OP_CALL);
            cl_emit(c, 2);
        } else {
            cl_emit(c, OP_NIL);
        }
    }

    /* Patch done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            cl_patch_jump(c, done_patches[i]);
    }

    cl_env_clear_boxed(env, saved_local_count);

    env->local_count = saved_local_count;
}

/* --- Multiple Values --- */

CL_Obj compile_multiple_value_bind(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-bind (vars...) values-form body...)
     *
     * Trampoline-aware: prelude binds the locals and resets MV state,
     * body's tail form returns to the driver loop, postlude restores
     * env->local_count + in_tail.  Without this, deeply nested MVB
     * (common in optimized macro output) grows the C stack one frame
     * per level. */
    CL_Obj vars_list = cl_car(cl_cdr(form));
    CL_Obj values_form = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    CL_Obj vl;
    int var_index;
    CL_TailFrame *tf;

    /* Compile values form — primary on stack, MV buffer set */
    c->in_tail = 0;
    compile_expr(c, values_form);

    /* Load MV values into local slots.
     * Var 0 uses the primary from the stack (reliable).
     * Vars 1+ use OP_MV_LOAD (reads from MV buffer, set by bi_values). */
    var_index = 0;
    vl = vars_list;
    while (!CL_NULL_P(vl)) {
        CL_Obj var = cl_car(vl);
        int slot = cl_env_add_local(env, var);
        if (var_index == 0) {
            /* Primary value is on stack top */
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);
        } else {
            cl_emit(c, OP_MV_LOAD);
            cl_emit(c, (uint8_t)var_index);
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);
        }
        var_index++;
        vl = cl_cdr(vl);
    }

    /* If no vars, still need to pop the primary */
    if (var_index == 0)
        cl_emit(c, OP_POP);

    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Reset MV state so stale values don't leak through the body */
    cl_emit(c, OP_MV_RESET);

    c->in_tail = saved_tail;

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_MULTIPLE_VALUE_BIND;
    tf->saved_local_count = saved_local_count;
    tf->saved_tail = saved_tail;
    return compile_body_tail(c, body);
}

void compile_multiple_value_list(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-list form) */
    CL_Obj values_form = cl_car(cl_cdr(form));
    int saved_tail = c->in_tail;

    c->in_tail = 0;
    compile_expr(c, values_form);
    cl_emit(c, OP_MV_TO_LIST);   /* pops primary, builds list from MV state */

    c->in_tail = saved_tail;
}

void compile_nth_value(CL_Compiler *c, CL_Obj form)
{
    /* (nth-value n form) */
    CL_Obj n_form = cl_car(cl_cdr(form));
    CL_Obj values_form = cl_car(cl_cdr(cl_cdr(form)));
    int saved_tail = c->in_tail;

    c->in_tail = 0;

    if (CL_FIXNUM_P(n_form)) {
        int idx = CL_FIXNUM_VAL(n_form);
        compile_expr(c, values_form);
        if (idx == 0) {
            /* Primary value is already on stack — nothing to do */
        } else {
            cl_emit(c, OP_POP);  /* discard primary */
            cl_emit(c, OP_MV_LOAD);
            cl_emit(c, (uint8_t)(idx < 0 ? 255 : idx > 255 ? 255 : idx));
        }
        /* nth-value returns a single value per CL spec — clear stale MV state
         * so callers using multiple-value-list see only this one value */
        cl_emit(c, OP_MV_RESET);
    } else {
        /* Dynamic index: stack will be [index] [primary] */
        compile_expr(c, n_form);
        compile_expr(c, values_form);
        cl_emit(c, OP_NTH_VALUE); /* pops primary, pops index, pushes result */
    }

    c->in_tail = saved_tail;
}

void compile_multiple_value_prog1(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-prog1 first-form forms...)
     * Evaluate first-form, save all its values,
     * evaluate remaining forms for effect, restore saved values. */
    CL_Obj first_form = cl_car(cl_cdr(form));
    CL_Obj rest_forms = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int list_slot;

    c->in_tail = 0;

    /* Compile first form — primary on stack, MV buffer set */
    compile_expr(c, first_form);
    cl_emit(c, OP_MV_TO_LIST);   /* pops primary, saves all values as a list */

    /* Allocate slot for saved list */
    list_slot = env->local_count;
    env->locals[list_slot] = CL_NIL;  /* Clear stale binding */
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)list_slot);
    cl_emit(c, OP_POP);

    /* Evaluate remaining forms for effect */
    {
        CL_Obj rf = rest_forms;
        CL_GC_PROTECT(rf);
        while (!CL_NULL_P(rf)) {
            compile_expr(c, cl_car(rf));
            cl_emit(c, OP_POP);
            rf = cl_cdr(rf);
        }
        CL_GC_UNPROTECT(1);
    }

    /* Restore MV state by calling VALUES-LIST on saved list */
    {
        CL_Obj vl_sym = cl_intern_in("VALUES-LIST", 11, cl_package_cl);
        int sym_idx = cl_add_constant(c, vl_sym);
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)sym_idx);
        cl_emit(c, OP_LOAD);
        cl_emit(c, (uint8_t)list_slot);
        cl_emit(c, OP_CALL);
        cl_emit(c, 1);
    }

    c->in_tail = saved_tail;

    cl_env_clear_boxed(env, saved_local_count);

    env->local_count = saved_local_count;
}

void compile_multiple_value_call(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-call fn form1 form2 ...)
     * => (apply fn (append (multiple-value-list form1) ...))
     * Build the expansion at compile time and compile it. */
    CL_Obj fn_form = cl_car(cl_cdr(form));
    CL_Obj value_forms = cl_cdr(cl_cdr(form));
    CL_Obj append_args = CL_NIL;
    CL_Obj result, f;
    CL_Obj sym_apply, sym_append, sym_mvl;

    sym_apply  = cl_intern_in("APPLY", 5, cl_package_cl);
    sym_append = cl_intern_in("APPEND", 6, cl_package_cl);
    sym_mvl    = cl_intern_in("MULTIPLE-VALUE-LIST", 19, cl_package_cl);

    CL_GC_PROTECT(fn_form);
    CL_GC_PROTECT(append_args);

    /* Build list of (multiple-value-list formN) in reverse */
    for (f = value_forms; !CL_NULL_P(f); f = cl_cdr(f)) {
        CL_Obj mvl_call = cl_cons(sym_mvl, cl_cons(cl_car(f), CL_NIL));
        append_args = cl_cons(mvl_call, append_args);
    }

    /* Reverse to restore order */
    {
        CL_Obj rev = CL_NIL;
        CL_GC_PROTECT(rev);
        while (!CL_NULL_P(append_args)) {
            rev = cl_cons(cl_car(append_args), rev);
            append_args = cl_cdr(append_args);
        }
        append_args = rev;
        CL_GC_UNPROTECT(1);
    }

    /* Build (apply fn (append mvl1 mvl2 ...)) */
    result = cl_cons(sym_apply,
                cl_cons(fn_form,
                    cl_cons(cl_cons(sym_append, append_args),
                        CL_NIL)));
    CL_GC_UNPROTECT(2);

    compile_expr(c, result);
}

/* --- Eval-when / Defsetf --- */

CL_Obj compile_eval_when(CL_Compiler *c, CL_Obj form)
{
    /* (eval-when (situations...) body...)
     * In single-pass compile-and-eval, always execute body.
     * At top level with multiple forms: if any is defmacro/deftype,
     * compile and eval each individually so macros are available for
     * later forms.
     *
     * Trampoline-aware: the body's tail form returns to the driver loop
     * via a CL_TAIL_EVAL_WHEN frame (no postlude needed — pure body
     * wrapper).  The two-pass branch (top-level with definitions) still
     * recurses into compile_progn for the second pass; that path runs
     * at toplevel so it doesn't compound deep nesting. */
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_TailFrame *tf;

    if (c->env->parent == NULL &&
        !CL_NULL_P(body) && !CL_NULL_P(cl_cdr(body))) {
        /* Check if any body form is a definition that later forms might depend on */
        int has_def = 0;
        CL_Obj rest;
        for (rest = body; !CL_NULL_P(rest); rest = cl_cdr(rest)) {
            CL_Obj sub = cl_car(rest);
            if (CL_CONS_P(sub)) {
                CL_Obj head = cl_car(sub);
                if (head == SYM_DEFMACRO || head == SYM_DEFTYPE) {
                    has_def = 1;
                    break;
                }
            }
        }
        if (has_def) {
            /* First pass: compile and eval each form individually so
               macros/types are available for later forms in this body */
            for (rest = body; !CL_NULL_P(rest); rest = cl_cdr(rest)) {
                CL_Obj sub = cl_car(rest);
                CL_Obj bc;
                CL_GC_PROTECT(rest);
                bc = cl_compile(sub);
                if (!CL_NULL_P(bc)) {
                    cl_vm_eval(bc);
                }
                CL_GC_UNPROTECT(1);
            }
            /* Second pass falls through to the trampoline body below so
             * bytecodes are preserved for FASL serialization (compile-file).
             * Macros are now available; definitions may eval twice — harmless. */
        }
    }

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_EVAL_WHEN;
    return compile_progn_tail(c, body);
}

void compile_load_time_value(CL_Compiler *c, CL_Obj form)
{
    /* (load-time-value form &optional read-only-p)
     * In single-pass compile-and-eval, compile-time IS load-time.
     * Evaluate the form now in the null lexical environment,
     * then emit the result as a constant. */
    CL_Obj value_form = cl_car(cl_cdr(form));
    CL_Obj bytecode, result;

    CL_GC_PROTECT(form);

    /* Compile and evaluate the form */
    bytecode = cl_compile(value_form);
    if (CL_NULL_P(bytecode)) {
        /* Form compiled to nothing (shouldn't happen, but be safe) */
        cl_emit(c, OP_NIL);
        CL_GC_UNPROTECT(1);
        return;
    }

    CL_GC_PROTECT(bytecode);
    result = cl_vm_eval(bytecode);
    CL_GC_UNPROTECT(1); /* bytecode */

    /* Emit the evaluated result as a constant */
    if (CL_NULL_P(result))
        cl_emit(c, OP_NIL);
    else if (result == SYM_T)
        cl_emit(c, OP_T);
    else
        cl_emit_const(c, result);

    CL_GC_UNPROTECT(1); /* form */
}

void compile_defsetf(CL_Compiler *c, CL_Obj form)
{
    /* Short form: (defsetf accessor updater)
     * Registers that (setf (accessor args...) val) → (updater args... val) */
    CL_Obj accessor = cl_car(cl_cdr(form));
    CL_Obj updater = cl_car(cl_cdr(cl_cdr(form)));
    int acc_idx, upd_idx;

    /* Store mapping in setf_table at compile time (immediate side effect) */
    {
        CL_Obj pair = cl_cons(accessor, updater);
        cl_tables_wrlock();
        setf_table = cl_cons(pair, setf_table);
        cl_tables_rwunlock();
    }

    /* Emit OP_DEFSETF so the mapping is registered at load time (FASL) */
    acc_idx = cl_add_constant(c, accessor);
    upd_idx = cl_add_constant(c, updater);
    cl_emit(c, OP_DEFSETF);
    cl_emit(c, (uint8_t)(acc_idx >> 8));
    cl_emit(c, (uint8_t)(acc_idx));
    cl_emit(c, (uint8_t)(upd_idx >> 8));
    cl_emit(c, (uint8_t)(upd_idx));
}

/* --- Deftype --- */

void compile_deftype(CL_Compiler *c, CL_Obj form)
{
    /* (deftype name lambda-list body...)
     * Compile the expander as a lambda, then emit OP_DEFTYPE */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    int idx;

    CL_Obj lambda_form = cl_cons(SYM_LAMBDA,
                                  cl_cons(lambda_list, body));
    CL_GC_PROTECT(lambda_form);

    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(1);

    /* OP_DEFTYPE pops the closure, registers type, pushes name */
    idx = cl_add_constant(c, name);
    cl_emit(c, OP_DEFTYPE);
    cl_emit_u16(c, (uint16_t)idx);
}

/* --- Defvar / Defparameter --- */

void compile_defvar(CL_Compiler *c, CL_Obj form)
{
    /* (defvar name) or (defvar name init-form) */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);

    sym->flags |= CL_SYM_SPECIAL;

    if (!CL_NULL_P(rest)) {
        int idx;
        compile_expr(c, cl_car(rest));
        idx = cl_add_constant(c, name);
        /* OP_DEFVAR: mark special, store only if unbound (runtime check) */
        cl_emit(c, OP_DEFVAR);
        cl_emit_u16(c, (uint16_t)idx);
    }
    cl_emit_const(c, name);
}

void compile_defparameter(CL_Compiler *c, CL_Obj form)
{
    /* (defparameter name init-form) — always sets value, marks special */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);
    int idx;

    sym->flags |= CL_SYM_SPECIAL;

    if (!CL_NULL_P(rest)) {
        compile_expr(c, cl_car(rest));
        idx = cl_add_constant(c, name);
        /* OP_GSTORE always sets the value (defparameter semantics).
         * Then OP_DEFVAR with NIL marks the symbol special at load time
         * (critical for FASL — compile-time side effect is lost). */
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_POP);
        cl_emit_const(c, CL_NIL);
        cl_emit(c, OP_DEFVAR);
        cl_emit_u16(c, (uint16_t)idx);
    }
    cl_emit_const(c, name);
}

void compile_defconstant(CL_Compiler *c, CL_Obj form)
{
    /* (defconstant name value) — always sets value, marks constant */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);
    int idx;

    /* If already constant with a different value, error */
    if ((sym->flags & CL_SYM_CONSTANT) && sym->value != CL_UNBOUND) {
        if (!CL_NULL_P(rest)) {
            /* Compile the value to check, but we'll compare at compile time */
            /* CL spec: redefining with same value is allowed, different value is undefined */
        }
    }

    sym->flags |= CL_SYM_CONSTANT;

    if (!CL_NULL_P(rest)) {
        compile_expr(c, cl_car(rest));
        idx = cl_add_constant(c, name);
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_POP);
    }
    cl_emit_const(c, name);
}

/* --- named-lambda --- */

void compile_named_lambda(CL_Compiler *c, CL_Obj form)
{
    /* (named-lambda name lambda-list &body body) → named closure */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Obj lambda_form = cl_cons(SYM_LAMBDA, rest);
    CL_GC_PROTECT(lambda_form);
    pending_lambda_name = name;
    compile_expr(c, lambda_form);
    CL_GC_UNPROTECT(1);
}

/* --- Defun / Defmacro --- */

void compile_defun(CL_Compiler *c, CL_Obj form)
{
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_Obj block_body, lambda_form;
    CL_Obj real_name = name;       /* symbol for block/return */
    CL_Obj store_sym = name;       /* symbol for OP_FSTORE */
    int is_setf_fn = 0;

    /* Handle (defun (setf name) ...) — setf function */
    if (CL_CONS_P(name) && cl_car(name) == SYM_SETF && CL_CONS_P(cl_cdr(name))) {
        CL_Obj accessor = cl_car(cl_cdr(name));
        is_setf_fn = 1;
        real_name = accessor;  /* block name = accessor name */

        /* Create hidden symbol %SETF-<name> for storing the function.
         * Use the accessor's home package so it matches the symbol
         * created by compile_setf_place's late-binding path. */
        {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(accessor);
            CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
            CL_Obj pkg = sym->package;
            char buf[256];
            int len;
            if (CL_NULL_P(pkg)) pkg = cl_package_cl;
            len = snprintf(buf, sizeof(buf), "%%SETF-%.*s",
                           (int)sname->length, sname->data);
            if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
            store_sym = cl_intern_in(buf, (uint32_t)len, pkg);
        }

        /* Register in setf_fn_table: (accessor . store_sym) */
        {
            CL_Obj pair = cl_cons(accessor, store_sym);
            cl_tables_wrlock();
            setf_fn_table = cl_cons(pair, setf_fn_table);
            cl_tables_rwunlock();
        }
    }

    /* CL spec: defun wraps body in (block name ...) */
    block_body = cl_cons(SYM_BLOCK, cl_cons(real_name, body));
    CL_GC_PROTECT(block_body);

    /* Build (lambda (params) (block name body...)) */
    lambda_form = cl_cons(SYM_LAMBDA,
                          cl_cons(lambda_list,
                                  cl_cons(block_body, CL_NIL)));
    CL_GC_PROTECT(lambda_form);

    pending_lambda_name = real_name;
    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(2);

    /* Store as function binding */
    {
        int idx = cl_add_constant(c, store_sym);
        /* OP_FSTORE peeks top without popping — stores to function slot */
        cl_emit(c, OP_FSTORE);
        cl_emit_u16(c, (uint16_t)idx);
    }

    /* Return the name (replace closure on stack) */
    cl_emit(c, OP_POP);
    cl_emit_const(c, is_setf_fn ? name : real_name);
}

/* Generate a unique uninterned symbol for defmacro destructuring */
CL_Obj defmacro_gensym(void)
{
    static volatile uint32_t counter = 0;
    char buf[32];
    CL_Obj name_str;
    snprintf(buf, sizeof(buf), "%%DMAC%lu",
             (unsigned long)(platform_atomic_inc(&counter) - 1));
    name_str = cl_make_string(buf, (uint32_t)strlen(buf));
    return cl_make_symbol(name_str);
}

/* Check if param is a lambda list keyword (&optional, &key, etc.) */
int defmacro_is_ll_keyword(CL_Obj param)
{
    return CL_SYMBOL_P(param) &&
           (param == SYM_AMP_OPTIONAL || param == SYM_AMP_KEY ||
            param == SYM_AMP_REST || param == SYM_AMP_BODY ||
            param == SYM_AMP_ALLOW_OTHER_KEYS ||
            param == SYM_AMP_ENVIRONMENT || param == SYM_AMP_WHOLE);
}

/* Check if a lambda list needs destructuring transformation.
 * Returns 1 if any required parameter is a list (not a symbol),
 * or if &body/&rest is followed by a list pattern.
 * List params after &optional/&key are (name default) specs, not destructuring. */
int defmacro_needs_destructuring(CL_Obj ll)
{
    int in_optional_or_key = 0;

    while (!CL_NULL_P(ll)) {
        CL_Obj param = cl_car(ll);

        if (CL_SYMBOL_P(param) &&
            (param == SYM_AMP_OPTIONAL || param == SYM_AMP_KEY)) {
            in_optional_or_key = 1;
            ll = cl_cdr(ll);
            continue;
        }
        if (CL_SYMBOL_P(param) &&
            (param == SYM_AMP_BODY || param == SYM_AMP_REST)) {
            in_optional_or_key = 0; /* back to positional mode */
            /* Check if next param is a list pattern */
            {
                CL_Obj next = cl_cdr(ll);
                if (!CL_NULL_P(next) && CL_CONS_P(cl_car(next)))
                    return 1;
            }
            ll = cl_cdr(ll);
            continue;
        }
        if (defmacro_is_ll_keyword(param)) {
            ll = cl_cdr(ll);
            continue;
        }

        /* Required parameter (before any keyword) */
        if (!in_optional_or_key && CL_CONS_P(param))
            return 1;

        ll = cl_cdr(ll);
    }
    return 0;
}

void compile_defmacro(CL_Compiler *c, CL_Obj form)
{
    /* Compile the expander as a lambda, then emit OP_DEFMACRO */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    int idx;
    CL_Obj lambda_form;

    CL_GC_PROTECT(name);
    CL_GC_PROTECT(lambda_list);
    CL_GC_PROTECT(body);

    /* Strip &environment var from lambda list and bind it to the current
     * lexical env (captured by the caller, read via %MACROEXPAND-ENV)
     * at the head of the expander body.  Macro expanders are called
     * with (whole-form arg1 arg2 ...) — the env is threaded through a
     * thread-local rather than the arg list, so the user's &environment
     * parameter is realized here as a plain LET binding. */
    {
        CL_Obj cur = lambda_list;
        CL_Obj prev = CL_NIL;
        while (!CL_NULL_P(cur)) {
            CL_Obj param = cl_car(cur);
            if (param == SYM_AMP_ENVIRONMENT) {
                CL_Obj next = cl_cdr(cur);
                CL_Obj env_var = CL_NULL_P(next) ? CL_NIL : cl_car(next);
                CL_Obj after = CL_NULL_P(next) ? CL_NIL : cl_cdr(next);
                /* Remove &environment and its var from the lambda list */
                if (CL_NULL_P(prev))
                    lambda_list = after;
                else
                    ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = after;
                /* Wrap body: (let ((env-var (CLAMIGA::%MACROEXPAND-ENV))) ...body) */
                if (CL_SYMBOL_P(env_var)) {
                    CL_Obj capture_sym =
                        cl_intern_in("%MACROEXPAND-ENV", 16, cl_package_clamiga);
                    CL_Obj capture_call = cl_cons(capture_sym, CL_NIL);
                    CL_Obj binding = cl_cons(env_var, cl_cons(capture_call, CL_NIL));
                    CL_Obj bindings = cl_cons(binding, CL_NIL);
                    CL_Obj let_form = cl_cons(SYM_LET, cl_cons(bindings, body));
                    body = cl_cons(let_form, CL_NIL);
                }
                break;
            }
            prev = cur;
            cur = cl_cdr(cur);
        }
    }

    /* Handle &whole: strip it from the lambda list and use its variable
     * as the first parameter (receives the whole form from expander).
     * If no &whole, add a hidden gensym to receive (and ignore) the form.
     * Macro expanders are always called with (whole-form arg1 arg2 ...). */
    {
        CL_Obj whole_var = CL_NIL;
        if (!CL_NULL_P(lambda_list) && cl_car(lambda_list) == SYM_AMP_WHOLE) {
            /* (&whole var ...) — strip &whole, use var */
            CL_Obj rest = cl_cdr(lambda_list);
            if (!CL_NULL_P(rest)) {
                whole_var = cl_car(rest);
                lambda_list = cl_cdr(rest); /* skip &whole and var */
            }
        }
        if (CL_NULL_P(whole_var)) {
            /* No &whole — generate hidden gensym to receive the form arg */
            whole_var = defmacro_gensym();
        }
        /* Prepend whole_var as the first parameter */
        lambda_list = cl_cons(whole_var, lambda_list);
    }

    /* Transform destructuring lambda lists:
     * Replace list parameters with gensyms and wrap body in
     * destructuring-bind forms. */
    if (defmacro_needs_destructuring(lambda_list)) {
        CL_Obj new_ll = CL_NIL, new_ll_tail = CL_NIL;
        CL_Obj cur = lambda_list;
        int in_opt_key = 0; /* inside &optional or &key section */

        CL_GC_PROTECT(new_ll);
        CL_GC_PROTECT(new_ll_tail);

        while (!CL_NULL_P(cur)) {
            CL_Obj param = cl_car(cur);
            CL_Obj next_param;
            CL_Obj cell;

            /* Track lambda list keyword sections */
            if (CL_SYMBOL_P(param) &&
                (param == SYM_AMP_OPTIONAL || param == SYM_AMP_KEY)) {
                in_opt_key = 1;
                cell = cl_cons(param, CL_NIL);
                if (CL_NULL_P(new_ll)) { new_ll = cell; }
                else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                new_ll_tail = cell;
                cur = cl_cdr(cur);
                continue;
            }

            if (!in_opt_key && CL_CONS_P(param)) {
                /* Required list parameter — replace with gensym + destructuring-bind */
                CL_Obj gs = defmacro_gensym();
                CL_Obj db_form;
                CL_GC_PROTECT(gs);

                /* Wrap body: (destructuring-bind pattern gensym ...body) */
                db_form = cl_cons(SYM_DESTRUCTURING_BIND,
                           cl_cons(param, cl_cons(gs, body)));
                body = cl_cons(db_form, CL_NIL);

                /* Add gensym to new lambda list */
                cell = cl_cons(gs, CL_NIL);
                if (CL_NULL_P(new_ll)) { new_ll = cell; }
                else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                new_ll_tail = cell;
                CL_GC_UNPROTECT(1); /* gs */
            } else if (CL_SYMBOL_P(param) &&
                       (param == SYM_AMP_BODY || param == SYM_AMP_REST)) {
                in_opt_key = 0; /* back to positional */
                /* &body/&rest — check if next is a list pattern */
                next_param = CL_NULL_P(cl_cdr(cur)) ? CL_NIL : cl_car(cl_cdr(cur));
                if (CL_CONS_P(next_param)) {
                    /* Replace with &body/&rest gensym + destructuring-bind */
                    CL_Obj gs = defmacro_gensym();
                    CL_Obj db_form;
                    CL_GC_PROTECT(gs);

                    db_form = cl_cons(SYM_DESTRUCTURING_BIND,
                               cl_cons(next_param, cl_cons(gs, body)));
                    body = cl_cons(db_form, CL_NIL);

                    /* Add &body/&rest gensym to new lambda list */
                    cell = cl_cons(param, CL_NIL);
                    if (CL_NULL_P(new_ll)) { new_ll = cell; }
                    else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                    new_ll_tail = cell;

                    cell = cl_cons(gs, CL_NIL);
                    ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell;
                    new_ll_tail = cell;

                    cur = cl_cdr(cur); /* skip the list pattern */
                    CL_GC_UNPROTECT(1); /* gs */
                } else {
                    /* Normal &body/&rest with symbol — just copy */
                    cell = cl_cons(param, CL_NIL);
                    if (CL_NULL_P(new_ll)) { new_ll = cell; }
                    else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                    new_ll_tail = cell;
                }
            } else {
                /* Normal symbol parameter or &optional/&key spec — just copy */
                cell = cl_cons(param, CL_NIL);
                if (CL_NULL_P(new_ll)) { new_ll = cell; }
                else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                new_ll_tail = cell;
            }
            cur = cl_cdr(cur);
        }

        lambda_list = new_ll;
        CL_GC_UNPROTECT(2); /* new_ll, new_ll_tail */
    }

    /* CL spec: a macro's body is enclosed in an implicit BLOCK named
       after the macro.  Build (lambda (ll) (block name body...)).
       Without this, (return-from <macro-name> ...) inside the body
       errors with "no block named <macro-name>". */
    {
        CL_Obj block_body = cl_cons(SYM_BLOCK, cl_cons(name, body));
        CL_GC_PROTECT(block_body);
        lambda_form = cl_cons(SYM_LAMBDA,
                              cl_cons(lambda_list,
                                      cl_cons(block_body, CL_NIL)));
        CL_GC_UNPROTECT(1);
    }
    CL_GC_PROTECT(lambda_form);

    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(4); /* lambda_form, body, lambda_list, name */

    /* OP_DEFMACRO pops the closure, registers macro, pushes name */
    idx = cl_add_constant(c, name);
    cl_emit(c, OP_DEFMACRO);
    cl_emit_u16(c, (uint16_t)idx);
}

/* --- Declaration processing --- */

/* Process a single declaration specifier, applying global side effects.
 * Used by declaim, proclaim, and declare (for special). */
void cl_process_declaration_specifier(CL_Obj spec)
{
    CL_Obj head;

    if (!CL_CONS_P(spec)) return;
    head = cl_car(spec);

    if (head == SYM_SPECIAL_DECL) {
        /* (special var...) — mark each symbol as globally special */
        CL_Obj vars = cl_cdr(spec);
        while (!CL_NULL_P(vars)) {
            CL_Obj var = cl_car(vars);
            if (CL_SYMBOL_P(var)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(var);
                s->flags |= CL_SYM_SPECIAL;
            }
            vars = cl_cdr(vars);
        }
    } else if (head == SYM_INLINE_DECL) {
        /* (inline fn...) — set inline flag */
        CL_Obj fns = cl_cdr(spec);
        while (!CL_NULL_P(fns)) {
            CL_Obj fn = cl_car(fns);
            if (CL_SYMBOL_P(fn)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(fn);
                s->flags |= CL_SYM_INLINE;
            }
            fns = cl_cdr(fns);
        }
    } else if (head == SYM_NOTINLINE_DECL) {
        /* (notinline fn...) — clear inline flag */
        CL_Obj fns = cl_cdr(spec);
        while (!CL_NULL_P(fns)) {
            CL_Obj fn = cl_car(fns);
            if (CL_SYMBOL_P(fn)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(fn);
                s->flags &= ~CL_SYM_INLINE;
            }
            fns = cl_cdr(fns);
        }
    } else if (head == SYM_OPTIMIZE_DECL) {
        /* (optimize (speed 3) (safety 1) ...) or (optimize speed) */
        CL_Obj quals = cl_cdr(spec);
        while (!CL_NULL_P(quals)) {
            CL_Obj q = cl_car(quals);
            CL_Obj name;
            int val = 3; /* bare name means 3 per CL spec */
            if (CL_CONS_P(q)) {
                name = cl_car(q);
                if (!CL_NULL_P(cl_cdr(q)) && CL_FIXNUM_P(cl_car(cl_cdr(q))))
                    val = CL_FIXNUM_VAL(cl_car(cl_cdr(q)));
            } else {
                name = q;
            }
            if (val < 0) val = 0;
            if (val > 3) val = 3;
            cl_tables_wrlock();
            if (name == SYM_SPEED) cl_optimize_settings.speed = (uint8_t)val;
            else if (name == SYM_SAFETY) cl_optimize_settings.safety = (uint8_t)val;
            else if (name == SYM_DEBUG) cl_optimize_settings.debug = (uint8_t)val;
            else if (name == SYM_SPACE) cl_optimize_settings.space = (uint8_t)val;
            cl_tables_rwunlock();
            quals = cl_cdr(quals);
        }
    }
    /* type, ftype, ignore, ignorable, dynamic-extent — no-op for now */
}

/* Scan body for (declare (special ...)) forms and return a list of
 * locally-special variable symbols. Does NOT consume the forms. */
CL_Obj scan_local_specials(CL_Obj body)
{
    CL_Obj result = CL_NIL;
    CL_Obj forms = body;

    CL_GC_PROTECT(result);
    while (!CL_NULL_P(forms)) {
        CL_Obj form = cl_car(forms);

        /* Stop at first non-declare form */
        if (!CL_CONS_P(form) || cl_car(form) != SYM_DECLARE)
            break;

        /* Walk each specifier in (declare spec1 spec2 ...) */
        {
            CL_Obj specs = cl_cdr(form);
            while (!CL_NULL_P(specs)) {
                CL_Obj spec = cl_car(specs);
                if (CL_CONS_P(spec) && cl_car(spec) == SYM_SPECIAL_DECL) {
                    CL_Obj vars = cl_cdr(spec);
                    while (!CL_NULL_P(vars)) {
                        result = cl_cons(cl_car(vars), result);
                        vars = cl_cdr(vars);
                    }
                }
                specs = cl_cdr(specs);
            }
        }
        forms = cl_cdr(forms);
    }
    CL_GC_UNPROTECT(1);
    return result;
}

/* Check if a variable is in the locally-special list */
int is_locally_special(CL_Obj var, CL_Obj local_specials)
{
    CL_Obj list = local_specials;
    while (!CL_NULL_P(list)) {
        if (cl_car(list) == var) return 1;
        list = cl_cdr(list);
    }
    return 0;
}

/* Strip leading (declare ...) forms from body, processing their specifiers.
 * Also skips docstrings (string literals among declarations per CL spec).
 * Returns the remaining body forms (first non-declare form onward). */
CL_Obj process_body_declarations(CL_Compiler *c, CL_Obj body)
{
    CL_Obj forms = body;
    (void)c;

    while (!CL_NULL_P(forms)) {
        CL_Obj form = cl_car(forms);

        /* Skip docstrings among declarations (string followed by more forms) */
        if (CL_ANY_STRING_P(form) && !CL_NULL_P(cl_cdr(forms))) {
            forms = cl_cdr(forms);
            continue;
        }

        /* Stop at first non-declare form */
        if (!CL_CONS_P(form) || cl_car(form) != SYM_DECLARE)
            break;

        /* Process each specifier.
         * Skip (special ...) — those are scoped declarations handled by
         * scan_local_specials() in compile_let/compile_lambda.
         * Setting the global CL_SYM_SPECIAL flag here would permanently
         * pollute the symbol, making ALL future bindings dynamic even in
         * scopes that didn't declare it special. */
        {
            CL_Obj specs = cl_cdr(form);
            while (!CL_NULL_P(specs)) {
                CL_Obj spec = cl_car(specs);
                if (!(CL_CONS_P(spec) && cl_car(spec) == SYM_SPECIAL_DECL))
                    cl_process_declaration_specifier(spec);
                specs = cl_cdr(specs);
            }
        }
        forms = cl_cdr(forms);
    }
    return forms;
}

/* --- Declaim --- */

void compile_declaim(CL_Compiler *c, CL_Obj form)
{
    /* (declaim decl-spec ...) — process each globally, emit NIL */
    CL_Obj specs = cl_cdr(form);
    while (!CL_NULL_P(specs)) {
        cl_process_declaration_specifier(cl_car(specs));
        specs = cl_cdr(specs);
    }
    cl_emit(c, OP_NIL);
}

/* --- Locally --- */

CL_Obj compile_locally(CL_Compiler *c, CL_Obj form)
{
    /* (locally (declare ...) body...) — pure body wrapper, no postlude.
     * Push a CL_TAIL_LOCALLY placeholder so the trampoline shape stays
     * consistent (compile_expr's drain treats it as a no-op). */
    CL_Obj body = cl_cdr(form);
    CL_TailFrame *tf = cl_tail_push(c);
    tf->kind = CL_TAIL_LOCALLY;
    return compile_body_tail(c, body);
}

/* --- The --- */

void compile_the(CL_Compiler *c, CL_Obj form)
{
    /* (the type-spec value-form) */
    CL_Obj args = cl_cdr(form);
    CL_Obj type_spec, value_form;

    if (CL_NULL_P(args) || CL_NULL_P(cl_cdr(args)))
        cl_error(CL_ERR_ARGS, "THE: requires type and value form");

    type_spec = cl_car(args);
    value_form = cl_car(cl_cdr(args));

    /* Values type specifier (CLHS 4.2.3): (values ...) is only meaningful for
     * multi-value return-type annotation, not for runtime typep on a single
     * value.  Per CLHS 5.3.2 "the", implementations are permitted but not
     * required to check that the number of values matches.  We skip the
     * assertion entirely, matching the behavior of major implementations. */
    if (CL_CONS_P(type_spec)) {
        CL_Obj head = cl_car(type_spec);
        if (CL_SYMBOL_P(head) &&
            head == cl_intern_in("VALUES", 6, cl_package_cl)) {
            compile_expr(c, value_form);
            return;
        }
    }

    if (cl_optimize_settings.safety >= 1) {
        /* Disable tail position: ASSERT_TYPE must run after value form */
        int saved_tail = c->in_tail;
        c->in_tail = 0;
        compile_expr(c, value_form);
        c->in_tail = saved_tail;

        int idx = cl_add_constant(c, type_spec);
        cl_emit(c, OP_ASSERT_TYPE);
        cl_emit_u16(c, (uint16_t)idx);
    } else {
        /* safety 0: no check, preserve tail position */
        compile_expr(c, value_form);
    }
}

/* --- Trace / Untrace --- */

void compile_trace(CL_Compiler *c, CL_Obj form)
{
    /* (trace) — return list of currently traced functions
     * (trace name1 name2 ...) — trace functions, return list of names */
    CL_Obj args = cl_cdr(form);
    int trace_fn_idx;
    int n = 0;
    CL_Obj a;

    if (CL_NULL_P(args)) {
        /* (trace) with no args: call %TRACED-FUNCTIONS */
        int idx = cl_add_constant(c, cl_intern_in("%TRACED-FUNCTIONS", 17, cl_package_clamiga));
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_CALL);
        cl_emit(c, 0);
        return;
    }

    trace_fn_idx = cl_add_constant(c, cl_intern_in("%TRACE-FUNCTION", 15, cl_package_clamiga));
    a = args;
    while (!CL_NULL_P(a)) {
        CL_Obj name = cl_car(a);
        int name_idx = cl_add_constant(c, name);
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)trace_fn_idx);
        cl_emit(c, OP_CONST);
        cl_emit_u16(c, (uint16_t)name_idx);
        cl_emit(c, OP_CALL);
        cl_emit(c, 1);
        n++;
        a = cl_cdr(a);
    }
    cl_emit(c, OP_LIST);
    cl_emit(c, (uint8_t)n);
}

void compile_untrace(CL_Compiler *c, CL_Obj form)
{
    /* (untrace) — untrace all, return NIL
     * (untrace name1 name2 ...) — untrace named functions, return list */
    CL_Obj args = cl_cdr(form);
    int untrace_fn_idx;
    int n = 0;
    CL_Obj a;

    if (CL_NULL_P(args)) {
        /* (untrace) with no args: call %UNTRACE-ALL */
        int idx = cl_add_constant(c, cl_intern_in("%UNTRACE-ALL", 12, cl_package_clamiga));
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_CALL);
        cl_emit(c, 0);
        return;
    }

    untrace_fn_idx = cl_add_constant(c, cl_intern_in("%UNTRACE-FUNCTION", 17, cl_package_clamiga));
    a = args;
    while (!CL_NULL_P(a)) {
        CL_Obj name = cl_car(a);
        int name_idx = cl_add_constant(c, name);
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)untrace_fn_idx);
        cl_emit(c, OP_CONST);
        cl_emit_u16(c, (uint16_t)name_idx);
        cl_emit(c, OP_CALL);
        cl_emit(c, 1);
        n++;
        a = cl_cdr(a);
    }
    cl_emit(c, OP_LIST);
    cl_emit(c, (uint8_t)n);
}

/* --- Time --- */

void compile_time(CL_Compiler *c, CL_Obj form)
{
    /* (time expr) — evaluate expr, print timing/allocation stats, return result */
    CL_Obj body = cl_cdr(form);
    int saved_tail = c->in_tail;
    int start_time_slot, start_consed_slot, start_gc_slot, result_slot;
    int get_time_idx, get_consed_idx, get_gc_idx, report_idx;

    if (CL_NULL_P(body))
        cl_error(CL_ERR_ARGS, "TIME: missing expression");

    c->in_tail = 0;
    start_time_slot = alloc_temp_slot(c->env);
    start_consed_slot = alloc_temp_slot(c->env);
    start_gc_slot = alloc_temp_slot(c->env);
    result_slot = alloc_temp_slot(c->env);

    get_time_idx = cl_add_constant(c, cl_intern_in("%GET-INTERNAL-TIME", 18, cl_package_clamiga));
    get_consed_idx = cl_add_constant(c, cl_intern_in("%GET-BYTES-CONSED", 17, cl_package_clamiga));
    get_gc_idx = cl_add_constant(c, cl_intern_in("%GET-GC-COUNT", 13, cl_package_clamiga));
    report_idx = cl_add_constant(c, cl_intern_in("%TIME-REPORT", 12, cl_package_clamiga));

    /* Capture start time */
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)get_time_idx);
    cl_emit(c, OP_CALL);
    cl_emit(c, 0);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)start_time_slot);
    cl_emit(c, OP_POP);

    /* Capture start bytes consed */
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)get_consed_idx);
    cl_emit(c, OP_CALL);
    cl_emit(c, 0);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)start_consed_slot);
    cl_emit(c, OP_POP);

    /* Capture start GC count */
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)get_gc_idx);
    cl_emit(c, OP_CALL);
    cl_emit(c, 0);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)start_gc_slot);
    cl_emit(c, OP_POP);

    /* Compile body expression (result on stack) */
    compile_expr(c, cl_car(body));

    /* Save body result in temp slot, pop from stack */
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
    cl_emit(c, OP_POP);

    /* Print timing: FLOAD %TIME-REPORT, LOAD start_time, start_consed, start_gc, CALL 3 */
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)report_idx);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)start_time_slot);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)start_consed_slot);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)start_gc_slot);
    cl_emit(c, OP_CALL);
    cl_emit(c, 3);
    cl_emit(c, OP_POP);  /* Discard %TIME-REPORT result (NIL) */

    /* Restore body result */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);
    c->in_tail = saved_tail;
}

/* --- in-package --- */

void compile_in_package(CL_Compiler *c, CL_Obj form)
{
    /* (in-package name) where name is a symbol or string */
    CL_Obj name_form = cl_car(cl_cdr(form));
    CL_Obj pkg;
    const char *name_str;
    uint32_t name_len;
    int idx;

    /* Resolve package name at compile time */
    if (CL_SYMBOL_P(name_form)) {
        CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name_form);
        CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
        name_str = sname->data;
        name_len = sname->length;
    } else if (CL_STRING_P(name_form)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(name_form);
        name_str = s->data;
        name_len = s->length;
    } else {
        cl_error(CL_ERR_TYPE, "IN-PACKAGE: expected symbol or string");
        return;
    }

    pkg = cl_find_package(name_str, name_len);
    if (!CL_NULL_P(pkg)) {
        /* Fast path: package known at compile time.  Set cl_current_package
         * so subsequent forms in the same compilation unit (e.g. inside a
         * compile-file pass) intern symbols in the new package, then emit
         * a constant store into *PACKAGE*. */
        cl_current_package = pkg;
        cl_emit_const(c, pkg);
        idx = cl_add_constant(c, SYM_STAR_PACKAGE);
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
        return;
    }

    /* Fallback: package not known at compile time.  This happens when a
     * top-level form bundles `(load <file>)` and `(in-package :PKG)` so that
     * PKG is created by the load only at run time.  Emit a runtime call to
     * clamiga::%set-current-package so the lookup-and-error happens at the
     * point this form is actually executed. */
    {
        CL_Obj name_str_obj = cl_make_string(name_str, name_len);
        CL_Obj setter_sym = cl_intern_in("%SET-CURRENT-PACKAGE", 20,
                                         cl_package_clamiga);
        int setter_idx = cl_add_constant(c, setter_sym);
        int name_idx = cl_add_constant(c, name_str_obj);
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)setter_idx);
        cl_emit(c, OP_CONST);
        cl_emit_u16(c, (uint16_t)name_idx);
        cl_emit(c, OP_CALL);
        cl_emit(c, 1);
    }
}
