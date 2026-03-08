/*
 * compiler_extra.c — Conditionals, case, quasiquote, MV, defs
 *
 * Split from compiler.c. Contains: and, or, cond, quasiquote,
 * case, typecase, multiple-value-*, eval-when, defsetf,
 * defvar, defparameter, defun, defmacro.
 */

#include "compiler_internal.h"
#include <stdio.h>

/* --- And / Or --- */

void compile_and(CL_Compiler *c, CL_Obj form)
{
    CL_Obj args = cl_cdr(form);
    int saved_tail = c->in_tail;
    int nil_patches[64];
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
    while (!CL_NULL_P(args)) {
        int is_last = CL_NULL_P(cl_cdr(args));
        if (is_last) {
            c->in_tail = saved_tail;
            compile_expr(c, cl_car(args));
        } else {
            c->in_tail = 0;
            compile_expr(c, cl_car(args));
            cl_emit(c, OP_DUP);
            nil_patches[n_patches++] = cl_emit_jump(c, OP_JNIL);
            cl_emit(c, OP_POP);
        }
        args = cl_cdr(args);
    }

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
    int true_patches[64];
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
    while (!CL_NULL_P(args)) {
        int is_last = CL_NULL_P(cl_cdr(args));
        if (is_last) {
            c->in_tail = saved_tail;
            compile_expr(c, cl_car(args));
        } else {
            c->in_tail = 0;
            compile_expr(c, cl_car(args));
            cl_emit(c, OP_DUP);
            true_patches[n_patches++] = cl_emit_jump(c, OP_JTRUE);
            cl_emit(c, OP_POP);
        }
        args = cl_cdr(args);
    }

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
    int done_patches[64];
    int n_done = 0;

    if (CL_NULL_P(clauses)) {
        cl_emit(c, OP_NIL);
        return;
    }

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
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            cl_patch_jump(c, jnil_pos);
            if (CL_NULL_P(body)) {
                /* JNIL popped the DUP'd value (was NIL), pop the original too */
                cl_emit(c, OP_POP);
            }
        }

        clauses = cl_cdr(clauses);
    }

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

/* Check if a list contains any (UNQUOTE-SPLICING ...) elements */
static int qq_has_splicing(CL_Obj list)
{
    while (CL_CONS_P(list)) {
        CL_Obj elem = cl_car(list);
        if (CL_CONS_P(elem) && cl_car(elem) == SYM_UNQUOTE_SPLICING)
            return 1;
        list = cl_cdr(list);
    }
    return 0;
}

/* Forward declaration */
static void compile_qq(CL_Compiler *c, CL_Obj tmpl);

/* Compile a quasiquote list without splicing:
 * Each element is compiled via compile_qq, then OP_LIST n.
 * Handles dotted tails: `(a b . ,x) */
static void compile_qq_list_nosplice(CL_Compiler *c, CL_Obj tmpl)
{
    int n = 0;
    CL_Obj cursor = tmpl;

    while (CL_CONS_P(cursor)) {
        CL_Obj elem = cl_car(cursor);
        CL_Obj rest = cl_cdr(cursor);

        /* Detect dotted unquote: `(... . ,x) reads as (... UNQUOTE x) */
        if (elem == SYM_UNQUOTE && CL_CONS_P(rest) && CL_NULL_P(cl_cdr(rest))) {
            compile_expr(c, cl_car(rest));
            {
                int i;
                for (i = 0; i < n; i++)
                    cl_emit(c, OP_CONS);
            }
            return;
        }

        compile_qq(c, elem);
        n++;
        cursor = rest;
    }

    if (CL_NULL_P(cursor)) {
        /* Proper list */
        cl_emit(c, OP_LIST);
        cl_emit(c, (uint8_t)n);
    } else {
        /* Dotted tail (non-unquote atom) — compile the cdr, then n CONSes */
        compile_qq(c, cursor);
        {
            int i;
            for (i = 0; i < n; i++)
                cl_emit(c, OP_CONS);
        }
    }
}

/* Compile a quasiquote list with splicing:
 * Groups elements into segments, calls APPEND on them. */
static void compile_qq_list_splice(CL_Compiler *c, CL_Obj tmpl)
{
    int n_segments = 0;
    CL_Obj cursor = tmpl;
    CL_Obj sym_append;
    int idx;
    int saved_tail = c->in_tail;

    /* Splice expressions are NOT in tail position — the APPEND call follows.
     * Without this, user-defined functions in ,@(fn ...) get compiled as
     * OP_TAILCALL which replaces the frame and skips the final APPEND call. */
    c->in_tail = 0;

    /* Load APPEND function */
    sym_append = cl_intern("APPEND", 6);
    idx = cl_add_constant(c, sym_append);
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)idx);

    /* Walk the list, grouping consecutive non-splice elements */
    while (CL_CONS_P(cursor)) {
        CL_Obj elem = cl_car(cursor);

        if (CL_CONS_P(elem) && cl_car(elem) == SYM_UNQUOTE_SPLICING) {
            /* Splice: compile the expression directly */
            compile_expr(c, cl_car(cl_cdr(elem)));
            n_segments++;
        } else {
            /* Start a run of non-splice elements */
            int run = 0;
            while (CL_CONS_P(cursor)) {
                CL_Obj e = cl_car(cursor);
                if (CL_CONS_P(e) && cl_car(e) == SYM_UNQUOTE_SPLICING)
                    break;
                compile_qq(c, e);
                run++;
                cursor = cl_cdr(cursor);
            }
            cl_emit(c, OP_LIST);
            cl_emit(c, (uint8_t)run);
            n_segments++;
            continue;  /* Don't advance cursor again */
        }
        cursor = cl_cdr(cursor);
    }

    c->in_tail = saved_tail;
    cl_emit(c, OP_CALL);
    cl_emit(c, (uint8_t)n_segments);
}

/* Recursive quasiquote template walker */
static void compile_qq(CL_Compiler *c, CL_Obj tmpl)
{
    /* Atom (non-cons, including NIL): emit as constant */
    if (!CL_CONS_P(tmpl)) {
        if (CL_NULL_P(tmpl))
            cl_emit(c, OP_NIL);
        else
            cl_emit_const(c, tmpl);
        return;
    }

    /* (UNQUOTE x): compile x — but NOT in tail position, since the
     * quasiquote still has work to do (LIST/CONS) after this value.
     * Without this, user-defined function calls in ,(fn ...) get
     * OP_TAILCALL which replaces the frame and skips the list construction. */
    if (cl_car(tmpl) == SYM_UNQUOTE) {
        int saved_tail = c->in_tail;
        c->in_tail = 0;
        compile_expr(c, cl_car(cl_cdr(tmpl)));
        c->in_tail = saved_tail;
        return;
    }

    /* List — check for splicing */
    if (qq_has_splicing(tmpl)) {
        compile_qq_list_splice(c, tmpl);
    } else {
        compile_qq_list_nosplice(c, tmpl);
    }
}

void compile_quasiquote(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tmpl = cl_car(cl_cdr(form));
    compile_qq(c, tmpl);
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
    int done_patches[64];
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
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);
        } else {
            int body_patches[64];
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
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            /* next_clause: */
            cl_patch_jump(c, next_clause_pos);
        }

        clauses = cl_cdr(clauses);
    }

    /* No default matched */
    if (!had_default) {
        if (error_if_no_match) {
            /* ecase: signal error */
            CL_Obj sym_error = cl_intern("ERROR", 5);
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
    int done_patches[64];
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

    /* Pre-load TYPEP symbol index */
    sym_typep = cl_intern("TYPEP", 5);
    typep_idx = cl_add_constant(c, sym_typep);

    /* Process clauses */
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
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            cl_patch_jump(c, jnil_pos);
        }

        clauses = cl_cdr(clauses);
    }

    /* No default */
    if (!had_default) {
        if (error_if_no_match) {
            CL_Obj sym_error = cl_intern("ERROR", 5);
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

    env->local_count = saved_local_count;
}

/* --- Multiple Values --- */

void compile_multiple_value_bind(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-bind (vars...) values-form body...) */
    CL_Obj vars_list = cl_car(cl_cdr(form));
    CL_Obj values_form = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    CL_Obj vl;
    int var_index;

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

    /* Compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

    /* Restore scope */
    env->local_count = saved_local_count;
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
        while (!CL_NULL_P(rf)) {
            compile_expr(c, cl_car(rf));
            cl_emit(c, OP_POP);
            rf = cl_cdr(rf);
        }
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
    env->local_count = saved_local_count;
}

/* --- Eval-when / Defsetf --- */

void compile_eval_when(CL_Compiler *c, CL_Obj form)
{
    /* (eval-when (situations...) body...)
     * In single-pass compile-and-eval, always execute body.
     * At top level with multiple forms: if any is defmacro/deftype,
     * compile and eval each individually so macros are available for later forms. */
    CL_Obj body = cl_cdr(cl_cdr(form));

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
            for (rest = body; !CL_NULL_P(rest); rest = cl_cdr(rest)) {
                CL_Obj sub = cl_car(rest);
                CL_Obj bc;
                CL_GC_PROTECT(rest);
                bc = cl_compile(sub);
                if (!CL_NULL_P(bc))
                    cl_vm_eval(bc);
                CL_GC_UNPROTECT(1);
            }
            cl_emit(c, OP_NIL);
            return;
        }
    }
    compile_progn(c, body);
}

void compile_defsetf(CL_Compiler *c, CL_Obj form)
{
    /* Short form: (defsetf accessor updater)
     * Registers that (setf (accessor args...) val) → (updater args... val) */
    CL_Obj accessor = cl_car(cl_cdr(form));
    CL_Obj updater = cl_car(cl_cdr(cl_cdr(form)));

    /* Store mapping in setf_table at compile time (immediate side effect) */
    setf_table = cl_cons(cl_cons(accessor, updater), setf_table);

    /* Return the accessor name */
    cl_emit_const(c, accessor);
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

    if (!CL_NULL_P(rest) && sym->value == CL_UNBOUND) {
        int idx;
        compile_expr(c, cl_car(rest));
        idx = cl_add_constant(c, name);
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_POP);
    }
    cl_emit_const(c, name);
}

void compile_defparameter(CL_Compiler *c, CL_Obj form)
{
    /* (defparameter name init-form) — always sets value */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);
    int idx;

    sym->flags |= CL_SYM_SPECIAL;

    if (!CL_NULL_P(rest)) {
        compile_expr(c, cl_car(rest));
        idx = cl_add_constant(c, name);
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
        cl_emit(c, OP_POP);
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

        /* Create hidden symbol %SETF-<name> for storing the function */
        {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(accessor);
            CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
            char buf[128];
            int len;
            len = snprintf(buf, sizeof(buf), "%%SETF-%.*s",
                           (int)sname->length, sname->data);
            store_sym = cl_intern_in(buf, (uint32_t)len, cl_package_cl);
        }

        /* Register in setf_fn_table: (accessor . store_sym) */
        setf_fn_table = cl_cons(cl_cons(accessor, store_sym), setf_fn_table);
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
static CL_Obj defmacro_gensym(void)
{
    static uint32_t counter = 0;
    char buf[32];
    CL_Obj name_str;
    snprintf(buf, sizeof(buf), "%%DMAC%lu", (unsigned long)counter++);
    name_str = cl_make_string(buf, (uint32_t)strlen(buf));
    return cl_make_symbol(name_str);
}

/* Check if param is a lambda list keyword (&optional, &key, etc.) */
static int is_ll_keyword(CL_Obj param)
{
    return CL_SYMBOL_P(param) &&
           (param == SYM_AMP_OPTIONAL || param == SYM_AMP_KEY ||
            param == SYM_AMP_REST || param == SYM_AMP_BODY ||
            param == SYM_AMP_ALLOW_OTHER_KEYS);
}

/* Check if a lambda list needs destructuring transformation.
 * Returns 1 if any required parameter is a list (not a symbol),
 * or if &body/&rest is followed by a list pattern.
 * List params after &optional/&key are (name default) specs, not destructuring. */
static int defmacro_needs_destructuring(CL_Obj ll)
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
        if (is_ll_keyword(param)) {
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

    lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, body));
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
            if (name == SYM_SPEED) cl_optimize_settings.speed = (uint8_t)val;
            else if (name == SYM_SAFETY) cl_optimize_settings.safety = (uint8_t)val;
            else if (name == SYM_DEBUG) cl_optimize_settings.debug = (uint8_t)val;
            else if (name == SYM_SPACE) cl_optimize_settings.space = (uint8_t)val;
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
    CL_UNUSED(c);

    while (!CL_NULL_P(forms)) {
        CL_Obj form = cl_car(forms);

        /* Skip docstrings among declarations (string followed by more forms) */
        if (CL_STRING_P(form) && !CL_NULL_P(cl_cdr(forms))) {
            forms = cl_cdr(forms);
            continue;
        }

        /* Stop at first non-declare form */
        if (!CL_CONS_P(form) || cl_car(form) != SYM_DECLARE)
            break;

        /* Process each specifier */
        {
            CL_Obj specs = cl_cdr(form);
            while (!CL_NULL_P(specs)) {
                cl_process_declaration_specifier(cl_car(specs));
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

void compile_locally(CL_Compiler *c, CL_Obj form)
{
    /* (locally (declare ...) body...) */
    CL_Obj body = cl_cdr(form);
    compile_body(c, body);
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
    if (CL_NULL_P(pkg)) {
        cl_error(CL_ERR_GENERAL, "Package %.*s not found", (int)name_len, name_str);
        return;
    }

    /* Set at compile time so subsequent symbols are interned correctly */
    cl_current_package = pkg;

    /* Emit runtime code to set *PACKAGE* */
    cl_emit_const(c, pkg);      /* Push package object */
    idx = cl_add_constant(c, SYM_STAR_PACKAGE);
    cl_emit(c, OP_GSTORE);
    cl_emit_u16(c, (uint16_t)idx);
    /* OP_GSTORE leaves value on stack — that's fine as the result */
}
