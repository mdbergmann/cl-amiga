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
            break; /* nothing after &rest */
        }

        /* &optional — remaining elements are optional with defaults */
        if (elem == SYM_AMP_OPTIONAL) {
            CL_Obj rest = cl_cdr(pattern);
            while (!CL_NULL_P(rest)) {
                CL_Obj opt = cl_car(rest);
                CL_Obj var;
                CL_Obj default_val = CL_NIL;
                int slot, skip_pos;

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

                if (CL_CONS_P(opt)) {
                    var = cl_car(opt);
                    if (!CL_NULL_P(cl_cdr(opt)))
                        default_val = cl_car(cl_cdr(opt));
                } else {
                    var = opt;
                }

                slot = cl_env_add_local(env, var);

                /* If pos is NIL, use default; else take (car pos) */
                cl_emit(c, OP_LOAD);
                cl_emit(c, (uint8_t)pos_slot);
                skip_pos = cl_emit_jump(c, OP_JNIL);

                /* pos not nil: var = (car pos), pos = (cdr pos) */
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
                {
                    int end_pos = cl_emit_jump(c, OP_JMP);
                    cl_patch_jump(c, skip_pos);
                    /* pos is nil: use default */
                    compile_expr(c, default_val);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)slot);
                    cl_emit(c, OP_POP);
                    cl_patch_jump(c, end_pos);
                }

                rest = cl_cdr(rest);
                elem = CL_NULL_P(rest) ? CL_NIL : cl_car(rest);
            }
            goto done;
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
                        cl_emit(c, OP_CDR);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)scan_slot);
                        cl_emit(c, OP_POP);
                        cl_emit_loop_jump(c, OP_JMP, loop_start);
                        cl_patch_jump(c, odd_pos);
                        /* Odd-length: pop NIL, fall through to not_found */
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

    /* Restore scope */
    env->local_count = saved_local_count;
}

/* --- Block / Return --- */

/* Check if an S-expression tree contains closure-creating forms
 * (lambda, labels, flet). Used to decide if a block needs NLX
 * for cross-closure return-from support. */
static int tree_has_closure_forms(CL_Obj tree)
{
    while (CL_CONS_P(tree)) {
        CL_Obj head = cl_car(tree);
        if (CL_CONS_P(head)) {
            CL_Obj op = cl_car(head);
            if (op == SYM_LAMBDA || op == SYM_LABELS || op == SYM_FLET)
                return 1;
            if (tree_has_closure_forms(head))
                return 1;
        } else if (head == SYM_LAMBDA || head == SYM_LABELS || head == SYM_FLET) {
            return 1;
        }
        tree = cl_cdr(tree);
    }
    return 0;
}

void compile_block(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int saved_block_count = c->block_count;
    int needs_nlx = tree_has_closure_forms(body);
    CL_BlockInfo *bi;

    /* Push block info (for compile-time lookup by return-from) */
    bi = &c->blocks[c->block_count++];
    bi->tag = tag;
    bi->n_patches = 0;
    bi->uses_nlx = needs_nlx;

    if (needs_nlx) {
        /* NLX path: set up NLX frame for cross-closure return-from */
        int tag_idx, block_push_pos, jmp_pos;

        bi->result_slot = -1;

        tag_idx = cl_add_constant(c, tag);
        cl_emit(c, OP_BLOCK_PUSH);
        cl_emit_u16(c, (uint16_t)tag_idx);
        block_push_pos = c->code_pos;
        cl_emit_i16(c, 0); /* placeholder offset to landing */

        compile_body(c, body);

        /* Normal exit: pop NLX frame, jump past landing */
        cl_emit(c, OP_BLOCK_POP);
        jmp_pos = cl_emit_jump(c, OP_JMP);

        /* Landing: longjmp arrives here with result on stack */
        cl_patch_jump(c, block_push_pos);

        /* End: both paths converge with result on TOS */
        cl_patch_jump(c, jmp_pos);
    } else {
        /* Local path: efficient local jumps (no NLX overhead) */
        CL_CompEnv *env = c->env;
        int saved_local_count = env->local_count;
        int result_slot, i;

        result_slot = env->local_count;
        env->local_count++;
        if (env->local_count > env->max_locals)
            env->max_locals = env->local_count;
        bi->result_slot = result_slot;

        compile_body(c, body);

        /* Normal exit: store result in slot */
        cl_emit(c, OP_STORE);
        cl_emit(c, (uint8_t)result_slot);
        cl_emit(c, OP_POP);

        /* Patch all return-from jumps to here */
        for (i = 0; i < bi->n_patches; i++)
            cl_patch_jump(c, bi->exit_patches[i]);

        /* Load result from slot */
        cl_emit(c, OP_LOAD);
        cl_emit(c, (uint8_t)result_slot);

        env->local_count = saved_local_count;
    }

    /* Restore */
    c->block_count = saved_block_count;
}

void compile_return_from(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj val_form = cl_car(cl_cdr(cl_cdr(form)));
    int saved_tail = c->in_tail;
    int i, tag_idx;

    /* Find matching block (innermost first) — check local blocks */
    for (i = c->block_count - 1; i >= 0; i--) {
        if (c->blocks[i].tag == tag) {
            CL_BlockInfo *bi = &c->blocks[i];

            /* Compile value */
            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(cl_cdr(form))))
                compile_expr(c, val_form);
            else
                cl_emit(c, OP_NIL);
            c->in_tail = saved_tail;

            if (bi->uses_nlx) {
                /* NLX-based block: emit OP_BLOCK_RETURN */
                tag_idx = cl_add_constant(c, tag);
                cl_emit(c, OP_BLOCK_RETURN);
                cl_emit_u16(c, (uint16_t)tag_idx);
            } else {
                /* Local-jump block (loop forms): use existing mechanism */
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)bi->result_slot);
                cl_emit(c, OP_POP);
                if (bi->n_patches < CL_MAX_BLOCK_PATCHES)
                    bi->exit_patches[bi->n_patches++] = cl_emit_jump(c, OP_JMP);
            }
            return;
        }
    }

    /* Check outer blocks (from enclosing scopes, for cross-closure return-from) */
    for (i = 0; i < c->outer_block_count; i++) {
        if (c->outer_blocks[i] == tag) {
            /* Compile value */
            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(cl_cdr(form))))
                compile_expr(c, val_form);
            else
                cl_emit(c, OP_NIL);
            c->in_tail = saved_tail;

            /* Emit cross-closure block return (NLX longjmp) */
            tag_idx = cl_add_constant(c, tag);
            cl_emit(c, OP_BLOCK_RETURN);
            cl_emit_u16(c, (uint16_t)tag_idx);
            return;
        }
    }

    cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
             CL_NULL_P(tag) ? "NIL" : cl_symbol_name(tag));
}

void compile_return(CL_Compiler *c, CL_Obj form)
{
    /* (return [value]) => return-from NIL */
    CL_Obj val_form = cl_car(cl_cdr(form));
    int saved_tail = c->in_tail;
    int i, tag_idx;

    for (i = c->block_count - 1; i >= 0; i--) {
        if (CL_NULL_P(c->blocks[i].tag)) {
            CL_BlockInfo *bi = &c->blocks[i];

            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(form)))
                compile_expr(c, val_form);
            else
                cl_emit(c, OP_NIL);
            c->in_tail = saved_tail;

            if (bi->uses_nlx) {
                /* NLX-based block: emit OP_BLOCK_RETURN */
                tag_idx = cl_add_constant(c, CL_NIL);
                cl_emit(c, OP_BLOCK_RETURN);
                cl_emit_u16(c, (uint16_t)tag_idx);
            } else {
                /* Local-jump block (loop forms): use existing mechanism */
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)bi->result_slot);
                cl_emit(c, OP_POP);
                if (bi->n_patches < CL_MAX_BLOCK_PATCHES)
                    bi->exit_patches[bi->n_patches++] = cl_emit_jump(c, OP_JMP);
            }
            return;
        }
    }

    /* Check outer blocks for NIL-tagged block */
    for (i = 0; i < c->outer_block_count; i++) {
        if (CL_NULL_P(c->outer_blocks[i])) {
            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(form)))
                compile_expr(c, val_form);
            else
                cl_emit(c, OP_NIL);
            c->in_tail = saved_tail;

            tag_idx = cl_add_constant(c, CL_NIL);
            cl_emit(c, OP_BLOCK_RETURN);
            cl_emit_u16(c, (uint16_t)tag_idx);
            return;
        }
    }

    cl_error(CL_ERR_GENERAL, "RETURN: no block named NIL");
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

    c->in_tail = 0;

    /* Push tagbody info */
    tb = &c->tagbodies[c->tagbody_count++];
    tb->n_tags = 0;

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

    /* Second pass: compile statements, record tag positions and patch forwards */
    cursor = body;
    while (!CL_NULL_P(cursor)) {
        CL_Obj item = cl_car(cursor);
        if (is_tagbody_tag(item)) {
            /* Record the bytecode position of this tag and patch pending forward jumps */
            for (i = 0; i < tb->n_tags; i++) {
                if (tb->tags[i].tag == item) {
                    int j;
                    tb->tags[i].code_pos = c->code_pos;
                    /* Patch any forward go jumps that targeted this tag */
                    for (j = 0; j < tb->tags[i].n_forward; j++) {
                        cl_patch_jump(c, tb->tags[i].forward_patches[j]);
                    }
                    tb->tags[i].n_forward = 0;
                    break;
                }
            }
        } else {
            /* Compile statement, discard result */
            compile_expr(c, item);
            cl_emit(c, OP_POP);
        }
        cursor = cl_cdr(cursor);
    }

    /* tagbody returns NIL */
    cl_emit(c, OP_NIL);

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
                if (ti->code_pos >= 0) {
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
    cl_emit_i16(c, 0); /* placeholder */

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
    /* (unwind-protect protected-form cleanup1 cleanup2 ...) */
    CL_Obj protected_form = cl_car(cl_cdr(form));
    CL_Obj cleanup_forms = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int uwprot_pos, jmp_pos;
    int result_slot;

    c->in_tail = 0;

    /* Allocate result slot */
    result_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* OP_UWPROT offset_to_cleanup_landing */
    cl_emit(c, OP_UWPROT);
    uwprot_pos = c->code_pos;
    cl_emit_i16(c, 0); /* placeholder */

    /* Compile protected form */
    compile_expr(c, protected_form);

    /* Save result in slot */
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)result_slot);
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
        while (!CL_NULL_P(cf)) {
            compile_expr(c, cl_car(cf));
            cl_emit(c, OP_POP);
            cf = cl_cdr(cf);
        }
    }

    /* OP_UWRETHROW: if pending throw, re-initiate (never returns); else nop */
    cl_emit(c, OP_UWRETHROW);

    /* Normal path: load saved result */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)result_slot);

    /* Restore */
    c->in_tail = saved_tail;
    env->local_count = saved_local_count;
}

/* --- Flet / Labels --- */

void compile_flet(CL_Compiler *c, CL_Obj form)
{
    /* (flet ((name (params) body...) ...) body...) */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_fun_count = env->local_fun_count;
    int saved_tail = c->in_tail;

    /* Phase 1: compile each function in outer scope, store in anonymous slots */
    {
        CL_Obj b = bindings;
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
    }

    /* Phase 2: compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

    /* Restore */
    env->local_count = saved_local_count;
    env->local_fun_count = saved_fun_count;
}

void compile_labels(CL_Compiler *c, CL_Obj form)
{
    /* (labels ((name (params) body...) ...) body...)
     *
     * Uses temporary global bindings so recursive/mutual references
     * resolve at runtime via FLOAD. This works because FLOAD checks
     * the value binding, and the closures are stored there before any
     * function body executes.
     */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int saved_tail = c->in_tail;

    /* Compile each function as lambda, store as global value binding */
    {
        CL_Obj b = bindings;
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj fname = cl_car(binding);
            CL_Obj lambda_list = cl_car(cl_cdr(binding));
            CL_Obj fbody = cl_cdr(cl_cdr(binding));
            CL_Obj lambda_form;
            int idx;

            /* Build (lambda (params) body...)
             * TODO: CL spec requires implicit block around labels functions,
             * but this interacts with our NLX-based cross-closure return-from.
             * Adding the block breaks cross-scope return-from. Needs investigation. */
            lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, fbody));
            CL_GC_PROTECT(lambda_form);

            c->in_tail = 0;
            compile_expr(c, lambda_form);
            CL_GC_UNPROTECT(1);

            /* Store as global value binding (FLOAD falls back to value) */
            idx = cl_add_constant(c, fname);
            cl_emit(c, OP_GSTORE);
            cl_emit_u16(c, (uint16_t)idx);
            cl_emit(c, OP_POP);

            b = cl_cdr(b);
        }
    }

    /* Compile body — calls use FLOAD which finds the value binding */
    c->in_tail = saved_tail;
    compile_body(c, body);
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
    CL_BlockInfo *bi;
    int i;

    c->in_tail = 0;

    /* Allocate var slot */
    var_slot = cl_env_add_local(env, var);

    /* Allocate internal iter slot (no symbol, never looked up) */
    iter_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;

    /* Compile list-form, store into iter_slot */
    compile_expr(c, list_form);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_POP);

    /* loop_start: LOAD iter_slot, JNIL -> end */
    loop_start = c->code_pos;
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    jnil_pos = cl_emit_jump(c, OP_JNIL);

    /* Set var = (car iter) */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_CAR);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* Advance iter = (cdr iter) */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_CDR);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)iter_slot);
    cl_emit(c, OP_POP);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            cl_emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Backward jump to loop_start */
    cl_emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    cl_patch_jump(c, jnil_pos);

    /* CL spec: var is NIL during result-form evaluation */
    cl_emit(c, OP_NIL);
    cl_emit(c, OP_STORE);
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
    CL_BlockInfo *bi;
    int i;

    c->in_tail = 0;

    /* Allocate var slot */
    var_slot = cl_env_add_local(env, var);

    /* Allocate internal limit slot */
    limit_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;

    /* Compile count-form, store into limit_slot */
    compile_expr(c, count_form);
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)limit_slot);
    cl_emit(c, OP_POP);

    /* var = 0 */
    cl_emit_const(c, CL_MAKE_FIXNUM(0));
    cl_emit(c, OP_STORE);
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_POP);

    /* loop_start: LOAD var, LOAD limit, GE, JTRUE -> end */
    loop_start = c->code_pos;
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)var_slot);
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)limit_slot);
    cl_emit(c, OP_GE);
    jtrue_pos = cl_emit_jump(c, OP_JTRUE);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            cl_emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Increment: var = var + 1 */
    cl_emit(c, OP_LOAD);
    cl_emit(c, (uint8_t)var_slot);
    cl_emit_const(c, CL_MAKE_FIXNUM(1));
    cl_emit(c, OP_ADD);
    cl_emit(c, OP_STORE);
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
    CL_Obj vars[CL_MAX_LOCALS];
    CL_Obj inits[CL_MAX_LOCALS];
    CL_Obj steps[CL_MAX_LOCALS];  /* CL_NIL if no step form */
    int has_step[CL_MAX_LOCALS];
    int n = 0;
    int i;
    int loop_start, jtrue_pos;
    int result_slot;
    CL_BlockInfo *bi;

    {
        CL_Obj vc = var_clauses;
        while (!CL_NULL_P(vc) && n < CL_MAX_LOCALS) {
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

    /* Allocate result slot for implicit block NIL */
    result_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info for implicit block NIL */
    bi = &c->blocks[c->block_count++];
    bi->tag = CL_NIL;
    bi->n_patches = 0;
    bi->result_slot = result_slot;

    /* loop_start: compile end-test, JTRUE -> end */
    loop_start = c->code_pos;
    compile_expr(c, end_test);
    jtrue_pos = cl_emit_jump(c, OP_JTRUE);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            cl_emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Parallel step: evaluate all step forms (or load current value) */
    for (i = 0; i < n; i++) {
        if (has_step[i]) {
            compile_expr(c, steps[i]);
        } else {
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)(saved_local_count + i));
        }
    }

    /* Store all back-to-front */
    for (i = n - 1; i >= 0; i--) {
        cl_emit(c, OP_STORE);
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

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

/* --- handler-bind --- */

void compile_handler_bind(CL_Compiler *c, CL_Obj form)
{
    /* (handler-bind ((type handler-expr) ...) body...) */
    CL_Obj clauses = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    int count = 0;
    CL_Obj cl;

    /* For each (type handler) clause: compile handler, push onto handler stack */
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

    /* Compile body as progn */
    compile_progn(c, body);

    /* Normal exit: pop all handler bindings */
    cl_emit(c, OP_HANDLER_POP);
    cl_emit(c, (uint8_t)count);
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
    cl_emit_i16(c, 0);  /* placeholder for landing offset */

    /* Push restart bindings: for each clause, compile lambda + push tag + OP_RESTART_PUSH */
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

void compile_macrolet(CL_Compiler *c, CL_Obj form)
{
    /* (macrolet ((name (params) body...) ...) body...) */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_macro_count = env->local_macro_count;

    /* Compile each macro expander at compile time */
    {
        CL_Obj b = bindings;
        while (!CL_NULL_P(b)) {
            CL_Obj binding = cl_car(b);
            CL_Obj mname = cl_car(binding);
            CL_Obj lambda_list = cl_car(cl_cdr(binding));
            CL_Obj mbody = cl_cdr(cl_cdr(binding));
            CL_Obj lambda_form, bytecode, closure;

            /* Build (lambda (params) body...) */
            lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, mbody));
            CL_GC_PROTECT(lambda_form);

            /* Compile and evaluate at compile time to get closure */
            bytecode = cl_compile(lambda_form);
            CL_GC_PROTECT(bytecode);
            closure = cl_vm_eval(bytecode);
            CL_GC_UNPROTECT(2);

            cl_env_add_local_macro(env, mname, closure);

            b = cl_cdr(b);
        }
    }

    /* Compile body with local macros active */
    compile_body(c, body);

    /* Restore */
    env->local_macro_count = saved_macro_count;
}

/* --- Symbol-macrolet --- */

void compile_symbol_macrolet(CL_Compiler *c, CL_Obj form)
{
    /* (symbol-macrolet ((sym expansion) ...) body...) */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_symbol_macro_count = env->symbol_macro_count;

    /* Register each symbol macro */
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

    /* Compile body with symbol macros active */
    compile_body(c, body);

    /* Restore */
    env->symbol_macro_count = saved_symbol_macro_count;
}
