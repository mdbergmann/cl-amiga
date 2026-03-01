/*
 * compiler_extra.c — Conditionals, case, quasiquote, MV, defs
 *
 * Split from compiler.c. Contains: and, or, cond, quasiquote,
 * case, typecase, multiple-value-*, eval-when, defsetf,
 * defvar, defparameter, defun, defmacro.
 */

#include "compiler_internal.h"

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
            jnil_pos = cl_emit_jump(c, OP_JNIL);

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body)) {
                /* (cond (test)) with no body — return test value
                 * But test was already consumed by JNIL, so push T. */
                cl_emit(c, OP_T);
            } else {
                compile_progn(c, body);
            }

            /* Always JMP past NIL fallthrough (even last non-t clause) */
            done_patches[n_done++] = cl_emit_jump(c, OP_JMP);

            cl_patch_jump(c, jnil_pos);
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

    /* (UNQUOTE x): compile x normally */
    if (cl_car(tmpl) == SYM_UNQUOTE) {
        compile_expr(c, cl_car(cl_cdr(tmpl)));
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
    /* (typecase keyform (type body...)... [(t|otherwise body...)]) */
    CL_Obj keyform = cl_car(cl_cdr(form));
    CL_Obj clauses = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int temp_slot;
    int done_patches[64];
    int n_done = 0;
    int had_default = 0;
    CL_Obj sym_type_of;
    int type_of_idx;

    /* Allocate temp slot for (type-of keyform) result */
    temp_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile keyform, call TYPE-OF, store result */
    c->in_tail = 0;
    sym_type_of = cl_intern("TYPE-OF", 7);
    type_of_idx = cl_add_constant(c, sym_type_of);
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)type_of_idx);
    compile_expr(c, keyform);
    cl_emit(c, OP_CALL);
    cl_emit(c, 1);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)temp_slot);
    cl_emit(c, OP_POP);

    /* Process clauses (same structure as case but compare type symbols) */
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
            CL_Obj type_sym;

            /* Map CL type specifier to our type-of symbol */
            if (type_spec == cl_intern("INTEGER", 7) ||
                type_spec == cl_intern("FIXNUM", 6))
                type_sym = cl_intern("FIXNUM", 6);
            else if (type_spec == cl_intern("STRING", 6))
                type_sym = cl_intern("STRING", 6);
            else if (type_spec == cl_intern("CONS", 4) ||
                     type_spec == cl_intern("LIST", 4))
                type_sym = cl_intern("CONS", 4);
            else if (type_spec == cl_intern("SYMBOL", 6))
                type_sym = cl_intern("SYMBOL", 6);
            else if (type_spec == cl_intern("FUNCTION", 8))
                type_sym = cl_intern("FUNCTION", 8);
            else
                type_sym = type_spec; /* Use as-is */

            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)temp_slot);
            cl_emit_const(c, type_sym);
            cl_emit(c, OP_EQ);
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
            CL_Obj errmsg = cl_make_string("ETYPECASE: no matching clause", 29);
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
     * In single-pass compile-and-eval, always execute body. */
    CL_Obj body = cl_cdr(cl_cdr(form));
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

/* --- Defun / Defmacro --- */

void compile_defun(CL_Compiler *c, CL_Obj form)
{
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_Obj block_body, lambda_form;

    /* CL spec: defun wraps body in (block name ...) */
    block_body = cl_cons(SYM_BLOCK, cl_cons(name, body));
    CL_GC_PROTECT(block_body);

    /* Build (lambda (params) (block name body...)) */
    lambda_form = cl_cons(SYM_LAMBDA,
                          cl_cons(lambda_list,
                                  cl_cons(block_body, CL_NIL)));
    CL_GC_PROTECT(lambda_form);

    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(2);

    /* Store as function binding of name */
    {
        int idx = cl_add_constant(c, name);
        /* The closure is on the stack; store as function binding */
        cl_emit(c, OP_DUP);
        cl_emit(c, OP_GSTORE);
        cl_emit_u16(c, (uint16_t)idx);
    }

    /* Return the symbol name */
    cl_emit(c, OP_POP);
    cl_emit_const(c, name);
}

void compile_defmacro(CL_Compiler *c, CL_Obj form)
{
    /* Compile the expander as a lambda, then emit OP_DEFMACRO */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    int idx;

    CL_Obj lambda_form = cl_cons(SYM_LAMBDA,
                                  cl_cons(lambda_list, body));
    CL_GC_PROTECT(lambda_form);

    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(1);

    /* OP_DEFMACRO pops the closure, registers macro, pushes name */
    idx = cl_add_constant(c, name);
    cl_emit(c, OP_DEFMACRO);
    cl_emit_u16(c, (uint16_t)idx);
}
