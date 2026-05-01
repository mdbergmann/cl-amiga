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
            while (!CL_NULL_P(rest)) {
                CL_Obj opt = cl_car(rest);
                CL_Obj var;
                CL_Obj default_val = CL_NIL;
                int slot, skip_pos;

                if (elem == SYM_AMP_KEY) {
                    /* &key after &optional: break to outer loop to process keys */
                    pattern = rest;  /* rest starts at &key */
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
            goto done;
        optional_done:
            /* Continue outer loop — pattern points to &key */
            continue;
        }

        /* &key — keyword destructuring (plist-based) */
        if (elem == SYM_AMP_KEY) {
            CL_Obj rest = cl_cdr(pattern);
            int scan_slot = alloc_temp_slot(env);

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

    cl_env_clear_boxed(env, saved_local_count);

    /* Restore scope */
    env->local_count = saved_local_count;
}

/* --- Block / Return --- */

/* Check if an S-expression tree contains closure-creating forms
 * (lambda, labels, flet). Used to decide if a block/tagbody needs NLX
 * for cross-closure return-from/go support. */
static int tree_has_closure_forms(CL_Obj tree)
{
    /* Closure-creating forms must appear as (OP ...) — with LAMBDA/FLET/etc.
     * in operator position.  A bare symbol such as `LAMBDA` used as a
     * variable reference (e.g. `(setq lambda X)`) does NOT create a
     * closure, so we must only inspect cons cells and only check their
     * head.  Earlier code also matched bare symbols, which spuriously
     * fired on user variables happening to be named LAMBDA/FLET/etc.,
     * wrongly promoting a block/tagbody to the NLX path. */
    while (CL_CONS_P(tree)) {
        CL_Obj head = cl_car(tree);
        if (CL_CONS_P(head)) {
            CL_Obj op = cl_car(head);
            if (op == SYM_LAMBDA || op == SYM_LABELS || op == SYM_FLET
                || op == SYM_RESTART_CASE)
                return 1;
            if (tree_has_closure_forms(head))
                return 1;
        }
        tree = cl_cdr(tree);
    }
    return 0;
}

/* Check if (return-from <tag> ...) appears anywhere in tree. */
static int tree_contains_return_from(CL_Obj tree, CL_Obj tag)
{
    while (CL_CONS_P(tree)) {
        CL_Obj head = cl_car(tree);
        if (CL_CONS_P(head)) {
            CL_Obj op = cl_car(head);
            if (op == SYM_RETURN_FROM && CL_CONS_P(cl_cdr(head))
                && cl_car(cl_cdr(head)) == tag)
                return 1;
            if (tree_contains_return_from(head, tag))
                return 1;
        }
        tree = cl_cdr(tree);
    }
    return 0;
}

/* Precise check: does a closure form (lambda/labels/flet) in the tree
 * contain (return-from <tag> ...)?  Only returns true when the block
 * actually needs NLX — i.e. return-from crosses a closure boundary.
 * Falls back to tree_has_closure_forms for NIL tags (anonymous blocks). */
static int tree_needs_nlx_block(CL_Obj body, CL_Obj tag)
{
    CL_Obj tree;
    if (CL_NULL_P(tag))
        return tree_has_closure_forms(body);

    tree = body;
    while (CL_CONS_P(tree)) {
        CL_Obj head = cl_car(tree);
        if (CL_CONS_P(head)) {
            CL_Obj op = cl_car(head);
            if (op == SYM_LAMBDA || op == SYM_LABELS || op == SYM_FLET
                || op == SYM_RESTART_CASE) {
                if (tree_contains_return_from(head, tag))
                    return 1;
            }
            if (tree_needs_nlx_block(head, tag))
                return 1;
        }
        tree = cl_cdr(tree);
    }
    return 0;
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

    /* Parallel init: evaluate all init forms onto stack */
    for (i = 0; i < n; i++) {
        compile_expr(c, inits[i]);
    }

    /* Register all vars as locals */
    for (i = 0; i < n; i++) {
        cl_env_add_local(env, vars[i]);
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
        CL_GC_UNPROTECT(3);
    }

    /* Parallel step: evaluate all step forms (or load current value) */
    for (i = 0; i < n; i++) {
        if (has_step[i]) {
            compile_expr(c, steps[i]);
        } else {
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)(saved_local_count + i));
            if (do_boxed[i]) cl_emit(c, OP_CELL_REF);
        }
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

    /* Sequential init: evaluate and store each var immediately */
    for (i = 0; i < n; i++) {
        compile_expr(c, inits[i]);
        cl_env_add_local(env, vars[i]);
        if (do_boxed[i]) {
            cl_emit(c, OP_MAKE_CELL);
            env->boxed[saved_local_count + i] = 1;
        }
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)(saved_local_count + i));
        cl_emit(c, OP_POP);
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
     * (declare ...) forms the same way tagbody ignores them.
     * `tail` is always reachable through `clean`'s cdr-chain, so only
     * `clean` and the final tb_form need GC roots — keeps compiler
     * root-stack pressure low (peak = 2 per nesting level). */
    {
        CL_Obj b = body;
        CL_Obj clean = CL_NIL, tail = CL_NIL;
        CL_Obj tb_form;
        CL_GC_PROTECT(clean);
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
        CL_GC_UNPROTECT(2);
    }

    /* Sequential step: evaluate and store each immediately */
    for (i = 0; i < n; i++) {
        if (has_step[i]) {
            compile_expr(c, steps[i]);
            if (do_boxed[i]) {
                cl_emit(c, OP_CELL_SET_LOCAL);
            } else {
                cl_emit(c, OP_STORE);
            }
            cl_emit(c, (uint8_t)(saved_local_count + i));
            cl_emit(c, OP_POP);
        }
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

    /* For each (type handler) clause: compile handler, push onto handler stack */
    CL_GC_PROTECT(clauses);
    for (cl = clauses; !CL_NULL_P(cl); cl = cl_cdr(cl)) {
        CL_Obj clause = cl_car(cl);
        CL_Obj type_sym = cl_car(clause);
        CL_Obj handler_expr = cl_car(cl_cdr(clause));
        int type_idx;

        compile_expr(c, handler_expr);    /* Push handler closure on VM stack */
        type_idx = cl_add_constant(c, type_sym);
        cl_emit(c, OP_HANDLER_PUSH);
        cl_emit_u16(c, (uint16_t)type_idx);
        count++;
    }
    CL_GC_UNPROTECT(1);

    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_HANDLER_BIND;
    tf->saved_macro_count = count;  /* reused: handler count for HANDLER_POP */
    return compile_body_tail(c, body);
}

/* --- restart-case --- */

/* Skip :report, :interactive, :test keyword options in restart clause */
static CL_Obj skip_restart_options(CL_Obj forms)
{
    static CL_Obj KW_REPORT = CL_NIL;
    static CL_Obj KW_INTERACTIVE = CL_NIL;
    static CL_Obj KW_TEST = CL_NIL;

    if (CL_NULL_P(KW_REPORT)) {
        KW_REPORT = cl_intern_keyword("REPORT", 6);
        KW_INTERACTIVE = cl_intern_keyword("INTERACTIVE", 11);
        KW_TEST = cl_intern_keyword("TEST", 4);
    }

    while (!CL_NULL_P(forms)) {
        CL_Obj head = cl_car(forms);
        if (head == KW_REPORT || head == KW_INTERACTIVE || head == KW_TEST) {
            /* Skip keyword + its value */
            forms = cl_cdr(forms);
            if (!CL_NULL_P(forms))
                forms = cl_cdr(forms);
        } else {
            break;
        }
    }
    return forms;
}

void compile_restart_case(CL_Compiler *c, CL_Obj form)
{
    /* (restart-case form (name (params...) [:report str] body...)...) */
    CL_Obj main_form = cl_car(cl_cdr(form));
    CL_Obj clauses = cl_cdr(cl_cdr(form));
    int saved_tail = c->in_tail;
    int count = 0;
    int catch_pos, jmp_pos;
    CL_Obj cl_iter;
    CL_Obj catch_tag;

    c->in_tail = 0;

    /* Generate unique catch tag (a fresh cons cell) */
    catch_tag = cl_cons(CL_NIL, CL_NIL);

    /* Set up catch frame with the unique tag */
    cl_emit_const(c, catch_tag);  /* push tag */
    cl_emit(c, OP_CATCH);
    catch_pos = c->code_pos;
    cl_emit_i32(c, 0);  /* placeholder for landing offset */

    /* Push restart bindings: for each clause, compile lambda + push tag + OP_RESTART_PUSH */
    CL_GC_PROTECT(clauses);
    for (cl_iter = clauses; !CL_NULL_P(cl_iter); cl_iter = cl_cdr(cl_iter)) {
        CL_Obj clause = cl_car(cl_iter);
        CL_Obj restart_name = cl_car(clause);
        CL_Obj params = cl_car(cl_cdr(clause));
        CL_Obj clause_body = skip_restart_options(cl_cdr(cl_cdr(clause)));
        CL_Obj lambda_form;
        int name_idx;

        /* Build (lambda (params...) body...) and compile it */
        lambda_form = cl_cons(SYM_LAMBDA, cl_cons(params, clause_body));
        compile_expr(c, lambda_form);  /* pushes closure on stack */

        /* Push catch tag */
        cl_emit_const(c, catch_tag);

        /* OP_RESTART_PUSH: pops tag, pops closure, pushes restart binding */
        name_idx = cl_add_constant(c, restart_name);
        cl_emit(c, OP_RESTART_PUSH);
        cl_emit_u16(c, (uint16_t)name_idx);
        count++;
    }
    CL_GC_UNPROTECT(1);

    /* Compile the main form */
    compile_expr(c, main_form);

    /* Normal exit: pop restart bindings, uncatch, jump past landing */
    cl_emit(c, OP_RESTART_POP);
    cl_emit(c, (uint8_t)count);
    cl_emit(c, OP_UNCATCH);
    jmp_pos = cl_emit_jump(c, OP_JMP);

    /* [landing]: invoke-restart threw result here */
    cl_patch_jump(c, catch_pos);

    /* [past_landing]: both paths converge */
    cl_patch_jump(c, jmp_pos);

    c->in_tail = saved_tail;
}

/* --- Macrolet --- */

CL_Obj compile_macrolet(CL_Compiler *c, CL_Obj form)
{
    /* (macrolet ((name (params) body...) ...) body...) */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_macro_count = env->local_macro_count;
    CL_TailFrame *tf;

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

            /* Compile and evaluate at compile time to get closure */
            bytecode = cl_compile(lambda_form);
            CL_GC_PROTECT(bytecode);
            closure = cl_vm_eval(bytecode);
            CL_GC_UNPROTECT(4); /* bytecode, lambda_form, mbody, lambda_list */

            cl_env_add_local_macro(env, mname, closure);

            b = cl_cdr(b);
        }
        CL_GC_UNPROTECT(1);
    }

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
