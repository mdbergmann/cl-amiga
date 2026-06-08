/*
 * compiler_special.c — Control flow, loops, patterns
 *
 * Split from compiler.c. Contains: block, return-from, return,
 * tagbody, go, catch, unwind-protect, flet, labels,
 * dolist, dotimes, do, destructuring-bind.
 */

#include "compiler_internal.h"

/* --- Destructuring --- */

/* Recursive destructuring pattern walker.
 * pos_slot: local slot holding the current list position.
 * pattern: the destructuring lambda list (nested list).
 * Allocates local slots for each bound variable. */
static void compile_destructure_pattern(CL_Compiler *c, int pos_slot,
                                         CL_Obj pattern)
{
    CL_CompEnv *env = c->env;
    int saved_tail = c->in_tail;
    c->in_tail = 0;

    while (!CL_NULL_P(pattern)) {
        CL_Obj elem = cl_car(pattern);

        /* &rest / &body — bind remaining list to next symbol */
        if (elem == SYM_AMP_REST || elem == SYM_AMP_BODY) {
            CL_Obj rest_var = cl_car(cl_cdr(pattern));
            int slot = cl_env_add_local(env, rest_var);
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)pos_slot);
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);
            /* Skip to &key if present, otherwise done */
            pattern = cl_cdr(cl_cdr(pattern)); /* skip &rest var */
            if (!CL_NULL_P(pattern) && cl_car(pattern) == SYM_AMP_KEY)
                continue; /* let &key handler process it */
            break;
        }

        /* &optional — remaining elements are optional with defaults */
        if (elem == SYM_AMP_OPTIONAL) {
            CL_Obj rest = cl_cdr(pattern);
            /* GC-protect cursor — compile_expr (default_val) can compact, making rest stale */
            CL_GC_PROTECT(rest);
            while (!CL_NULL_P(rest)) {
                CL_Obj opt = cl_car(rest);
                CL_Obj var;
                CL_Obj default_val = CL_NIL;
                int slot, skip_pos;

                if (elem == SYM_AMP_KEY) {
                    /* &key after &optional: break to outer loop to process keys */
                    pattern = rest;  /* rest starts at &key */
                    CL_GC_UNPROTECT(1);
                    goto optional_done;
                }
                if (elem == SYM_AMP_REST || elem == SYM_AMP_BODY) {
                    /* &rest after &optional */
                    CL_Obj rest_var = cl_car(cl_cdr(rest));
                    slot = cl_env_add_local(env, rest_var);
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)pos_slot);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)slot);
                    cl_emit(c, OP_POP);
                    CL_GC_UNPROTECT(1);
                    goto done;
                }

                CL_Obj supplied_p = CL_NIL;
                int supplied_slot = -1;

                if (CL_CONS_P(opt)) {
                    var = cl_car(opt);
                    if (!CL_NULL_P(cl_cdr(opt)))
                        default_val = cl_car(cl_cdr(opt));
                    if (!CL_NULL_P(cl_cdr(opt)) &&
                        !CL_NULL_P(cl_cdr(cl_cdr(opt))))
                        supplied_p = cl_car(cl_cdr(cl_cdr(opt)));
                } else {
                    var = opt;
                }

                slot = cl_env_add_local(env, var);
                if (!CL_NULL_P(supplied_p))
                    supplied_slot = cl_env_add_local(env, supplied_p);

                /* If pos is NIL, use default; else take (car pos) */
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                skip_pos = cl_emit_jump(c, OP_JNIL);

                /* pos not nil: var = (car pos), pos = (cdr pos), supplied-p = T */
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                cl_emit(c, OP_CAR);
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)slot);
                cl_emit(c, OP_POP);
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                cl_emit(c, OP_CDR);
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)pos_slot);
                cl_emit(c, OP_POP);
                if (supplied_slot >= 0) {
                    cl_emit(c, OP_T);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)supplied_slot);
                    cl_emit(c, OP_POP);
                }
                {
                    int end_pos = cl_emit_jump(c, OP_JMP);
                    cl_patch_jump(c, skip_pos);
                    /* pos is nil: use default, supplied-p = NIL */
                    compile_expr(c, default_val);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)slot);
                    cl_emit(c, OP_POP);
                    if (supplied_slot >= 0) {
                        cl_emit(c, OP_NIL);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)supplied_slot);
                        cl_emit(c, OP_POP);
                    }
                    cl_patch_jump(c, end_pos);
                }

                rest = cl_cdr(rest);
                elem = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
            }
            CL_GC_UNPROTECT(1);
            goto done;
        optional_done:
            /* Continue outer loop — pattern points to &key */
            continue;
        }

        /* &key — keyword destructuring (plist-based) */
        if (elem == SYM_AMP_KEY) {
            CL_Obj rest = cl_cdr(pattern);
            int scan_slot = alloc_temp_slot(env);
            /* GC-protect cursor — compile_expr (default_val) can compact, making rest stale */
            CL_GC_PROTECT(rest);

            while (!CL_NULL_P(rest)) {
                CL_Obj spec = cl_car(rest);
                CL_Obj var, keyword_sym, default_val = CL_NIL;
                CL_Obj supplied_p = CL_NIL;
                int var_slot, supplied_slot = -1;
                int found_pos, not_found_pos, done_pos;
                const char *kw_name;

                /* Skip &allow-other-keys */
                if (CL_SYMBOL_P(spec) && spec == SYM_AMP_ALLOW_OTHER_KEYS) {
                    rest = cl_cdr(rest);
                    continue;
                }

                /* Parse key spec: var | (var default) | (var default supplied-p) |
                   ((keyword var) default [supplied-p]) */
                if (CL_CONS_P(spec)) {
                    CL_Obj first = cl_car(spec);
                    if (CL_CONS_P(first)) {
                        /* ((keyword var) default ...) */
                        keyword_sym = cl_car(first);
                        var = cl_car(cl_cdr(first));
                    } else {
                        var = first;
                        /* keyword derived from var name */
                        kw_name = cl_symbol_name(var);
                        keyword_sym = cl_intern_keyword(kw_name,
                                         (uint32_t)strlen(kw_name));
                    }
                    if (!CL_NULL_P(cl_cdr(spec)))
                        default_val = cl_car(cl_cdr(spec));
                    if (!CL_NULL_P(cl_cdr(spec)) &&
                        !CL_NULL_P(cl_cdr(cl_cdr(spec))))
                        supplied_p = cl_car(cl_cdr(cl_cdr(spec)));
                } else {
                    var = spec;
                    kw_name = cl_symbol_name(var);
                    keyword_sym = cl_intern_keyword(kw_name,
                                     (uint32_t)strlen(kw_name));
                }

                var_slot = cl_env_add_local(env, var);
                if (!CL_NULL_P(supplied_p))
                    supplied_slot = cl_env_add_local(env, supplied_p);

                /* Emit bytecode to scan plist for keyword:
                 *   scan_slot = pos_slot (copy plist)
                 *   loop: if scan_slot is nil → not_found
                 *         if (car scan_slot) == keyword → found
                 *         scan_slot = (cddr scan_slot)
                 *         goto loop
                 * found: var = (cadr scan_slot), supplied-p = T
                 * not_found: var = default, supplied-p = NIL
                 */
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)scan_slot);
                cl_emit(c, OP_POP);

                /* loop: */
                {
                    int loop_start = c->code_pos;

                    /* if scan_slot is nil → not_found */
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)scan_slot);
                    not_found_pos = cl_emit_jump(c, OP_JNIL);

                    /* (car scan_slot) == keyword? */
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)scan_slot);
                    cl_emit(c, OP_CAR);
                    cl_emit_const(c, keyword_sym);
                    cl_emit(c, OP_EQ);
                    found_pos = cl_emit_jump(c, OP_JTRUE);

                    /* Not found: scan_slot = (cddr scan_slot) */
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)scan_slot);
                    cl_emit(c, OP_CDR);
                    /* Protect against odd-length plist */
                    {
                        int odd_pos = cl_emit_jump(c, OP_JNIL);
                        /* JNIL consumed TOS; reload (cdr scan_slot) */
                        cl_emit(c, OP_LOAD);
                        cl_emit(c, (uint8_t)scan_slot);
                        cl_emit(c, OP_CDR);
                        cl_emit(c, OP_CDR);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)scan_slot);
                        cl_emit(c, OP_POP);
                        cl_emit_loop_jump(c, OP_JMP, loop_start);
                        cl_patch_jump(c, odd_pos);
                        /* Odd-length: fall through to not_found */
                    }

                    /* not_found: var = default, supplied-p = NIL */
                    cl_patch_jump(c, not_found_pos);
                    compile_expr(c, default_val);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)var_slot);
                    cl_emit(c, OP_POP);
                    if (supplied_slot >= 0) {
                        cl_emit(c, OP_NIL);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)supplied_slot);
                        cl_emit(c, OP_POP);
                    }
                    done_pos = cl_emit_jump(c, OP_JMP);

                    /* found: var = (cadr scan_slot), supplied-p = T */
                    cl_patch_jump(c, found_pos);
                    cl_emit(c, OP_LOAD);
                    cl_emit(c, (uint8_t)scan_slot);
                    cl_emit(c, OP_CDR);
                    cl_emit(c, OP_CAR);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)var_slot);
                    cl_emit(c, OP_POP);
                    if (supplied_slot >= 0) {
                        cl_emit(c, OP_T);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)supplied_slot);
                        cl_emit(c, OP_POP);
                    }
                    cl_patch_jump(c, done_pos);
                }

                rest = cl_cdr(rest);
            }
            CL_GC_UNPROTECT(1);
            goto done;
        }

        if (CL_CONS_P(elem)) {
            /* Nested pattern: extract sublist, recurse */
            int sub_slot = alloc_temp_slot(env);
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)pos_slot);
            cl_emit(c, OP_CAR);
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)sub_slot);
            cl_emit(c, OP_POP);
            compile_destructure_pattern(c, sub_slot, elem);
        } else if (CL_SYMBOL_P(elem)) {
            /* Simple variable: bind (car pos) */
            int slot = cl_env_add_local(env, elem);
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)pos_slot);
            cl_emit(c, OP_CAR);
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);
        }

        /* Advance pos = (cdr pos) */
        cl_emit(c, OP_LOAD);
        cl_emit(c, (uint8_t)pos_slot);
        cl_emit(c, OP_CDR);
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)pos_slot);
        cl_emit(c, OP_POP);

        pattern = cl_cdr(pattern);

        /* Dotted pair tail: (a b . rest) — bind rest to remaining list */
        if (!CL_NULL_P(pattern) && !CL_CONS_P(pattern)) {
            if (CL_SYMBOL_P(pattern)) {
                int slot = cl_env_add_local(env, pattern);
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)slot);
                cl_emit(c, OP_POP);
            }
            break;
        }
    }
done:
    c->in_tail = saved_tail;
}

void compile_destructuring_bind(CL_Compiler *c, CL_Obj form)
{
    /* (destructuring-bind pattern expr body...) */
    CL_Obj pattern = cl_car(cl_cdr(form));
    CL_Obj expr = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int pos_slot;

    /* GC-protect pattern and body — compile_expr can compact, making them stale */
    CL_GC_PROTECT(pattern);
    CL_GC_PROTECT(body);

    /* Compile expression, store in temp slot */
    c->in_tail = 0;
    compile_expr(c, expr);
    pos_slot = alloc_temp_slot(env);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)pos_slot);
    cl_emit(c, OP_POP);

    /* Walk pattern, binding variables */
    compile_destructure_pattern(c, pos_slot, pattern);

    /* Compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

    CL_GC_UNPROTECT(2);  /* pattern, body */
    cl_env_clear_boxed(env, saved_local_count);

    /* Restore scope */
    env->local_count = saved_local_count;
}

/* --- Block / Return --- */

/* The NLX-detection scanners below decide whether a BLOCK/TAGBODY must be
 * compiled with a non-local-exit frame (so a RETURN-FROM/GO from inside a
 * nested closure can longjmp out) instead of a cheap local jump.  The
 * decision is made by scanning the *source* body for closure forms and
 * RETURN-FROM/GO references.
 *
 * Crucially, a user macro can hide a closure: `(in-lambda (go start))`
 * expands to `(funcall (lambda () (go start)))`.  Scanning only the
 * unexpanded source misses the lambda, the block/tagbody is wrongly
 * compiled with local jumps, and the GO/RETURN-FROM inside the expanded
 * closure can't reach its tag ("GO: no tag named ..."). This idiom — a
 * macro wrapping its body in a closure — is pervasive (e.g. SLY's
 * with-connection, without-sly-interrupts).  So the scanners macroexpand
 * macro calls before recursing, mirroring scan_body_for_boxing(). */

static int scan_nlx_macro_depth = 0;
#define SCAN_NLX_MACRO_MAX_DEPTH 50

/* Macroexpand FORM once for NLX-detection scanning, with full VM/compiler
 * state save+restore so a side-effecting or failing expander cannot corrupt
 * compiler state.  Returns the expansion, or FORM unchanged when its head is
 * not a macro, expansion failed, or the macro-chain depth cap was hit.
 * Mirrors the macro-expansion guard in scan_body_for_boxing(). */
static CL_Obj scan_nlx_macroexpand_1(CL_Obj form)
{
    CL_Obj head, expanded;
    int saved_sp, saved_fp, saved_dyn, saved_nlx, saved_handler,
        saved_restart, saved_debugger, saved_gc_roots;

    if (!CL_CONS_P(form)) return form;
    head = cl_car(form);
    if (!CL_SYMBOL_P(head) || !cl_macro_p(head)) return form;
    if (scan_nlx_macro_depth >= SCAN_NLX_MACRO_MAX_DEPTH) return form;

    saved_sp = cl_vm.sp;
    saved_fp = cl_vm.fp;
    saved_dyn = cl_dyn_top;
    saved_nlx = cl_nlx_top;
    saved_handler = cl_handler_top;
    saved_restart = cl_restart_top;
    saved_debugger = cl_debugger_enabled;
    saved_gc_roots = gc_root_count;
    cl_debugger_enabled = 0;  /* Suppress debugger during expansion */
    expanded = form;

    scan_nlx_macro_depth++;
    {
        int err; CL_CATCH(err);
        if (err == 0) {
            CL_Obj scan_env = CL_NIL;
            if (cl_active_compiler)
                scan_env = cl_build_lex_env(cl_active_compiler->env);
            expanded = cl_macroexpand_1_env(form, scan_env);
            CL_UNCATCH();
        } else {
            /* Expansion errored: restore state and treat form as opaque. */
            CL_UNCATCH();
            cl_vm.sp = saved_sp;
            cl_vm.fp = saved_fp;
            cl_dynbind_restore_to(saved_dyn);
            cl_nlx_top = saved_nlx;
            gc_root_count = saved_gc_roots;
            expanded = form;
        }
    }
    cl_debugger_enabled = saved_debugger;
    cl_handler_top = saved_handler;
    cl_restart_top = saved_restart;
    scan_nlx_macro_depth--;
    return expanded;
}

/* Recursion-depth cap for the NLX walker — guards against C-stack overflow
 * from pathologically nested forms (mirrors scan_body_for_boxing). */
static int scan_nlx_recurse_depth = 0;
#define SCAN_NLX_MAX_RECURSE_DEPTH 500

/* NLX walker modes:
 *   NLX_ANY_CLOSURE — true if the body contains any closure form
 *                     (lambda/flet/labels/restart-case).  Used for TAGBODY
 *                     and the coarse anonymous-BLOCK check.
 *   NLX_FIND_RF     — true if (return-from <tag> ...) (or bare (return) when
 *                     anonymous) appears anywhere.  Used to test a closure's
 *                     subtree.
 *   NLX_BLOCK       — true if a closure or unwind-protect form contains a
 *                     matching return-from (named block), or any closure /
 *                     uwp-with-(return) exists (anonymous block). */
enum { NLX_ANY_CLOSURE, NLX_FIND_RF, NLX_BLOCK };

static int nlx_scan(CL_Obj form, int mode, CL_Obj tag, int anon);

/* Walk a list of body forms — all in code (evaluated) position. */
static int nlx_scan_body(CL_Obj body, int mode, CL_Obj tag, int anon)
{
    while (CL_CONS_P(body)) {
        if (nlx_scan(cl_car(body), mode, tag, anon))
            return 1;
        body = cl_cdr(body);
    }
    return 0;
}

/* Walk a quasiquote template, descending only into unquoted (evaluated)
 * subforms — the rest is data.  Mirrors scan_qq_for_boxing(). */
static int nlx_scan_qq(CL_Obj tmpl, int mode, CL_Obj tag, int anon)
{
    if (CL_VECTOR_P(tmpl)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(tmpl);
        uint32_t i, n = cl_vector_active_length(v);
        CL_Obj *data = cl_vector_data(v);
        for (i = 0; i < n; i++)
            if (nlx_scan_qq(data[i], mode, tag, anon)) return 1;
        return 0;
    }
    if (!CL_CONS_P(tmpl)) return 0;
    if (cl_car(tmpl) == SYM_UNQUOTE || cl_car(tmpl) == SYM_UNQUOTE_SPLICING)
        return nlx_scan(cl_car(cl_cdr(tmpl)), mode, tag, anon);
    if (cl_car(tmpl) == SYM_QUASIQUOTE) return 0;  /* deeper nesting: data */
    {
        CL_Obj cur = tmpl;
        while (CL_CONS_P(cur)) {
            if (nlx_scan_qq(cl_car(cur), mode, tag, anon)) return 1;
            cur = cl_cdr(cur);
        }
    }
    return 0;
}

/* Single-form NLX walker.  Sees through user macros (a macro can hide a
 * closure or a return-from), and — like scan_body_for_boxing — descends
 * only into CODE positions, skipping binding names, lambda lists,
 * destructuring patterns, case keys, declarations and quoted data so it
 * never macroexpands a symbol that merely names a macro in a non-code slot
 * (e.g. a destructuring-bind pattern produced by DEFMACRO). */
static int nlx_scan(CL_Obj form, int mode, CL_Obj tag, int anon)
{
    CL_Obj head, rest;
    int r = 0;

    if (!CL_CONS_P(form)) return 0;
    if (scan_nlx_recurse_depth >= SCAN_NLX_MAX_RECURSE_DEPTH) return 0;
    head = cl_car(form);
    rest = cl_cdr(form);

    /* RETURN-FROM / RETURN detection. */
    if (mode == NLX_FIND_RF) {
        if (head == SYM_RETURN_FROM && CL_CONS_P(rest) && cl_car(rest) == tag)
            return 1;
        if (anon && head == SYM_RETURN)
            return 1;
    }

    /* Closure-creating forms.  Must appear as (OP ...) — a bare symbol
     * LAMBDA used as a variable does not create a closure. */
    if (head == SYM_LAMBDA || head == SYM_LABELS || head == SYM_FLET
        || head == SYM_RESTART_CASE) {
        if (mode == NLX_ANY_CLOSURE)
            return 1;
        if (mode == NLX_BLOCK) {
            /* Anonymous block promotes on any closure; a named block only
             * when a matching return-from crosses this closure boundary. */
            if (anon) return 1;
            return nlx_scan(form, NLX_FIND_RF, tag, anon);
        }
        /* NLX_FIND_RF: fall through to the structural handlers below, which
         * descend the closure body (skipping its lambda list). */
    }

    /* (unwind-protect ...) containing a return-from to our tag must force a
     * named/anonymous BLOCK onto the NLX path: a local OP_JMP would bypass
     * the unwind-protect's OP_UWPOP + cleanup forms (CLHS: cleanups must run
     * on any non-local exit). */
    if (mode == NLX_BLOCK && head == SYM_UNWIND_PROTECT)
        return nlx_scan(form, NLX_FIND_RF, tag, anon);

    scan_nlx_recurse_depth++;

    /* (quote ...) / (declare ...) / (defmacro ...) — no evaluated code. */
    if (head == SYM_QUOTE || head == SYM_DECLARE || head == SYM_DEFMACRO)
        goto done;

    /* (quasiquote tmpl) — scan only unquoted subforms. */
    if (head == SYM_QUASIQUOTE) {
        if (CL_CONS_P(rest)) r = nlx_scan_qq(cl_car(rest), mode, tag, anon);
        goto done;
    }

    /* (setq/setf place value ...) — scan value forms (places aren't code
     * that creates closures, and expanding a setf-place macro would be a
     * spurious side effect). */
    if (head == SYM_SETQ || head == SYM_SETF) {
        CL_Obj p = rest;
        while (CL_CONS_P(p) && CL_CONS_P(cl_cdr(p))) {
            if (nlx_scan(cl_car(cl_cdr(p)), mode, tag, anon)) { r = 1; goto done; }
            p = cl_cdr(cl_cdr(p));
        }
        goto done;
    }

    /* (lambda params body...) — skip params. */
    if (head == SYM_LAMBDA) {
        if (CL_CONS_P(rest)) r = nlx_scan_body(cl_cdr(rest), mode, tag, anon);
        goto done;
    }
    /* (defun name params body...) — skip name + params. */
    if (head == SYM_DEFUN) {
        if (CL_CONS_P(rest) && CL_CONS_P(cl_cdr(rest)))
            r = nlx_scan_body(cl_cdr(cl_cdr(rest)), mode, tag, anon);
        goto done;
    }
    /* (flet/labels ((name params body...)...) body...) — skip names/params. */
    if (head == SYM_FLET || head == SYM_LABELS) {
        CL_Obj defs, body;
        if (!CL_CONS_P(rest)) goto done;
        defs = cl_car(rest);
        body = cl_cdr(rest);
        while (CL_CONS_P(defs)) {
            CL_Obj def = cl_car(defs);
            if (CL_CONS_P(def) && CL_CONS_P(cl_cdr(def)))
                if (nlx_scan_body(cl_cdr(cl_cdr(def)), mode, tag, anon)) { r = 1; goto done; }
            defs = cl_cdr(defs);
        }
        r = nlx_scan_body(body, mode, tag, anon);
        goto done;
    }
    /* (let/let* ((var value)...) body...) — scan values + body, skip names. */
    if (head == SYM_LET || head == SYM_LETSTAR) {
        CL_Obj bindings, body;
        if (!CL_CONS_P(rest)) goto done;
        bindings = cl_car(rest);
        body = cl_cdr(rest);
        while (CL_CONS_P(bindings)) {
            CL_Obj clause = cl_car(bindings);
            if (CL_CONS_P(clause) && CL_CONS_P(cl_cdr(clause)))
                if (nlx_scan(cl_car(cl_cdr(clause)), mode, tag, anon)) { r = 1; goto done; }
            bindings = cl_cdr(bindings);
        }
        r = nlx_scan_body(body, mode, tag, anon);
        goto done;
    }
    /* (destructuring-bind pattern value body...) — skip pattern. */
    if (head == SYM_DESTRUCTURING_BIND) {
        if (!CL_CONS_P(rest) || !CL_CONS_P(cl_cdr(rest))) goto done;
        if (nlx_scan(cl_car(cl_cdr(rest)), mode, tag, anon)) { r = 1; goto done; }
        r = nlx_scan_body(cl_cdr(cl_cdr(rest)), mode, tag, anon);
        goto done;
    }
    /* (multiple-value-bind (vars...) value body...) — skip var list. */
    if (head == SYM_MULTIPLE_VALUE_BIND) {
        if (!CL_CONS_P(rest) || !CL_CONS_P(cl_cdr(rest))) goto done;
        if (nlx_scan(cl_car(cl_cdr(rest)), mode, tag, anon)) { r = 1; goto done; }
        r = nlx_scan_body(cl_cdr(cl_cdr(rest)), mode, tag, anon);
        goto done;
    }
    /* (do/do* ((var init [step])...) (end result...) body...) — skip names. */
    if (head == SYM_DO || head == SYM_DO_STAR) {
        CL_Obj clauses, endc, body;
        if (!CL_CONS_P(rest)) goto done;
        clauses = cl_car(rest);
        if (!CL_CONS_P(cl_cdr(rest))) goto done;
        endc = cl_car(cl_cdr(rest));
        body = cl_cdr(cl_cdr(rest));
        while (CL_CONS_P(clauses)) {
            CL_Obj clause = cl_car(clauses);
            if (CL_CONS_P(clause) && CL_CONS_P(cl_cdr(clause))) {
                if (nlx_scan(cl_car(cl_cdr(clause)), mode, tag, anon)) { r = 1; goto done; }
                if (CL_CONS_P(cl_cdr(cl_cdr(clause))))
                    if (nlx_scan(cl_car(cl_cdr(cl_cdr(clause))), mode, tag, anon)) { r = 1; goto done; }
            }
            clauses = cl_cdr(clauses);
        }
        if (CL_CONS_P(endc) && nlx_scan_body(endc, mode, tag, anon)) { r = 1; goto done; }
        r = nlx_scan_body(body, mode, tag, anon);
        goto done;
    }
    /* (case/typecase keyform (key body...)...) — skip keys. */
    if (head == SYM_CASE || head == SYM_ECASE ||
        head == SYM_TYPECASE || head == SYM_ETYPECASE) {
        CL_Obj clauses;
        if (!CL_CONS_P(rest)) goto done;
        if (nlx_scan(cl_car(rest), mode, tag, anon)) { r = 1; goto done; }
        clauses = cl_cdr(rest);
        while (CL_CONS_P(clauses)) {
            CL_Obj clause = cl_car(clauses);
            if (CL_CONS_P(clause) && nlx_scan_body(cl_cdr(clause), mode, tag, anon)) { r = 1; goto done; }
            clauses = cl_cdr(clauses);
        }
        goto done;
    }
    /* (macrolet/symbol-macrolet (bindings) body...) — skip bindings. */
    if (head == SYM_MACROLET || head == SYM_SYMBOL_MACROLET) {
        if (CL_CONS_P(rest)) r = nlx_scan_body(cl_cdr(rest), mode, tag, anon);
        goto done;
    }

    /* Macro call: expand and scan the expansion.  THIS is what lets us see
     * a closure or return-from hidden behind a user macro. */
    if (CL_SYMBOL_P(head) && cl_macro_p(head)) {
        CL_Obj expanded = scan_nlx_macroexpand_1(form);
        if (expanded != form) {
            CL_GC_PROTECT(expanded);
            r = nlx_scan(expanded, mode, tag, anon);
            CL_GC_UNPROTECT(1);
            goto done;
        }
    }

    /* General form (function call / other special form): every element is
     * evaluated code.  (Includes ((lambda ...) args) immediate calls — the
     * head cons is scanned too.) */
    {
        CL_Obj cur = form;
        while (CL_CONS_P(cur)) {
            if (nlx_scan(cl_car(cur), mode, tag, anon)) { r = 1; goto done; }
            cur = cl_cdr(cur);
        }
    }

done:
    scan_nlx_recurse_depth--;
    return r;
}

/* Check if BODY contains any closure-creating form (lambda/labels/flet/
 * restart-case), including ones produced by macro expansion.  Used to decide
 * if a TAGBODY needs NLX for cross-closure go support. */
static int tree_has_closure_forms(CL_Obj tree)
{
    return nlx_scan_body(tree, NLX_ANY_CLOSURE, CL_NIL, 0);
}

/* Decide whether a BLOCK must use the NLX path.  A named block needs NLX
 * only when a matching (return-from <tag> ...) crosses a closure boundary or
 * sits inside an unwind-protect; an anonymous block (NIL tag) promotes on
 * any closure too, since a macro-hidden (return ...) cannot be matched
 * against a specific tag.  All recursion sees through macro expansions. */
static int tree_needs_nlx_block(CL_Obj body, CL_Obj tag)
{
    return nlx_scan_body(body, NLX_BLOCK, tag, CL_NULL_P(tag));
}

/* Strip leading declarations from body, then delegate to the
 * trampoline-aware compile_progn_tail (which pushes a PROGN_ITER frame
 * when the body has multiple forms so iteration stays off the C stack).
 * Body must be GC-protected by caller. */
static CL_Obj compile_nontail_body(CL_Compiler *c, CL_Obj body)
{
    CL_Obj rest = process_body_declarations(c, body);
    return compile_progn_tail(c, rest);
}

CL_Obj compile_block(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int saved_block_count = c->block_count;
    int needs_nlx = tree_needs_nlx_block(body, tag);
    CL_BlockInfo *bi;
    CL_TailFrame *tf;

    /* Hard cap: writing to c->blocks[CL_MAX_BLOCKS] would clobber the
     * adjacent block_count field (silent stack-corruption that surfaces
     * later as a SIGBUS in compile_return_from when iterating
     * c->blocks[].tag).  Raise a clean compile error instead. */
    if (c->block_count >= CL_MAX_BLOCKS) {
        cl_error(CL_ERR_OVERFLOW,
                 "BLOCK nesting depth exceeded (%d): "
                 "consider raising CL_MAX_BLOCKS", CL_MAX_BLOCKS);
    }

    CL_GC_PROTECT(body);

    /* Push block info (for compile-time lookup by return-from) */
    bi = &c->blocks[c->block_count++];
    bi->tag = tag;
    bi->n_patches = 0;
    bi->uses_nlx = needs_nlx;
    bi->dyn_depth = c->special_depth;

    /* CRITICAL: push the BLOCK frame BEFORE compile_nontail_body so the
     * BLOCK postlude drains AFTER any PROGN_ITER frames the body
     * iteration pushes.  Otherwise BLOCK_LOCAL would emit its closing
     * STORE/POP/LOAD before non-tail body forms had been compiled. */
    if (needs_nlx) {
        int tag_idx, block_push_pos;
        int saved_tail = c->in_tail;

        bi->result_slot = -1;

        tag_idx = cl_add_constant(c, tag);
        cl_emit(c, OP_BLOCK_PUSH);
        cl_emit_u16(c, (uint16_t)tag_idx);
        block_push_pos = c->code_pos;
        cl_emit_i32(c, 0); /* placeholder offset to landing */

        /* Disable tail calls inside NLX blocks — the body must
         * return to OP_BLOCK_POP so the NLX frame is properly popped.
         * Without this, tail calls leak NLX BLOCK frames. */
        c->in_tail = 0;

        tf = cl_tail_push(c);
        tf->kind = CL_TAIL_BLOCK_NLX;
        tf->saved_block_count = saved_block_count;
        tf->saved_tail = saved_tail;
        tf->block_push_pos = block_push_pos;

        {
            CL_Obj tail = compile_nontail_body(c, body);
            CL_GC_UNPROTECT(1);
            return tail;  /* CL_NIL means caller emits OP_NIL for empty body */
        }
    } else {
        CL_CompEnv *env = c->env;
        int saved_local_count = env->local_count;
        int result_slot;

        result_slot = env->local_count;
        env->locals[result_slot] = CL_NIL;  /* Clear stale binding */
        env->local_count++;
        if (env->local_count > env->max_locals)
            env->max_locals = env->local_count;
        bi->result_slot = result_slot;

        tf = cl_tail_push(c);
        tf->kind = CL_TAIL_BLOCK_LOCAL;
        tf->saved_block_count = saved_block_count;
        tf->saved_local_count = saved_local_count;
        tf->result_slot = result_slot;

        {
            CL_Obj tail = compile_nontail_body(c, body);
            CL_GC_UNPROTECT(1);
            return tail;
        }
    }
}

/* Helper: trampoline prelude common to RETURN-FROM and RETURN.
 * `tag` is the block tag (possibly NIL for RETURN), `val_form` is the value
 * (CL_NIL means "no value form supplied — emit OP_NIL inline"), and
 * `has_value` distinguishes (return-from X) from (return-from X NIL).
 * Returns the value form for the trampoline, or CL_NIL when the caller
 * should emit OP_NIL (then drain). */
static CL_Obj return_from_prelude(CL_Compiler *c, CL_Obj tag,
                                  CL_Obj val_form, int has_value)
{
    int saved_tail = c->in_tail;
    int i, tag_idx;
    CL_TailFrame *tf;

    for (i = c->block_count - 1; i >= 0; i--) {
        if (c->blocks[i].tag == tag) {
            CL_BlockInfo *bi = &c->blocks[i];
            c->in_tail = 0;
            tf = cl_tail_push(c);
            if (bi->uses_nlx) {
                tag_idx = cl_add_constant(c, tag);
                tf->kind = CL_TAIL_RETURN_FROM_NLX;
                tf->saved_tail = saved_tail;
                tf->tag_idx = tag_idx;
            } else {
                tf->kind = CL_TAIL_RETURN_FROM_LOCAL;
                tf->saved_tail = saved_tail;
                tf->bi_index = i;
                tf->unwind_count = c->special_depth - bi->dyn_depth;
            }
            return has_value ? val_form : CL_NIL;
        }
    }

    for (i = 0; i < c->outer_block_count; i++) {
        if (c->outer_blocks[i] == tag) {
            c->in_tail = 0;
            tag_idx = cl_add_constant(c, tag);
            tf = cl_tail_push(c);
            tf->kind = CL_TAIL_OUTER_RETURN_FROM;
            tf->saved_tail = saved_tail;
            tf->tag_idx = tag_idx;
            return has_value ? val_form : CL_NIL;
        }
    }

    cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
             CL_NULL_P(tag) ? "NIL" : cl_symbol_name(tag));
    return CL_NIL;
}

CL_Obj compile_return_from(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj rest_after_tag = cl_cdr(cl_cdr(form));
    int has_value = !CL_NULL_P(rest_after_tag);
    CL_Obj val_form = has_value ? cl_car(rest_after_tag) : CL_NIL;
    return return_from_prelude(c, tag, val_form, has_value);
}

CL_Obj compile_return(CL_Compiler *c, CL_Obj form)
{
    /* (return [value]) => return-from NIL */
    CL_Obj rest = cl_cdr(form);
    int has_value = !CL_NULL_P(rest);
    CL_Obj val_form = has_value ? cl_car(rest) : CL_NIL;
    return return_from_prelude(c, CL_NIL, val_form, has_value);
}

/* Emit deferred postlude for a block / return-from tail frame.  Called
 * from compile_expr's drain loop in LIFO order. */
void emit_block_or_return_postlude(CL_Compiler *c, CL_TailFrame *tf)
{
    switch ((CL_TailKind)tf->kind) {
    case CL_TAIL_BLOCK_NLX: {
        int jmp_pos;
        cl_emit(c, OP_BLOCK_POP);
        jmp_pos = cl_emit_jump(c, OP_JMP);
        /* Landing: longjmp arrives here with result on stack */
        cl_patch_jump(c, tf->block_push_pos);
        /* End: both paths converge with result on TOS */
        cl_patch_jump(c, jmp_pos);
        c->in_tail = tf->saved_tail;
        c->block_count = tf->saved_block_count;
        break;
    }
    case CL_TAIL_BLOCK_LOCAL: {
        int i;
        CL_BlockInfo *bi = &c->blocks[tf->saved_block_count];  /* this block's slot */
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)tf->result_slot);
        cl_emit(c, OP_POP);
        for (i = 0; i < bi->n_patches; i++)
            cl_patch_jump(c, bi->exit_patches[i]);
        cl_emit(c, OP_LOAD);
        cl_emit(c, (uint8_t)tf->result_slot);
        cl_env_clear_boxed(c->env, tf->saved_local_count);
        c->env->local_count = tf->saved_local_count;
        c->block_count = tf->saved_block_count;
        break;
    }
    case CL_TAIL_RETURN_FROM_LOCAL: {
        CL_BlockInfo *bi = &c->blocks[tf->bi_index];
        c->in_tail = tf->saved_tail;
        if (tf->unwind_count > 0) {
            cl_emit(c, OP_DYNUNBIND);
            cl_emit(c, (uint8_t)tf->unwind_count);
        }
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)bi->result_slot);
        cl_emit(c, OP_POP);
        if (bi->n_patches < CL_MAX_BLOCK_PATCHES)
            bi->exit_patches[bi->n_patches++] = cl_emit_jump(c, OP_JMP);
        break;
    }
    case CL_TAIL_RETURN_FROM_NLX:
    case CL_TAIL_OUTER_RETURN_FROM:
        c->in_tail = tf->saved_tail;
        cl_emit(c, OP_BLOCK_RETURN);
        cl_emit_u16(c, (uint16_t)tf->tag_idx);
        break;
    default:
        break;  /* not a block/return frame */
    }
}

/* --- Tagbody / Go --- */

static int is_tagbody_tag(CL_Obj form)
{
    /* Tags are symbols or integers (CL spec) */
    return CL_SYMBOL_P(form) || CL_FIXNUM_P(form);
}

void compile_tagbody(CL_Compiler *c, CL_Obj form)
{
    CL_Obj body = cl_cdr(form);
    int saved_tagbody_count = c->tagbody_count;
    int saved_tail = c->in_tail;
    CL_TagbodyInfo *tb;
    CL_Obj cursor;
    int i;
    int needs_nlx = tree_has_closure_forms(body);

    /* Hard cap: writing past tagbodies[] would clobber adjacent fields
     * (silent corruption that surfaces later as wild memory access).
     * Raise a clean compile error instead. */
    if (c->tagbody_count >= CL_MAX_TAGBODIES) {
        cl_error(CL_ERR_OVERFLOW,
                 "TAGBODY nesting depth exceeded (%d): "
                 "consider raising CL_MAX_TAGBODIES", CL_MAX_TAGBODIES);
    }

    c->in_tail = 0;

    /* Push tagbody info */
    tb = &c->tagbodies[c->tagbody_count++];
    tb->n_tags = 0;
    tb->uses_nlx = needs_nlx;

    /* Create unique tagbody identifier for NLX */
    if (needs_nlx)
        tb->id = cl_cons(CL_NIL, CL_NIL);  /* fresh cons cell as unique id */
    else
        tb->id = CL_NIL;

    /* First pass: collect all tags */
    cursor = body;
    while (!CL_NULL_P(cursor)) {
        CL_Obj item = cl_car(cursor);
        if (is_tagbody_tag(item)) {
            CL_TagInfo *ti = &tb->tags[tb->n_tags++];
            ti->tag = item;
            ti->code_pos = -1;
            ti->n_forward = 0;
        }
        cursor = cl_cdr(cursor);
    }

    if (needs_nlx) {
        /* NLX path: set up NLX frame, compile body, then dispatch landing */
        int id_idx, push_pos, jmp_pos;

        id_idx = cl_add_constant(c, tb->id);
        cl_emit(c, OP_TAGBODY_PUSH);
        cl_emit_u16(c, (uint16_t)id_idx);
        push_pos = c->code_pos;
        cl_emit_i32(c, 0);  /* placeholder offset to landing */

        /* Compile body (same as non-NLX) */
        cursor = body;
        while (!CL_NULL_P(cursor)) {
            CL_Obj item = cl_car(cursor);
            if (is_tagbody_tag(item)) {
                for (i = 0; i < tb->n_tags; i++) {
                    if (tb->tags[i].tag == item) {
                        int j;
                        tb->tags[i].code_pos = c->code_pos;
                        for (j = 0; j < tb->tags[i].n_forward; j++)
                            cl_patch_jump(c, tb->tags[i].forward_patches[j]);
                        tb->tags[i].n_forward = 0;
                        break;
                    }
                }
            } else {
                compile_expr(c, item);
                cl_emit(c, OP_POP);
            }
            cursor = cl_cdr(cursor);
        }

        /* Normal exit: pop NLX frame, jump past dispatch */
        cl_emit(c, OP_TAGBODY_POP);
        jmp_pos = cl_emit_jump(c, OP_JMP);

        /* Landing: longjmp from cross-closure GO arrives here.
         * TOS = tag index (fixnum). Dispatch to the right tag position.
         * Each JTRUE lands in a per-tag shim that pops the tag index
         * before jumping to the tag's body, so the body resumes with the
         * stack at the same height as it was at OP_TAGBODY_PUSH (matching
         * the fall-through / local-go entry conditions). */
        {
            int shim_jumps[CL_MAX_TAGBODY_TAGS];
            int default_jmp;

            cl_patch_jump(c, push_pos);

            for (i = 0; i < tb->n_tags; i++) {
                if (tb->tags[i].code_pos >= 0) {
                    cl_emit(c, OP_DUP);
                    cl_emit_const(c, CL_MAKE_FIXNUM(i));
                    cl_emit(c, OP_EQ);
                    shim_jumps[i] = cl_emit_jump(c, OP_JTRUE);
                } else {
                    shim_jumps[i] = -1;
                }
            }
            /* Default fallthrough: pop tag index, jump past shims. */
            cl_emit(c, OP_POP);
            default_jmp = cl_emit_jump(c, OP_JMP);

            /* Per-tag shims: pop tag index then jump to tag body. */
            for (i = 0; i < tb->n_tags; i++) {
                if (shim_jumps[i] >= 0) {
                    cl_patch_jump(c, shim_jumps[i]);
                    cl_emit(c, OP_POP);
                    cl_emit_loop_jump(c, OP_JMP, tb->tags[i].code_pos);
                }
            }

            cl_patch_jump(c, default_jmp);
        }

        /* Past dispatch */
        cl_patch_jump(c, jmp_pos);

        /* tagbody returns NIL */
        cl_emit(c, OP_NIL);
    } else {
        /* Local path (non-NLX): efficient local jumps */
        cursor = body;
        while (!CL_NULL_P(cursor)) {
            CL_Obj item = cl_car(cursor);
            if (is_tagbody_tag(item)) {
                for (i = 0; i < tb->n_tags; i++) {
                    if (tb->tags[i].tag == item) {
                        int j;
                        tb->tags[i].code_pos = c->code_pos;
                        for (j = 0; j < tb->tags[i].n_forward; j++)
                            cl_patch_jump(c, tb->tags[i].forward_patches[j]);
                        tb->tags[i].n_forward = 0;
                        break;
                    }
                }
            } else {
                compile_expr(c, item);
                cl_emit(c, OP_POP);
            }
            cursor = cl_cdr(cursor);
        }

        /* tagbody returns NIL */
        cl_emit(c, OP_NIL);
    }

    /* Restore */
    c->tagbody_count = saved_tagbody_count;
    c->in_tail = saved_tail;
}

void compile_go(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    int i, j;

    /* Search tagbodies innermost-first for matching tag */
    for (i = c->tagbody_count - 1; i >= 0; i--) {
        CL_TagbodyInfo *tb = &c->tagbodies[i];
        for (j = 0; j < tb->n_tags; j++) {
            if (tb->tags[j].tag == tag) {
                CL_TagInfo *ti = &tb->tags[j];
                if (tb->uses_nlx) {
                    /* NLX-based tagbody: emit OP_TAGBODY_GO */
                    int id_idx = cl_add_constant(c, tb->id);
                    cl_emit_const(c, CL_MAKE_FIXNUM(j));  /* tag index */
                    cl_emit(c, OP_TAGBODY_GO);
                    cl_emit_u16(c, (uint16_t)id_idx);
                } else if (ti->code_pos >= 0) {
                    /* Backward jump to known position */
                    cl_emit_loop_jump(c, OP_JMP, ti->code_pos);
                } else {
                    /* Forward jump — patch later */
                    if (ti->n_forward < CL_MAX_BLOCK_PATCHES)
                        ti->forward_patches[ti->n_forward++] = cl_emit_jump(c, OP_JMP);
                }
                return;
            }
        }
    }

    /* Check outer tagbody tags (from enclosing scopes, for cross-closure go) */
    for (i = 0; i < c->outer_tag_count; i++) {
        if (c->outer_tags[i].tag == tag) {
            int id_idx = cl_add_constant(c, c->outer_tags[i].tagbody_id);
            cl_emit_const(c, CL_MAKE_FIXNUM(c->outer_tags[i].tag_index));
            cl_emit(c, OP_TAGBODY_GO);
            cl_emit_u16(c, (uint16_t)id_idx);
            return;
        }
    }

    cl_error(CL_ERR_GENERAL, "GO: no tag named %s",
             CL_SYMBOL_P(tag) ? cl_symbol_name(tag) : "?");
}

/* --- Catch --- */

void compile_catch(CL_Compiler *c, CL_Obj form)
{
    /* (catch tag body...) */
    CL_Obj tag_form = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int saved_tail = c->in_tail;
    int catch_pos, jmp_pos;

    c->in_tail = 0;

    /* Compile tag expression (push tag value on stack) */
    compile_expr(c, tag_form);

    /* OP_CATCH offset_to_landing: setjmp; on throw jumps to landing */
    cl_emit(c, OP_CATCH);
    catch_pos = c->code_pos;
    cl_emit_i32(c, 0); /* placeholder */

    /* Compile body (progn) */
    c->in_tail = saved_tail;
    if (CL_NULL_P(body))
        cl_emit(c, OP_NIL);
    else
        compile_progn(c, body);

    /* OP_UNCATCH: pop catch frame (normal exit) */
    cl_emit(c, OP_UNCATCH);

    /* JMP past the throw landing */
    jmp_pos = cl_emit_jump(c, OP_JMP);

    /* [landing]: throw arrives here with result on stack */
    cl_patch_jump(c, catch_pos);

    /* [past_landing]: both paths converge, result is on stack */
    cl_patch_jump(c, jmp_pos);
}

/* --- Unwind-protect --- */

void compile_unwind_protect(CL_Compiler *c, CL_Obj form)
{
    /* (unwind-protect protected-form cleanup1 cleanup2 ...)
     *
     * Per CLHS: unwind-protect returns the values that result from
     * evaluating protected-form. We must capture the full multiple-value
     * state of protected-form across cleanup-form evaluation — single-value
     * save/restore would silently drop secondary values, which breaks
     * common idioms like (mv-bind (v p) (with-lock-held (l) (gethash k h)))
     * where p (presentp) is the second value. */
    CL_Obj protected_form = cl_car(cl_cdr(form));
    CL_Obj cleanup_forms = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int uwprot_pos, jmp_pos;
    int list_slot;
    CL_Obj vl_sym;
    int vl_idx;

    c->in_tail = 0;

    /* Allocate slot to hold the saved values list */
    list_slot = env->local_count;
    env->locals[list_slot] = CL_NIL;  /* Clear stale binding */
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* OP_UWPROT offset_to_cleanup_landing */
    cl_emit(c, OP_UWPROT);
    uwprot_pos = c->code_pos;
    cl_emit_i32(c, 0); /* placeholder */

    /* Compile protected form (leaves primary on stack, MV buffer holds rest) */
    compile_expr(c, protected_form);

    /* OP_MV_TO_LIST: pop primary, build full list of all values, push list */
    cl_emit(c, OP_MV_TO_LIST);

    /* Save the list in slot */
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)list_slot);
    cl_emit(c, OP_POP);

    /* OP_UWPOP: normal exit, pop frame, clear pending */
    cl_emit(c, OP_UWPOP);

    /* JMP to cleanup_start (skip the landing) */
    jmp_pos = cl_emit_jump(c, OP_JMP);

    /* [cleanup_landing]: longjmp arrives here */
    cl_patch_jump(c, uwprot_pos);

    /* [cleanup_start]: both paths merge */
    cl_patch_jump(c, jmp_pos);

    /* Compile cleanup forms, discarding results */
    {
        CL_Obj cf = cleanup_forms;
        CL_GC_PROTECT(cf);
        while (!CL_NULL_P(cf)) {
            compile_expr(c, cl_car(cf));
            cl_emit(c, OP_POP);
            cf = cl_cdr(cf);
        }
        CL_GC_UNPROTECT(1);
    }

    /* OP_UWRETHROW: if pending throw, re-initiate (never returns); else nop */
    cl_emit(c, OP_UWRETHROW);

    /* Restore MV state by calling VALUES-LIST on saved list. The call leaves
     * the primary on the stack and sets cl_mv_count / cl_mv_values. */
    vl_sym = cl_intern_in("VALUES-LIST", 11, cl_package_cl);
    vl_idx = cl_add_constant(c, vl_sym);
    cl_emit(c, OP_FLOAD);
    cl_emit_u16(c, (uint16_t)vl_idx);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)list_slot);
    cl_emit(c, OP_CALL);
    cl_emit(c, 1);

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore */
    c->in_tail = saved_tail;
    env->local_count = saved_local_count;
}

/* --- Flet / Labels --- */

CL_Obj compile_flet(CL_Compiler *c, CL_Obj form)
{
    /* (flet ((name (params) body...) ...) body...)
     *
     * Trampoline-aware: prelude compiles each local function and pushes
     * a tail frame that restores local + local-fun counts in the
     * postlude.  Body's tail form returns to the driver. */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_fun_count = env->local_fun_count;
    int saved_tail = c->in_tail;
    CL_TailFrame *tf;

    /* Phase 1: compile each function in outer scope, store in anonymous slots */
    {
        CL_Obj b = bindings;
        CL_GC_PROTECT(b);
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj fname = cl_car(binding);
            CL_Obj lambda_list = cl_car(cl_cdr(binding));
            CL_Obj fbody = cl_cdr(cl_cdr(binding));
            CL_Obj lambda_form;
            int slot;

            /* Build (lambda (params) (block name body...)) per CL spec:
             * flet functions have an implicit block named after the function */
            {
                CL_Obj block_form = cl_cons(SYM_BLOCK, cl_cons(fname, fbody));
                lambda_form = cl_cons(SYM_LAMBDA,
                               cl_cons(lambda_list, cl_cons(block_form, CL_NIL)));
            }
            CL_GC_PROTECT(lambda_form);

            c->in_tail = 0;
            compile_expr(c, lambda_form);
            CL_GC_UNPROTECT(1);

            /* Allocate anonymous slot and store */
            slot = env->local_count;
            env->local_count++;
            if (env->local_count > env->max_locals)
                env->max_locals = env->local_count;
            /* Store CL_NIL in locals[] to keep it anonymous */
            env->locals[slot] = CL_NIL;

            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);

            /* Register in function namespace */
            /* Re-read fname from protected cursor b: compile_expr above may
             * have triggered compaction, staling the pre-compile snapshot. */
            fname = cl_car(cl_car(b));
            cl_env_add_local_fun(env, fname, slot);

            b = cl_cdr(b);
        }
        CL_GC_UNPROTECT(1);
    }

    c->in_tail = saved_tail;

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_FLET;
    tf->saved_local_count = saved_local_count;
    tf->saved_block_count = saved_fun_count;  /* reused: saved_local_fun_count */
    tf->saved_tail = saved_tail;
    return compile_body_tail(c, body);
}

CL_Obj compile_labels(CL_Compiler *c, CL_Obj form)
{
    /* (labels ((name (params) body...) ...) body...)
     *
     * Like flet but functions can reference each other (mutual recursion).
     * We pre-allocate local slots for all function names first, then compile
     * each function body (which can now resolve cross-references via the
     * local function namespace).
     *
     * Trampoline-aware: same shape as compile_flet, postlude restores env. */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_fun_count = env->local_fun_count;
    int saved_tail = c->in_tail;

    /* Phase 1: pre-allocate slots, initialize to NIL, box them, and register
     * all function names. Boxing is required so that closures compiled in
     * phase 2 capture a reference to the box cell (not a copy of NIL). */
    {
        CL_Obj b = bindings;
        int n = 0;
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj fname = cl_car(binding);
            int slot = env->local_count;
            env->local_count++;
            if (env->local_count > env->max_locals)
                env->max_locals = env->local_count;
            env->locals[slot] = CL_NIL;  /* anonymous slot */

            /* Initialize slot to NIL */
            cl_emit(c, OP_CONST);
            cl_emit_u16(c, cl_add_constant(c, CL_NIL));
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);

            /* Register in function namespace */
            cl_env_add_local_fun(env, fname, slot);

            n++;
            b = cl_cdr(b);
        }

        /* Box all slots so closures capture by reference */
        {
            int i;
            for (i = 0; i < n; i++) {
                int slot = saved_local_count + i;
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)slot);
                cl_emit(c, OP_MAKE_CELL);
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)slot);
                cl_emit(c, OP_POP);
                env->boxed[slot] = 1;
            }
        }
    }

    /* Phase 2: compile each function and store in its pre-allocated (boxed) slot */
    {
        CL_Obj b = bindings;
        int slot = saved_local_count;  /* first allocated slot */
        CL_GC_PROTECT(b);
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj fname = cl_car(binding);
            CL_Obj lambda_list = cl_car(cl_cdr(binding));
            CL_Obj fbody = cl_cdr(cl_cdr(binding));
            CL_Obj lambda_form;

            /* Build (lambda (params) (block name body...)) per CL spec:
             * labels functions have an implicit block named after the function */
            {
                CL_Obj block_form = cl_cons(SYM_BLOCK, cl_cons(fname, fbody));
                lambda_form = cl_cons(SYM_LAMBDA,
                               cl_cons(lambda_list, cl_cons(block_form, CL_NIL)));
            }
            CL_GC_PROTECT(lambda_form);

            c->in_tail = 0;
            compile_expr(c, lambda_form);
            CL_GC_UNPROTECT(1);

            /* Store in boxed slot */
            cl_emit(c, OP_CELL_SET_LOCAL);
            cl_emit(c, (uint8_t)slot);
            cl_emit(c, OP_POP);

            slot++;
            b = cl_cdr(b);
        }
        CL_GC_UNPROTECT(1);
    }

    /* Phase 3: compile body via trampoline */
    c->in_tail = saved_tail;
    {
        CL_TailFrame *tf = cl_tail_push(c);
        tf->kind = CL_TAIL_LABELS;
        tf->saved_local_count = saved_local_count;
        tf->saved_block_count = saved_fun_count;  /* reused: saved_local_fun_count */
        tf->saved_tail = saved_tail;
        return compile_body_tail(c, body);
    }
}

/* --- Loop forms --- */

void compile_dolist(CL_Compiler *c, CL_Obj form)
{
    /* (dolist (var list-form [result-form]) body...) */
    CL_Obj binding = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_Obj var = cl_car(binding);
    CL_Obj list_form = cl_car(cl_cdr(binding));
    CL_Obj result_form = cl_car(cl_cdr(cl_cdr(binding)));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int saved_block_count = c->block_count;
    int var_slot, iter_slot, result_slot;
    int loop_start, jnil_pos;
    int var_boxed = 0;
    CL_BlockInfo *bi;
    int i;

    c->in_tail = 0;

    /* Allocate iter slot FIRST — list-form must be evaluated before
     * the loop variable is bound (CL spec), so we compile list-form
     * into iter_slot while the outer env is still visible. */
    iter_slot = env->local_count;
    env->locals[iter_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile list-form BEFORE adding var to env.
     * This ensures (dolist (x (f x)) ...) evaluates (f x) using
     * the OUTER binding of x, not the new loop variable. */
    compile_expr(c, list_form);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_POP);

    /* NOW allocate var slot — after list-form is compiled */
    var_slot = cl_env_add_local(env, var);

    /* Check if loop var is captured in body (always mutated by loop) */
    {
        uint8_t captured = 0, mutated = 0;
        CL_Obj cur = body;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), &var, 1, &mutated, &captured, 0);
            cur = cl_cdr(cur);
        }
        if (!CL_NULL_P(cl_cdr(cl_cdr(binding))))
            scan_body_for_boxing(result_form, &var, 1, &mutated, &captured, 0);
        var_boxed = captured;
    }

    /* If boxed, initialize cell in var_slot */
    if (var_boxed) {
        cl_emit(c, OP_NIL);
        cl_emit(c, OP_MAKE_CELL);
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)var_slot);
        cl_emit(c, OP_POP);
        env->boxed[var_slot] = 1;
    }

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->locals[result_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;
    bi->uses_nlx = 0;
    bi->dyn_depth = c->special_depth;

    /* loop_start: LOAD iter_slot, JNIL -> end */
    loop_start = c->code_pos;
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    jnil_pos = cl_emit_jump(c, OP_JNIL);

    /* Set var = (car iter) */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_CAR);
    if (var_boxed) {
        cl_emit(c, OP_CELL_SET_LOCAL);
    } else {
        cl_emit(c, OP_STORE);
    }
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* Advance iter = (cdr iter) */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_CDR);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_POP);

    /* CLHS 3.6: the body of DOLIST is an implicit TAGBODY.  Strip leading
       declarations (they don't belong inside tagbody), then compile a
       single (tagbody body...) form so go-tags in the body resolve and
       (go tag) works.  TAGBODY returns NIL, which we POP — matching the
       existing "body values are discarded" semantics. */
    {
        CL_Obj b = body;
        CL_Obj tb_forms = CL_NIL, tb_tail = CL_NIL;
        CL_Obj tagbody_form;
        CL_GC_PROTECT(b);
        CL_GC_PROTECT(tb_forms);
        CL_GC_PROTECT(tb_tail);
        while (!CL_NULL_P(b)) {
            CL_Obj form = cl_car(b);
            if (CL_CONS_P(form) && cl_car(form) == SYM_DECLARE) {
                b = cl_cdr(b);
                continue;
            }
            {
                CL_Obj cell = cl_cons(form, CL_NIL);
                if (CL_NULL_P(tb_forms)) {
                    tb_forms = cell;
                } else {
                    ((CL_Cons *)CL_OBJ_TO_PTR(tb_tail))->cdr = cell;
                }
                tb_tail = cell;
            }
            b = cl_cdr(b);
        }
        tagbody_form = cl_cons(SYM_TAGBODY, tb_forms);
        CL_GC_PROTECT(tagbody_form);
        compile_expr(c, tagbody_form);
        cl_emit(c, OP_POP);
        CL_GC_UNPROTECT(4);
    }

    /* Backward jump to loop_start */
    cl_emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    cl_patch_jump(c, jnil_pos);

    /* CL spec: var is NIL during result-form evaluation */
    cl_emit(c, OP_NIL);
    if (var_boxed) {
        cl_emit(c, OP_CELL_SET_LOCAL);
    } else {
        cl_emit(c, OP_STORE);
    }
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* Compile result-form or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(binding)))) {
        compile_expr(c, result_form);
    } else {
        cl_emit(c, OP_NIL);
    }
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
    cl_emit(c, OP_POP);

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        cl_patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

void compile_dotimes(CL_Compiler *c, CL_Obj form)
{
    /* (dotimes (var count-form [result-form]) body...) */
    CL_Obj binding = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_Obj var = cl_car(binding);
    CL_Obj count_form = cl_car(cl_cdr(binding));
    CL_Obj result_form = cl_car(cl_cdr(cl_cdr(binding)));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int saved_block_count = c->block_count;
    int var_slot, limit_slot, result_slot;
    int loop_start, jtrue_pos;
    int var_boxed = 0;
    CL_BlockInfo *bi;
    int i;

    c->in_tail = 0;

    /* Allocate limit slot FIRST — count-form must be evaluated before
     * the loop variable is bound (CL spec). */
    limit_slot = env->local_count;
    env->locals[limit_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile count-form BEFORE adding var to env.
     * This ensures (dotimes (x (f x)) ...) evaluates (f x) using
     * the OUTER binding of x, not the new loop variable. */
    compile_expr(c, count_form);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)limit_slot);
    cl_emit(c, OP_POP);

    /* NOW allocate var slot — after count-form is compiled */
    var_slot = cl_env_add_local(env, var);

    /* Check if loop var is captured in body (always mutated by loop) */
    {
        uint8_t captured = 0, mutated = 0;
        CL_Obj cur = body;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), &var, 1, &mutated, &captured, 0);
            cur = cl_cdr(cur);
        }
        if (!CL_NULL_P(cl_cdr(cl_cdr(binding))))
            scan_body_for_boxing(result_form, &var, 1, &mutated, &captured, 0);
        var_boxed = captured;
    }

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->locals[result_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;
    bi->uses_nlx = 0;
    bi->dyn_depth = c->special_depth;

    /* var = 0 (boxed: wrap in cell) */
    cl_emit_const(c, CL_MAKE_FIXNUM(0));
    if (var_boxed) {
        cl_emit(c, OP_MAKE_CELL);
        env->boxed[var_slot] = 1;
    }
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* loop_start: LOAD var, LOAD limit, GE, JTRUE -> end */
    loop_start = c->code_pos;
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)var_slot);
    if (var_boxed) cl_emit(c, OP_CELL_REF);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)limit_slot);
    cl_emit(c, OP_GE);
    jtrue_pos = cl_emit_jump(c, OP_JTRUE);

    /* CLHS 3.6: the body of DOTIMES is an implicit TAGBODY. */
    {
        CL_Obj b = body;
        CL_Obj tb_forms = CL_NIL, tb_tail = CL_NIL;
        CL_Obj tagbody_form;
        CL_GC_PROTECT(b);
        CL_GC_PROTECT(tb_forms);
        CL_GC_PROTECT(tb_tail);
        while (!CL_NULL_P(b)) {
            CL_Obj form = cl_car(b);
            if (CL_CONS_P(form) && cl_car(form) == SYM_DECLARE) {
                b = cl_cdr(b);
                continue;
            }
            {
                CL_Obj cell = cl_cons(form, CL_NIL);
                if (CL_NULL_P(tb_forms)) {
                    tb_forms = cell;
                } else {
                    ((CL_Cons *)CL_OBJ_TO_PTR(tb_tail))->cdr = cell;
                }
                tb_tail = cell;
            }
            b = cl_cdr(b);
        }
        tagbody_form = cl_cons(SYM_TAGBODY, tb_forms);
        CL_GC_PROTECT(tagbody_form);
        compile_expr(c, tagbody_form);
        cl_emit(c, OP_POP);
        CL_GC_UNPROTECT(4);
    }

    /* Increment: var = var + 1 */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)var_slot);
    if (var_boxed) cl_emit(c, OP_CELL_REF);
    cl_emit_const(c, CL_MAKE_FIXNUM(1));
    cl_emit(c, OP_ADD);
    if (var_boxed) {
        cl_emit(c, OP_CELL_SET_LOCAL);
    } else {
        cl_emit(c, OP_STORE);
    }
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* Backward jump to loop_start */
    cl_emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    cl_patch_jump(c, jtrue_pos);

    /* Compile result-form or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(binding)))) {
        compile_expr(c, result_form);
    } else {
        cl_emit(c, OP_NIL);
    }
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
    cl_emit(c, OP_POP);

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        cl_patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

void compile_do(CL_Compiler *c, CL_Obj form)
{
    /* (do ((var init [step])...) (end-test result...) body...) */
    CL_Obj var_clauses = cl_car(cl_cdr(form));
    CL_Obj end_clause = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_Obj end_test = cl_car(end_clause);
    CL_Obj result_forms = cl_cdr(end_clause);
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int saved_block_count = c->block_count;

    /* Parse bindings into arrays */
    CL_Obj vars[CL_MAX_BINDINGS];
    CL_Obj inits[CL_MAX_BINDINGS];
    CL_Obj steps[CL_MAX_BINDINGS];  /* CL_NIL if no step form */
    int has_step[CL_MAX_BINDINGS];
    uint8_t do_boxed[CL_MAX_BINDINGS];
    int n = 0;
    int i;
    int loop_start, jtrue_pos;
    int result_slot;
    CL_BlockInfo *bi;

    {
        CL_Obj vc = var_clauses;
        while (!CL_NULL_P(vc) && n < CL_MAX_BINDINGS) {
            CL_Obj clause = cl_car(vc);
            vars[n] = cl_car(clause);
            inits[n] = cl_car(cl_cdr(clause));
            if (!CL_NULL_P(cl_cdr(cl_cdr(clause)))) {
                steps[n] = cl_car(cl_cdr(cl_cdr(clause)));
                has_step[n] = 1;
            } else {
                steps[n] = CL_NIL;
                has_step[n] = 0;
            }
            n++;
            vc = cl_cdr(vc);
        }
    }

    /* Check which do vars are captured (all stepped vars are mutated by loop) */
    {
        uint8_t mutated[CL_MAX_BINDINGS], captured[CL_MAX_BINDINGS];
        CL_Obj cur;
        memset(mutated, 0, (size_t)n);
        memset(captured, 0, (size_t)n);
        memset(do_boxed, 0, (size_t)n);
        /* Scan body, end-test, result forms, and step forms */
        cur = body;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n, mutated, captured, 0);
            cur = cl_cdr(cur);
        }
        scan_body_for_boxing(end_test, vars, n, mutated, captured, 0);
        cur = result_forms;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n, mutated, captured, 0);
            cur = cl_cdr(cur);
        }
        for (i = 0; i < n; i++) {
            scan_body_for_boxing(steps[i], vars, n, mutated, captured, 0);
        }
        /* Stepped vars are always mutated; all do vars with step forms need boxing if captured */
        for (i = 0; i < n; i++) {
            do_boxed[i] = (has_step[i] && captured[i]) ? 1 :
                          (mutated[i] && captured[i]) ? 1 : 0;
        }
    }

    c->in_tail = 0;

    /* GC-protect — compile_expr below can compact, making inits[]/vars[]/steps[]/
     * end_test/result_forms/body stale.  var_clauses is re-walked by each cursor. */
    CL_GC_PROTECT(var_clauses);
    CL_GC_PROTECT(body);
    CL_GC_PROTECT(end_test);
    CL_GC_PROTECT(result_forms);

    /* Parallel init: evaluate all init forms onto stack.
     * Walk var_clauses with a protected cursor — compile_expr can compact inits[] stale. */
    {
        CL_Obj vc = var_clauses;
        CL_GC_PROTECT(vc);
        for (i = 0; i < n; i++) {
            compile_expr(c, cl_car(cl_cdr(cl_car(vc))));
            vc = cl_cdr(vc);
        }
        CL_GC_UNPROTECT(1);
    }

    /* Register all vars as locals — re-walk var_clauses; compile_expr above made vars[] stale */
    {
        CL_Obj vc = var_clauses;
        for (i = 0; i < n; i++) {
            cl_env_add_local(env, cl_car(cl_car(vc)));
            vc = cl_cdr(vc);
        }
    }

    /* Store back-to-front */
    for (i = n - 1; i >= 0; i--) {
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)(saved_local_count + i));
        cl_emit(c, OP_POP);
    }

    /* Box vars that need it */
    for (i = 0; i < n; i++) {
        if (do_boxed[i]) {
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)(saved_local_count + i));
            cl_emit(c, OP_MAKE_CELL);
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)(saved_local_count + i));
            cl_emit(c, OP_POP);
            env->boxed[saved_local_count + i] = 1;
        }
    }

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->locals[result_slot] = CL_NIL;  /* Clear stale binding */
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;
    bi->uses_nlx = 0;
    bi->dyn_depth = c->special_depth;

    /* loop_start: compile end-test, JTRUE -> end */
    loop_start = c->code_pos;
    compile_expr(c, end_test);
    jtrue_pos = cl_emit_jump(c, OP_JTRUE);

    /* Compile body as an implicit tagbody per CLHS 6.1.6. */
    {
        CL_Obj b = body;
        CL_Obj clean = CL_NIL, tail = CL_NIL;
        CL_Obj tb_form;
        /* GC-protect b and clean — cl_cons inside the loop can compact */
        CL_GC_PROTECT(b);
        CL_GC_PROTECT(clean);
        CL_GC_PROTECT(tail);
        while (!CL_NULL_P(b)) {
            CL_Obj bform = cl_car(b);
            if (!(CL_CONS_P(bform) && cl_car(bform) == SYM_DECLARE)) {
                CL_Obj cell = cl_cons(bform, CL_NIL);
                if (CL_NULL_P(clean)) { clean = cell; tail = cell; }
                else {
                    ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                    tail = cell;
                }
            }
            b = cl_cdr(b);
        }
        tb_form = cl_cons(SYM_TAGBODY, clean);
        CL_GC_PROTECT(tb_form);
        compile_expr(c, tb_form);
        cl_emit(c, OP_POP);  /* tagbody always leaves NIL */
        CL_GC_UNPROTECT(4);  /* tb_form, tail, clean, b */
    }

    /* Parallel step: evaluate all step forms (or load current value).
     * Walk var_clauses with a protected cursor — compile_expr can compact steps[] stale. */
    {
        CL_Obj vc = var_clauses;
        CL_GC_PROTECT(vc);
        for (i = 0; i < n; i++) {
            if (has_step[i]) {
                CL_Obj clause = cl_car(vc);
                compile_expr(c, cl_car(cl_cdr(cl_cdr(clause))));
            } else {
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)(saved_local_count + i));
                if (do_boxed[i]) cl_emit(c, OP_CELL_REF);
            }
            vc = cl_cdr(vc);
        }
        CL_GC_UNPROTECT(1);
    }

    /* Store all back-to-front */
    for (i = n - 1; i >= 0; i--) {
        if (do_boxed[i]) {
            cl_emit(c, OP_CELL_SET_LOCAL);
        } else {
            cl_emit(c, OP_STORE);
        }
        cl_emit(c, (uint8_t)(saved_local_count + i));
        cl_emit(c, OP_POP);
    }

    /* Backward jump to loop_start */
    cl_emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    cl_patch_jump(c, jtrue_pos);

    /* Compile result forms as progn, or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(result_forms)) {
        compile_progn(c, result_forms);
    } else {
        cl_emit(c, OP_NIL);
    }
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
    cl_emit(c, OP_POP);

    CL_GC_UNPROTECT(4);  /* var_clauses, body, end_test, result_forms */

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        cl_patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

void compile_do_star(CL_Compiler *c, CL_Obj form)
{
    /* (do* ((var init [step])...) (end-test result...) body...)
     * Like do, but bindings and steps are sequential. */
    CL_Obj var_clauses = cl_car(cl_cdr(form));
    CL_Obj end_clause = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    CL_Obj end_test = cl_car(end_clause);
    CL_Obj result_forms = cl_cdr(end_clause);
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int saved_block_count = c->block_count;

    CL_Obj vars[CL_MAX_BINDINGS];
    CL_Obj inits[CL_MAX_BINDINGS];
    CL_Obj steps[CL_MAX_BINDINGS];
    int has_step[CL_MAX_BINDINGS];
    uint8_t do_boxed[CL_MAX_BINDINGS];
    int n = 0;
    int i;
    int loop_start, jtrue_pos;
    int result_slot;
    CL_BlockInfo *bi;

    {
        CL_Obj vc = var_clauses;
        while (!CL_NULL_P(vc) && n < CL_MAX_BINDINGS) {
            CL_Obj clause = cl_car(vc);
            vars[n] = cl_car(clause);
            inits[n] = cl_car(cl_cdr(clause));
            if (!CL_NULL_P(cl_cdr(cl_cdr(clause)))) {
                steps[n] = cl_car(cl_cdr(cl_cdr(clause)));
                has_step[n] = 1;
            } else {
                steps[n] = CL_NIL;
                has_step[n] = 0;
            }
            n++;
            vc = cl_cdr(vc);
        }
    }

    /* Check which vars need boxing */
    {
        uint8_t mutated[CL_MAX_BINDINGS], captured[CL_MAX_BINDINGS];
        CL_Obj cur;
        memset(mutated, 0, (size_t)n);
        memset(captured, 0, (size_t)n);
        memset(do_boxed, 0, (size_t)n);
        cur = body;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n, mutated, captured, 0);
            cur = cl_cdr(cur);
        }
        scan_body_for_boxing(end_test, vars, n, mutated, captured, 0);
        cur = result_forms;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n, mutated, captured, 0);
            cur = cl_cdr(cur);
        }
        for (i = 0; i < n; i++)
            scan_body_for_boxing(steps[i], vars, n, mutated, captured, 0);
        for (i = 0; i < n; i++) {
            do_boxed[i] = (has_step[i] && captured[i]) ? 1 :
                          (mutated[i] && captured[i]) ? 1 : 0;
        }
    }

    c->in_tail = 0;

    /* GC-protect — compile_expr below can compact, making inits[]/vars[]/steps[]/
     * end_test/result_forms/body stale.  var_clauses is re-walked by each cursor. */
    CL_GC_PROTECT(var_clauses);
    CL_GC_PROTECT(body);
    CL_GC_PROTECT(end_test);
    CL_GC_PROTECT(result_forms);

    /* Sequential init: evaluate and store each var immediately.
     * Walk var_clauses with a protected cursor — compile_expr can compact inits[]/vars[] stale. */
    {
        CL_Obj vc = var_clauses;
        CL_GC_PROTECT(vc);
        for (i = 0; i < n; i++) {
            CL_Obj clause = cl_car(vc);
            compile_expr(c, cl_car(cl_cdr(clause)));
            /* Re-read var from protected cursor after compile_expr may have compacted */
            cl_env_add_local(env, cl_car(cl_car(vc)));
            if (do_boxed[i]) {
                cl_emit(c, OP_MAKE_CELL);
                env->boxed[saved_local_count + i] = 1;
            }
            cl_emit(c, OP_STORE);
            cl_emit(c, (uint8_t)(saved_local_count + i));
            cl_emit(c, OP_POP);
            vc = cl_cdr(vc);
        }
        CL_GC_UNPROTECT(1);
    }

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->locals[result_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;
    bi->uses_nlx = 0;
    bi->dyn_depth = c->special_depth;

    /* loop_start: compile end-test, JTRUE -> end */
    loop_start = c->code_pos;
    compile_expr(c, end_test);
    jtrue_pos = cl_emit_jump(c, OP_JTRUE);

    /* Compile body as an implicit tagbody per CLHS 6.1.6.  (go tag) inside
     * a DO / DO* body must find tags defined there.  Strip leading
     * (declare ...) forms the same way tagbody ignores them. */
    {
        CL_Obj b = body;
        CL_Obj clean = CL_NIL, tail = CL_NIL;
        CL_Obj tb_form;
        /* GC-protect b, clean, and tail — cl_cons inside the loop can compact */
        CL_GC_PROTECT(b);
        CL_GC_PROTECT(clean);
        CL_GC_PROTECT(tail);
        while (!CL_NULL_P(b)) {
            CL_Obj bform = cl_car(b);
            if (!(CL_CONS_P(bform) && cl_car(bform) == SYM_DECLARE)) {
                CL_Obj cell = cl_cons(bform, CL_NIL);
                if (CL_NULL_P(clean)) { clean = cell; tail = cell; }
                else {
                    ((CL_Cons *)CL_OBJ_TO_PTR(tail))->cdr = cell;
                    tail = cell;
                }
            }
            b = cl_cdr(b);
        }
        tb_form = cl_cons(SYM_TAGBODY, clean);
        CL_GC_PROTECT(tb_form);
        compile_expr(c, tb_form);
        cl_emit(c, OP_POP);  /* tagbody always leaves NIL */
        CL_GC_UNPROTECT(4);  /* tb_form, tail, clean, b */
    }

    /* Sequential step: evaluate and store each immediately.
     * Walk var_clauses with a protected cursor — compile_expr can compact steps[] stale. */
    {
        CL_Obj vc = var_clauses;
        CL_GC_PROTECT(vc);
        for (i = 0; i < n; i++) {
            if (has_step[i]) {
                CL_Obj clause = cl_car(vc);
                compile_expr(c, cl_car(cl_cdr(cl_cdr(clause))));
                if (do_boxed[i]) {
                    cl_emit(c, OP_CELL_SET_LOCAL);
                } else {
                    cl_emit(c, OP_STORE);
                }
                cl_emit(c, (uint8_t)(saved_local_count + i));
                cl_emit(c, OP_POP);
            }
            vc = cl_cdr(vc);
        }
        CL_GC_UNPROTECT(1);
    }

    cl_emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    cl_patch_jump(c, jtrue_pos);

    /* Compile result forms or NIL */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(result_forms)) {
        compile_progn(c, result_forms);
    } else {
        cl_emit(c, OP_NIL);
    }
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
    cl_emit(c, OP_POP);

    CL_GC_UNPROTECT(4);  /* var_clauses, body, end_test, result_forms */

    for (i = 0; i < bi->n_patches; i++)
        cl_patch_jump(c, bi->exit_patches[i]);

    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);

    cl_env_clear_boxed(env, saved_local_count);

    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

/* --- handler-bind --- */

CL_Obj compile_handler_bind(CL_Compiler *c, CL_Obj form)
{
    /* (handler-bind ((type handler-expr) ...) body...)
     *
     * Trampoline-aware: prelude pushes each handler, body's tail form
     * returns to the driver, postlude emits OP_HANDLER_POP <count>. */
    CL_Obj clauses = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int count = 0;
    CL_Obj cl;
    CL_TailFrame *tf;

    /* For each (type handler) clause: compile handler, push onto handler stack.
     * Protect body (used after the loop) and the cursor cl — compile_expr can
     * compact, making bare locals stale.  Re-read type_sym after compile_expr. */
    CL_GC_PROTECT(body);
    cl = clauses;
    CL_GC_PROTECT(cl);
    while (!CL_NULL_P(cl)) {
        CL_Obj clause      = cl_car(cl);
        CL_Obj handler_expr = cl_car(cl_cdr(clause));
        int type_idx;

        compile_expr(c, handler_expr);   /* Push handler closure on VM stack */
        /* Re-read type_sym after compile_expr — compaction may have moved it */
        type_idx = cl_add_constant(c, cl_car(cl_car(cl)));
        cl_emit(c, OP_HANDLER_PUSH);
        cl_emit_u16(c, (uint16_t)type_idx);
        count++;
        cl = cl_cdr(cl);
    }
    CL_GC_UNPROTECT(2); /* cl, body */

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_HANDLER_BIND;
    tf->saved_macro_count = count;  /* reused: handler count for HANDLER_POP */
    return compile_body_tail(c, body);
}

/* --- restart-case --- */

/* Parse the leading :report / :interactive / :test options of a restart
 * clause (CLHS 9.1).  Stores each supplied option expression through the
 * out-params (CL_UNBOUND = not supplied) and returns the remaining forms
 * (the restart body). */
static CL_Obj parse_restart_options(CL_Obj forms, CL_Obj *report,
                                    CL_Obj *interactive, CL_Obj *test)
{
    static CL_Obj KW_REPORT = CL_NIL;
    static CL_Obj KW_INTERACTIVE = CL_NIL;
    static CL_Obj KW_TEST = CL_NIL;

    if (CL_NULL_P(KW_REPORT)) {
        /* cl_intern_keyword allocates on first call; protect `forms` so the
         * caller's clause tree isn't stale when we walk it after returning. */
        CL_GC_PROTECT(forms);
        KW_REPORT = cl_intern_keyword("REPORT", 6);
        KW_INTERACTIVE = cl_intern_keyword("INTERACTIVE", 11);
        KW_TEST = cl_intern_keyword("TEST", 4);
        CL_GC_UNPROTECT(1);
    }

    *report = CL_UNBOUND;
    *interactive = CL_UNBOUND;
    *test = CL_UNBOUND;

    while (!CL_NULL_P(forms)) {
        CL_Obj head = cl_car(forms);
        CL_Obj *slot;
        if (head == KW_REPORT)           slot = report;
        else if (head == KW_INTERACTIVE) slot = interactive;
        else if (head == KW_TEST)        slot = test;
        else break;
        /* keyword + its value */
        forms = cl_cdr(forms);
        if (!CL_NULL_P(forms)) {
            *slot = cl_car(forms);
            forms = cl_cdr(forms);
        }
    }
    return forms;
}

/* Push a restart option onto the stack for OP_RESTART_PUSH.
 *  - not supplied            -> NIL
 *  - :report "string"        -> the literal string (a report string)
 *  - any other expression    -> (function <expr>), i.e. a report/interactive/
 *                               test function (CLHS accepts a symbol or
 *                               lambda expression acceptable to FUNCTION). */
static void compile_restart_option(CL_Compiler *c, CL_Obj expr, int allow_string)
{
    if (expr == CL_UNBOUND) {
        cl_emit_const(c, CL_NIL);
    } else if (allow_string && CL_ANY_STRING_P(expr)) {
        cl_emit_const(c, expr);
    } else {
        CL_Obj inner, fn_form;
        CL_GC_PROTECT(expr);
        inner = cl_cons(expr, CL_NIL);
        CL_GC_PROTECT(inner);
        fn_form = cl_cons(SYM_FUNCTION, inner);
        CL_GC_UNPROTECT(1); /* inner */
        CL_GC_PROTECT(fn_form);
        compile_expr(c, fn_form);
        CL_GC_UNPROTECT(2); /* expr, fn_form */
    }
}

void compile_restart_case(CL_Compiler *c, CL_Obj form)
{
    /* (restart-case form (name (params...) [:report str] body...)...) */
    CL_Obj main_form = cl_car(cl_cdr(form));
    CL_Obj clauses = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int count = 0;
    int catch_pos, jmp_pos;
    int dispatch_slot;
    CL_Obj cl_iter;
    CL_Obj catch_tag;

    c->in_tail = 0;

    /* Reserve a local slot for the dispatch cons at the catch landing.
     * Must be allocated before main_form compiles (same pattern as
     * compile_unwind_protect) so nested forms don't reuse this index. */
    dispatch_slot = env->local_count;
    env->locals[dispatch_slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Generate unique catch tag (a fresh cons cell) */
    catch_tag = cl_cons(CL_NIL, CL_NIL);

    /* Set up catch frame with the unique tag */
    cl_emit_const(c, catch_tag);  /* push tag */
    cl_emit(c, OP_CATCH);
    catch_pos = c->code_pos;
    cl_emit_i32(c, 0);  /* placeholder for landing offset */

    /* Push restart bindings: for each clause, compile lambda + push tag + OP_RESTART_PUSH.
     * Protect the cursor and catch_tag — compile_expr/cl_cons inside the loop can
     * compact, making bare locals stale.  Re-read restart_name after allocating calls. */
    cl_iter = clauses;
    CL_GC_PROTECT(cl_iter);    /* loop cursor */
    CL_GC_PROTECT(catch_tag);  /* used in every iteration after compile_expr */
    CL_GC_PROTECT(main_form);  /* used after the loop; compaction inside loop stales it */
    while (!CL_NULL_P(cl_iter)) {
        CL_Obj clause = cl_car(cl_iter);
        CL_Obj params = cl_car(cl_cdr(clause));
        CL_Obj report = CL_UNBOUND, interactive = CL_UNBOUND, test = CL_UNBOUND;
        CL_Obj clause_body = parse_restart_options(cl_cdr(cl_cdr(clause)),
                                                   &report, &interactive, &test);
        CL_Obj lambda_form;
        int name_idx;

        /* Re-read params from the protected cl_iter: parse_restart_options
         * may have triggered a compacting GC (keyword intern on first call),
         * leaving the pre-call `params` value stale. */
        params = cl_car(cl_cdr(cl_car(cl_iter)));

        /* Protect the option expressions across the allocating compile
         * calls below — compaction is a moving GC. */
        CL_GC_PROTECT(report);
        CL_GC_PROTECT(interactive);
        CL_GC_PROTECT(test);

        /* Build (lambda (params...) body...) and compile it */
        lambda_form = cl_cons(SYM_LAMBDA, cl_cons(params, clause_body));
        compile_expr(c, lambda_form);  /* pushes closure (handler) on stack */

        /* Push the restart's :report / :interactive / :test operands, then
         * the catch tag.  OP_RESTART_PUSH pops them in reverse. */
        compile_restart_option(c, report, 1);       /* report may be a string */
        compile_restart_option(c, interactive, 0);
        compile_restart_option(c, test, 0);
        cl_emit_const(c, catch_tag);
        CL_GC_UNPROTECT(3);

        /* OP_RESTART_PUSH: pops tag/test/interactive/report/handler, builds
         * the first-class restart object, pushes the restart binding */
        /* Re-read restart_name after allocating calls — compaction may have moved it */
        name_idx = cl_add_constant(c, cl_car(cl_car(cl_iter)));
        cl_emit(c, OP_RESTART_PUSH);
        cl_emit_u16(c, (uint16_t)name_idx);
        count++;
        cl_iter = cl_cdr(cl_iter);
    }
    CL_GC_UNPROTECT(3); /* main_form, catch_tag, cl_iter */

    /* Compile the main form */
    compile_expr(c, main_form);

    /* Normal exit: pop restart bindings, uncatch, jump past landing */
    cl_emit(c, OP_RESTART_POP);
    cl_emit(c, (uint8_t)count);
    cl_emit(c, OP_UNCATCH);
    jmp_pos = cl_emit_jump(c, OP_JMP);

    /* [landing]: invoke-restart threw (handler . args-list) cons here.
     * Unwind-protect cleanups have already run; now apply the handler in
     * the dynamic environment captured at restart establishment (CLHS). */
    cl_patch_jump(c, catch_pos);
    cl_emit(c, OP_DUP);
    cl_emit(c, OP_CDR);                         /* TOS = args-list, below = cons */
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)dispatch_slot);         /* save args-list (peeks, no pop) */
    cl_emit(c, OP_POP);                         /* discard args-list copy */
    cl_emit(c, OP_CAR);                         /* TOS = handler */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)dispatch_slot);         /* push args-list */
    cl_emit(c, OP_APPLY);                       /* result on stack */

    /* [past_landing]: both paths converge with one value on stack */
    cl_patch_jump(c, jmp_pos);

    cl_env_clear_boxed(env, saved_local_count);
    env->local_count = saved_local_count;
    c->in_tail = saved_tail;
}

/* --- Macrolet --- */

/* Build the macrolet local-macro expanders for BINDINGS (each
 * (name lambda-list . body)) and install them into ENV via
 * cl_env_add_local_macro.  Factored out of compile_macrolet so compile-file's
 * top-level processor can reconstruct a macrolet's lexical macro environment
 * and process its body forms as top-level forms (CLHS 3.2.3.1). */
void cl_macrolet_install_expanders(CL_CompEnv *env, CL_Obj bindings)
{
    /* Compile each macro expander at compile time */
    {
        CL_Obj b = bindings;
        CL_GC_PROTECT(b);
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj mname = cl_car(binding);
            CL_Obj lambda_list = cl_car(cl_cdr(binding));
            CL_Obj mbody = cl_cdr(cl_cdr(binding));
            CL_Obj lambda_form, bytecode, closure;

            CL_GC_PROTECT(lambda_list);
            CL_GC_PROTECT(mbody);

            /* Strip &environment (same as defmacro).  The env-var is
             * bound at the top of the body to the current lexical env,
             * captured by the caller and read via %MACROEXPAND-ENV. */
            {
                CL_Obj cur2 = lambda_list;
                CL_Obj prev2 = CL_NIL;
                while (!CL_NULL_P(cur2)) {
                    CL_Obj p = cl_car(cur2);
                    if (p == SYM_AMP_ENVIRONMENT) {
                        CL_Obj next2 = cl_cdr(cur2);
                        CL_Obj env_var = CL_NULL_P(next2) ? CL_NIL : cl_car(next2);
                        CL_Obj after2 = CL_NULL_P(next2) ? CL_NIL : cl_cdr(next2);
                        if (CL_NULL_P(prev2))
                            lambda_list = after2;
                        else
                            ((CL_Cons *)CL_OBJ_TO_PTR(prev2))->cdr = after2;
                        if (CL_SYMBOL_P(env_var)) {
                            CL_Obj capture_sym =
                                cl_intern_in("%MACROEXPAND-ENV", 16,
                                             cl_package_clamiga);
                            CL_Obj capture_call = cl_cons(capture_sym, CL_NIL);
                            CL_Obj bind = cl_cons(env_var,
                                                  cl_cons(capture_call, CL_NIL));
                            CL_Obj binds = cl_cons(bind, CL_NIL);
                            CL_Obj let_f = cl_cons(SYM_LET, cl_cons(binds, mbody));
                            mbody = cl_cons(let_f, CL_NIL);
                        }
                        break;
                    }
                    prev2 = cur2;
                    cur2 = cl_cdr(cur2);
                }
            }

            /* Handle &whole (same as defmacro) */
            {
                CL_Obj whole_var = CL_NIL;
                if (!CL_NULL_P(lambda_list) && cl_car(lambda_list) == SYM_AMP_WHOLE) {
                    CL_Obj rest2 = cl_cdr(lambda_list);
                    if (!CL_NULL_P(rest2)) {
                        whole_var = cl_car(rest2);
                        lambda_list = cl_cdr(rest2);
                    }
                }
                if (CL_NULL_P(whole_var)) {
                    whole_var = defmacro_gensym();
                }
                lambda_list = cl_cons(whole_var, lambda_list);
            }

            /* Transform destructuring lambda lists (same as defmacro) */
            if (defmacro_needs_destructuring(lambda_list)) {
                CL_Obj new_ll = CL_NIL, new_ll_tail = CL_NIL;
                CL_Obj cur = lambda_list;
                int in_opt_key = 0;

                CL_GC_PROTECT(new_ll);
                CL_GC_PROTECT(new_ll_tail);

                while (!CL_NULL_P(cur)) {
                    CL_Obj param = cl_car(cur);
                    CL_Obj cell;

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
                        CL_Obj gs = defmacro_gensym();
                        CL_Obj db_form;
                        CL_GC_PROTECT(gs);
                        db_form = cl_cons(SYM_DESTRUCTURING_BIND,
                                   cl_cons(param, cl_cons(gs, mbody)));
                        mbody = cl_cons(db_form, CL_NIL);
                        cell = cl_cons(gs, CL_NIL);
                        if (CL_NULL_P(new_ll)) { new_ll = cell; }
                        else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                        new_ll_tail = cell;
                        CL_GC_UNPROTECT(1); /* gs */
                    } else if (CL_SYMBOL_P(param) &&
                               (param == SYM_AMP_BODY || param == SYM_AMP_REST)) {
                        CL_Obj next_param = CL_NULL_P(cl_cdr(cur)) ? CL_NIL : cl_car(cl_cdr(cur));
                        in_opt_key = 0;
                        if (CL_CONS_P(next_param)) {
                            CL_Obj gs = defmacro_gensym();
                            CL_Obj db_form;
                            CL_GC_PROTECT(gs);
                            db_form = cl_cons(SYM_DESTRUCTURING_BIND,
                                       cl_cons(next_param, cl_cons(gs, mbody)));
                            mbody = cl_cons(db_form, CL_NIL);
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
                            cell = cl_cons(param, CL_NIL);
                            if (CL_NULL_P(new_ll)) { new_ll = cell; }
                            else { ((CL_Cons *)CL_OBJ_TO_PTR(new_ll_tail))->cdr = cell; }
                            new_ll_tail = cell;
                        }
                    } else {
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

            /* Build (lambda (params) body...) */
            lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, mbody));
            CL_GC_PROTECT(lambda_form);

            /* Wrap in 2-arg (form environment) trampoline — matches
             * compile_defmacro so local-macrolet expanders have the
             * same shape and can be invoked uniformly. */
            {
                CL_Obj inner_gs = defmacro_gensym();
                CL_Obj form_gs, env_gs, cme_sym;
                CL_Obj inner_call, outer_ll, outer_lambda;
                CL_Obj let_binding, let_bindings, wrap_form;
                CL_GC_PROTECT(inner_gs);
                form_gs = defmacro_gensym();
                CL_GC_PROTECT(form_gs);
                env_gs = defmacro_gensym();
                CL_GC_PROTECT(env_gs);
                cme_sym = cl_intern_in("%CALL-MACRO-EXPANDER", 20,
                                       cl_package_clamiga);
                inner_call = cl_cons(env_gs, CL_NIL);
                inner_call = cl_cons(form_gs, inner_call);
                inner_call = cl_cons(inner_gs, inner_call);
                inner_call = cl_cons(cme_sym, inner_call);
                CL_GC_PROTECT(inner_call);
                outer_ll = cl_cons(env_gs, CL_NIL);
                outer_ll = cl_cons(form_gs, outer_ll);
                CL_GC_PROTECT(outer_ll);
                outer_lambda = cl_cons(inner_call, CL_NIL);
                outer_lambda = cl_cons(outer_ll, outer_lambda);
                outer_lambda = cl_cons(SYM_LAMBDA, outer_lambda);
                CL_GC_PROTECT(outer_lambda);
                let_binding = cl_cons(lambda_form, CL_NIL);
                let_binding = cl_cons(inner_gs, let_binding);
                CL_GC_PROTECT(let_binding);
                let_bindings = cl_cons(let_binding, CL_NIL);
                CL_GC_PROTECT(let_bindings);
                wrap_form = cl_cons(outer_lambda, CL_NIL);
                wrap_form = cl_cons(let_bindings, wrap_form);
                wrap_form = cl_cons(SYM_LET, wrap_form);
                CL_GC_PROTECT(wrap_form);

                /* Compile and evaluate at compile time to get closure */
                bytecode = cl_compile(wrap_form);
                CL_GC_PROTECT(bytecode);
                closure = cl_vm_eval(bytecode);
                CL_GC_UNPROTECT(10); /* bytecode + 9 wrapper temps */
            }
            CL_GC_UNPROTECT(3); /* lambda_form, mbody, lambda_list */

            cl_env_add_local_macro(env, mname, closure);

            b = cl_cdr(b);
        }
        CL_GC_UNPROTECT(1);
    }
}

CL_Obj compile_macrolet(CL_Compiler *c, CL_Obj form)
{
    /* (macrolet ((name (params) body...) ...) body...) */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_macro_count = env->local_macro_count;
    CL_TailFrame *tf;

    cl_macrolet_install_expanders(env, bindings);

    /* Trampoline body — postlude restores local_macro_count. */
    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_MACROLET;
    tf->saved_macro_count = saved_macro_count;
    return compile_body_tail(c, body);
}

/* --- Symbol-macrolet --- */

CL_Obj compile_symbol_macrolet(CL_Compiler *c, CL_Obj form)
{
    /* (symbol-macrolet ((sym expansion) ...) body...) — register each
     * symbol macro, push a tail frame to restore the count after the
     * body chain drains, and return the body's tail form. */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_symbol_macro_count = env->symbol_macro_count;
    CL_TailFrame *tf;

    {
        CL_Obj b = bindings;
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj sym = cl_car(binding);
            CL_Obj expansion = cl_car(cl_cdr(binding));
            cl_env_add_symbol_macro(env, sym, expansion);
            b = cl_cdr(b);
        }
    }

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_SYMBOL_MACROLET;
    tf->saved_macro_count = saved_symbol_macro_count;
    return compile_body_tail(c, body);
}

/* --- PROGV --- */

CL_Obj compile_progv(CL_Compiler *c, CL_Obj form)
{
    /* (progv symbols-form values-form body...) — emit BIND prelude,
     * trampoline the body, postlude emits UNBIND. */
    CL_Obj symbols_form = cl_car(cl_cdr(form));
    CL_Obj values_form  = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));
    int saved_tail = c->in_tail;
    CL_TailFrame *tf;

    c->in_tail = 0;
    compile_expr(c, symbols_form);
    compile_expr(c, values_form);
    cl_emit(c, OP_PROGV_BIND);

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_PROGV;
    tf->saved_tail = saved_tail;
    return compile_body_tail(c, body);
}
