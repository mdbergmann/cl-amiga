#include "compiler_internal.h"

/* --- Shared globals --- */

CL_Obj macro_table = CL_NIL;
CL_Obj setf_table = CL_NIL;
CL_Obj setf_fn_table = CL_NIL;  /* (setf name) functions: ((accessor . setf-fn-sym) ...) val-first calling */
CL_Obj type_table = CL_NIL;
CL_Obj pending_lambda_name = CL_NIL;

CL_Obj SETF_SYM_CAR = CL_NIL;
CL_Obj SETF_SYM_CDR = CL_NIL;
CL_Obj SETF_SYM_FIRST = CL_NIL;
CL_Obj SETF_SYM_REST = CL_NIL;
CL_Obj SETF_SYM_NTH = CL_NIL;
CL_Obj SETF_SYM_AREF = CL_NIL;
CL_Obj SETF_SYM_SVREF = CL_NIL;
CL_Obj SETF_SYM_CHAR = CL_NIL;
CL_Obj SETF_SYM_SCHAR = CL_NIL;
CL_Obj SETF_SYM_SYMBOL_VALUE = CL_NIL;
CL_Obj SETF_SYM_SYMBOL_FUNCTION = CL_NIL;
CL_Obj SETF_SYM_FDEFINITION = CL_NIL;
CL_Obj SETF_HELPER_NTH = CL_NIL;
CL_Obj SETF_HELPER_SV = CL_NIL;
CL_Obj SETF_HELPER_SF = CL_NIL;
CL_Obj SETF_SYM_GETHASH = CL_NIL;
CL_Obj SETF_HELPER_GETHASH = CL_NIL;
CL_Obj SETF_HELPER_AREF = CL_NIL;
CL_Obj SETF_SYM_ROW_MAJOR_AREF = CL_NIL;
CL_Obj SETF_HELPER_ROW_MAJOR_AREF = CL_NIL;
CL_Obj SETF_SYM_FILL_POINTER = CL_NIL;
CL_Obj SETF_HELPER_FILL_POINTER = CL_NIL;
CL_Obj SETF_SYM_BIT = CL_NIL;
CL_Obj SETF_HELPER_BIT = CL_NIL;
CL_Obj SETF_SYM_SBIT = CL_NIL;
CL_Obj SETF_HELPER_SBIT = CL_NIL;
CL_Obj SETF_SYM_GET = CL_NIL;
CL_Obj SETF_HELPER_GET = CL_NIL;
CL_Obj SETF_SYM_GETF = CL_NIL;
CL_Obj SETF_HELPER_GETF = CL_NIL;

/* Global optimization settings */
CL_OptimizeSettings cl_optimize_settings = {1, 1, 1, 1};

/* --- Code emission --- */

void cl_emit(CL_Compiler *c, uint8_t byte)
{
    if (c->code_pos < CL_MAX_CODE_SIZE) {
        c->code[c->code_pos++] = byte;
    } else {
        cl_error(CL_ERR_OVERFLOW, "Bytecode too large");
    }
}

void cl_emit_u16(CL_Compiler *c, uint16_t val)
{
    cl_emit(c, (uint8_t)(val >> 8));
    cl_emit(c, (uint8_t)(val & 0xFF));
}

void cl_emit_i16(CL_Compiler *c, int16_t val)
{
    cl_emit_u16(c, (uint16_t)val);
}

int cl_add_constant(CL_Compiler *c, CL_Obj obj)
{
    int i;
    /* Check for existing identical constant */
    for (i = 0; i < c->const_count; i++) {
        if (c->constants[i] == obj) return i;
    }
    if (c->const_count >= CL_MAX_CONSTANTS) {
        cl_error(CL_ERR_OVERFLOW, "Too many constants");
        return 0;
    }
    c->constants[c->const_count] = obj;
    return c->const_count++;
}

void cl_emit_const(CL_Compiler *c, CL_Obj obj)
{
    int idx = cl_add_constant(c, obj);
    cl_emit(c, OP_CONST);
    cl_emit_u16(c, (uint16_t)idx);
}

int cl_emit_jump(CL_Compiler *c, uint8_t op)
{
    int pos;
    cl_emit(c, op);
    pos = c->code_pos;
    cl_emit_i16(c, 0); /* placeholder */
    return pos;
}

void cl_patch_jump(CL_Compiler *c, int patch_pos)
{
    int offset = c->code_pos - (patch_pos + 2);
    c->code[patch_pos]     = (uint8_t)(offset >> 8);
    c->code[patch_pos + 1] = (uint8_t)(offset & 0xFF);
}

void cl_emit_loop_jump(CL_Compiler *c, uint8_t op, int target)
{
    cl_emit(c, op);
    cl_emit_i16(c, (int16_t)(target - (c->code_pos + 2)));
}

/* --- Helper --- */

int alloc_temp_slot(CL_CompEnv *env)
{
    int slot = env->local_count;
    env->locals[slot] = CL_NIL;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;
    return slot;
}

/* --- Basic special forms --- */

static void compile_quote(CL_Compiler *c, CL_Obj form)
{
    CL_Obj val = cl_car(cl_cdr(form));
    if (CL_NULL_P(val))
        cl_emit(c, OP_NIL);
    else if (val == SYM_T)
        cl_emit(c, OP_T);
    else
        cl_emit_const(c, val);
}

static void compile_if(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);
    CL_Obj test = cl_car(rest);
    CL_Obj then_form = cl_car(cl_cdr(rest));
    CL_Obj else_form = cl_car(cl_cdr(cl_cdr(rest)));
    int saved_tail = c->in_tail;
    int jnil_pos, jmp_pos;

    c->in_tail = 0;
    compile_expr(c, test);
    jnil_pos = cl_emit_jump(c, OP_JNIL);

    c->in_tail = saved_tail;
    compile_expr(c, then_form);
    jmp_pos = cl_emit_jump(c, OP_JMP);
    cl_patch_jump(c, jnil_pos);

    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(rest)))) {
        compile_expr(c, else_form);
    } else {
        cl_emit(c, OP_NIL);
    }
    cl_patch_jump(c, jmp_pos);
}

void compile_progn(CL_Compiler *c, CL_Obj forms)
{
    if (CL_NULL_P(forms)) {
        cl_emit(c, OP_NIL);
        return;
    }
    while (!CL_NULL_P(forms)) {
        int is_last = CL_NULL_P(cl_cdr(forms));
        if (!is_last) {
            int saved_tail = c->in_tail;
            c->in_tail = 0;
            compile_expr(c, cl_car(forms));
            c->in_tail = saved_tail;
            cl_emit(c, OP_POP);
        } else {
            compile_expr(c, cl_car(forms));
        }
        forms = cl_cdr(forms);
    }
}

/* --- Lambda --- */

static void parse_lambda_list(CL_Obj params, CL_ParsedLambdaList *ll)
{
    CL_Obj p = params;
    int state = 0; /* 0=required, 1=optional, 2=rest, 3=key, 4=aux */

    memset(ll, 0, sizeof(*ll));
    ll->rest_name = CL_NIL;

    while (!CL_NULL_P(p)) {
        CL_Obj item = cl_car(p);
        p = cl_cdr(p);

        if (item == SYM_AMP_OPTIONAL) { state = 1; continue; }
        if (item == SYM_AMP_REST || item == SYM_AMP_BODY) { state = 2; continue; }
        if (item == SYM_AMP_KEY) { state = 3; continue; }
        if (item == SYM_AMP_ALLOW_OTHER_KEYS) { ll->allow_other_keys = 1; continue; }
        if (item == SYM_AMP_AUX) { state = 4; continue; }

        switch (state) {
        case 0:
            ll->required[ll->n_required++] = item;
            break;
        case 1:
            if (CL_CONS_P(item)) {
                ll->opt_names[ll->n_optional] = cl_car(item);
                ll->opt_defaults[ll->n_optional] = cl_car(cl_cdr(item));
            } else {
                ll->opt_names[ll->n_optional] = item;
                ll->opt_defaults[ll->n_optional] = CL_NIL;
            }
            ll->n_optional++;
            break;
        case 2:
            ll->rest_name = item;
            ll->has_rest = 1;
            state = 3;
            break;
        case 3:
            if (CL_CONS_P(item)) {
                ll->key_names[ll->n_keys] = cl_car(item);
                ll->key_defaults[ll->n_keys] = cl_car(cl_cdr(item));
                /* Third element is supplied-p variable: (name default svar) */
                {
                    CL_Obj cddr = cl_cdr(cl_cdr(item));
                    ll->key_suppliedp[ll->n_keys] = CL_NULL_P(cddr) ? CL_NIL : cl_car(cddr);
                }
            } else {
                ll->key_names[ll->n_keys] = item;
                ll->key_defaults[ll->n_keys] = CL_NIL;
                ll->key_suppliedp[ll->n_keys] = CL_NIL;
            }
            {
                const char *name_str = cl_symbol_name(ll->key_names[ll->n_keys]);
                ll->key_keywords[ll->n_keys] = cl_intern_keyword(
                    name_str, (uint32_t)strlen(name_str));
            }
            ll->n_keys++;
            break;
        case 4:
            if (CL_CONS_P(item)) {
                ll->aux_names[ll->n_aux] = cl_car(item);
                ll->aux_inits[ll->n_aux] = cl_car(cl_cdr(item));
            } else {
                ll->aux_names[ll->n_aux] = item;
                ll->aux_inits[ll->n_aux] = CL_NIL;
            }
            ll->n_aux++;
            break;
        }
    }
}

void compile_lambda(CL_Compiler *c, CL_Obj form)
{
    CL_Obj params = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_Compiler *inner;
    CL_CompEnv *env;
    CL_ParsedLambdaList ll;
    CL_Bytecode *bc;
    int const_idx;
    int i;
    int key_slot_indices[CL_MAX_LOCALS];
    int key_suppliedp_indices[CL_MAX_LOCALS];

    parse_lambda_list(params, &ll);

    /* Heap-allocate inner compiler (~45KB — too large for AmigaOS stack) */
    inner = (CL_Compiler *)platform_alloc(sizeof(CL_Compiler));
    if (!inner) return;
    memset(inner, 0, sizeof(*inner));
    env = cl_env_create(c->env);
    inner->env = env;
    inner->in_tail = 1;

    /* Propagate visible block names for cross-closure return-from */
    inner->outer_block_count = 0;
    for (i = 0; i < c->block_count; i++) {
        if (inner->outer_block_count < CL_MAX_BLOCKS)
            inner->outer_blocks[inner->outer_block_count++] = c->blocks[i].tag;
    }
    for (i = 0; i < c->outer_block_count; i++) {
        if (inner->outer_block_count < CL_MAX_BLOCKS)
            inner->outer_blocks[inner->outer_block_count++] = c->outer_blocks[i];
    }

    /* Propagate visible tagbody tags for cross-closure go */
    inner->outer_tag_count = 0;
    for (i = 0; i < c->tagbody_count; i++) {
        CL_TagbodyInfo *tb = &c->tagbodies[i];
        int j;
        if (!tb->uses_nlx) continue;  /* only NLX tagbodies need propagation */
        for (j = 0; j < tb->n_tags; j++) {
            if (inner->outer_tag_count < (int)(sizeof(inner->outer_tags)/sizeof(inner->outer_tags[0]))) {
                inner->outer_tags[inner->outer_tag_count].tag = tb->tags[j].tag;
                inner->outer_tags[inner->outer_tag_count].tagbody_id = tb->id;
                inner->outer_tags[inner->outer_tag_count].tag_index = j;
                inner->outer_tag_count++;
            }
        }
    }
    for (i = 0; i < c->outer_tag_count; i++) {
        if (inner->outer_tag_count < (int)(sizeof(inner->outer_tags)/sizeof(inner->outer_tags[0]))) {
            inner->outer_tags[inner->outer_tag_count] = c->outer_tags[i];
            inner->outer_tag_count++;
        }
    }

    for (i = 0; i < ll.n_required; i++)
        cl_env_add_local(env, ll.required[i]);

    /* Emit prologue for optional defaults.
     * Add each optional local AFTER compiling its default expression,
     * so the default can refer to earlier params but not the current one.
     * This is critical for &optional (*special* *special*) where the
     * default should read the dynamic/global value, not the uninitialized slot. */
    for (i = 0; i < ll.n_optional; i++) {
        if (!CL_NULL_P(ll.opt_defaults[i])) {
            int skip_pos;
            cl_emit(inner, OP_ARGC);
            cl_emit_const(inner, CL_MAKE_FIXNUM(ll.n_required + i + 1));
            cl_emit(inner, OP_GE);
            skip_pos = cl_emit_jump(inner, OP_JTRUE);
            {
                int saved = inner->in_tail;
                inner->in_tail = 0;
                compile_expr(inner, ll.opt_defaults[i]);
                inner->in_tail = saved;
            }
            cl_emit(inner, OP_STORE);
            cl_emit(inner, (uint8_t)(ll.n_required + i));
            cl_emit(inner, OP_POP);
            cl_patch_jump(inner, skip_pos);
        }
        cl_env_add_local(env, ll.opt_names[i]);
    }

    if (ll.has_rest)
        cl_env_add_local(env, ll.rest_name);
    for (i = 0; i < ll.n_keys; i++)
        key_slot_indices[i] = cl_env_add_local(env, ll.key_names[i]);
    /* Always allocate a tracking slot per key for VM-level supplied-p.
     * If user also declared a supplied-p var, reuse the same slot. */
    for (i = 0; i < ll.n_keys; i++) {
        if (!CL_NULL_P(ll.key_suppliedp[i]))
            key_suppliedp_indices[i] = cl_env_add_local(env, ll.key_suppliedp[i]);
        else
            key_suppliedp_indices[i] = alloc_temp_slot(env);
    }
    for (i = 0; i < ll.n_aux; i++)
        cl_env_add_local(env, ll.aux_names[i]);

    /* Emit prologue for key defaults: check the VM-set tracking slot,
     * not the key value itself (which could legitimately be NIL). */
    for (i = 0; i < ll.n_keys; i++) {
        if (!CL_NULL_P(ll.key_defaults[i])) {
            int skip_pos;
            cl_emit(inner, OP_LOAD);
            cl_emit(inner, (uint8_t)key_suppliedp_indices[i]);
            skip_pos = cl_emit_jump(inner, OP_JTRUE);
            {
                int saved = inner->in_tail;
                inner->in_tail = 0;
                compile_expr(inner, ll.key_defaults[i]);
                inner->in_tail = saved;
            }
            cl_emit(inner, OP_STORE);
            cl_emit(inner, (uint8_t)key_slot_indices[i]);
            cl_emit(inner, OP_POP);
            cl_patch_jump(inner, skip_pos);
        }
    }

    /* Emit prologue for &aux bindings */
    for (i = 0; i < ll.n_aux; i++) {
        int aux_slot = cl_env_lookup(env, ll.aux_names[i]);
        {
            int saved = inner->in_tail;
            inner->in_tail = 0;
            compile_expr(inner, ll.aux_inits[i]);
            inner->in_tail = saved;
        }
        cl_emit(inner, OP_STORE);
        cl_emit(inner, (uint8_t)aux_slot);
        cl_emit(inner, OP_POP);
    }

    /* Box params that are both mutated and captured across closure boundary */
    {
        CL_Obj param_vars[CL_MAX_LOCALS];
        int param_slots[CL_MAX_LOCALS];
        int n_params = 0;
        uint8_t needs_boxing[CL_MAX_LOCALS];

        for (i = 0; i < ll.n_required; i++) {
            param_slots[n_params] = cl_env_lookup(env, ll.required[i]);
            param_vars[n_params] = ll.required[i];
            n_params++;
        }
        for (i = 0; i < ll.n_optional; i++) {
            param_slots[n_params] = cl_env_lookup(env, ll.opt_names[i]);
            param_vars[n_params] = ll.opt_names[i];
            n_params++;
        }
        if (ll.has_rest) {
            param_slots[n_params] = cl_env_lookup(env, ll.rest_name);
            param_vars[n_params] = ll.rest_name;
            n_params++;
        }
        for (i = 0; i < ll.n_keys; i++) {
            param_slots[n_params] = key_slot_indices[i];
            param_vars[n_params] = ll.key_names[i];
            n_params++;
        }
        for (i = 0; i < ll.n_aux; i++) {
            param_slots[n_params] = cl_env_lookup(env, ll.aux_names[i]);
            param_vars[n_params] = ll.aux_names[i];
            n_params++;
        }

        if (n_params > 0) {
            determine_boxed_vars(body, param_vars, n_params, needs_boxing);
            for (i = 0; i < n_params; i++) {
                if (needs_boxing[i] && param_slots[i] >= 0) {
                    cl_emit(inner, OP_LOAD);
                    cl_emit(inner, (uint8_t)param_slots[i]);
                    cl_emit(inner, OP_MAKE_CELL);
                    cl_emit(inner, OP_STORE);
                    cl_emit(inner, (uint8_t)param_slots[i]);
                    cl_emit(inner, OP_POP);
                    env->boxed[param_slots[i]] = 1;
                }
            }
        }
    }

    compile_body(inner, body);
    cl_emit(inner, OP_RET);

    /* Build bytecode object */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) { cl_env_destroy(env); platform_free(inner); return; }

    bc->code = (uint8_t *)platform_alloc(inner->code_pos);
    if (bc->code) memcpy(bc->code, inner->code, inner->code_pos);
    bc->code_len = inner->code_pos;

    if (inner->const_count > 0) {
        bc->constants = (CL_Obj *)platform_alloc(
            inner->const_count * sizeof(CL_Obj));
        if (bc->constants) {
            for (i = 0; i < inner->const_count; i++)
                bc->constants[i] = inner->constants[i];
        }
        bc->n_constants = inner->const_count;
    } else {
        bc->constants = NULL;
        bc->n_constants = 0;
    }

    bc->arity = ll.has_rest ? (ll.n_required | 0x8000) : ll.n_required;
    bc->n_locals = env->max_locals;
    bc->n_upvalues = env->upvalue_count;
    bc->name = pending_lambda_name;
    pending_lambda_name = CL_NIL;
    bc->n_optional = (uint8_t)ll.n_optional;
    bc->flags = (ll.n_keys > 0 ? 1 : 0) | (ll.allow_other_keys ? 2 : 0);
    bc->n_keys = (uint8_t)ll.n_keys;

    if (ll.n_keys > 0) {
        bc->key_syms = (CL_Obj *)platform_alloc(ll.n_keys * sizeof(CL_Obj));
        bc->key_slots = (uint8_t *)platform_alloc(ll.n_keys);
        bc->key_suppliedp_slots = (uint8_t *)platform_alloc(ll.n_keys);
        for (i = 0; i < ll.n_keys; i++) {
            bc->key_syms[i] = ll.key_keywords[i];
            bc->key_slots[i] = (uint8_t)key_slot_indices[i];
            bc->key_suppliedp_slots[i] = (uint8_t)key_suppliedp_indices[i];
        }
    } else {
        bc->key_syms = NULL;
        bc->key_slots = NULL;
        bc->key_suppliedp_slots = NULL;
    }

    /* Transfer source line map */
    if (inner->line_entry_count > 0) {
        bc->line_map = (CL_LineEntry *)platform_alloc(
            inner->line_entry_count * sizeof(CL_LineEntry));
        if (bc->line_map) {
            for (i = 0; i < inner->line_entry_count; i++)
                bc->line_map[i] = inner->line_entries[i];
        }
        bc->line_map_count = (uint16_t)inner->line_entry_count;
    } else {
        bc->line_map = NULL;
        bc->line_map_count = 0;
    }
    bc->source_line = (uint16_t)c->current_line;
    bc->source_file = cl_current_source_file;

    const_idx = cl_add_constant(c, CL_PTR_TO_OBJ(bc));
    cl_emit(c, OP_CLOSURE);
    cl_emit_u16(c, (uint16_t)const_idx);

    for (i = 0; i < env->upvalue_count; i++) {
        cl_emit(c, (uint8_t)env->upvalues[i].is_local);
        cl_emit(c, (uint8_t)env->upvalues[i].index);
    }

    cl_env_destroy(env);
    platform_free(inner);
}

/* --- Boxing analysis (pre-scan for mutable closure bindings) --- */

/* Check if sym is one of the tracked vars; returns index or -1 */
static int find_var_index(CL_Obj sym, CL_Obj *vars, int n_vars)
{
    int i;
    /* Search from end so shadowed variables (let*) resolve to innermost binding */
    for (i = n_vars - 1; i >= 0; i--) {
        if (vars[i] == sym) return i;
    }
    return -1;
}

/* Walk a quasiquote template, only scanning UNQUOTE/UNQUOTE-SPLICING subforms.
 * Template structure (literal symbols, lists used as structure) is skipped. */
static void scan_qq_for_boxing(CL_Obj tmpl, CL_Obj *vars, int n_vars,
                               uint8_t *mutated, uint8_t *captured,
                               int closure_depth)
{
    if (!CL_CONS_P(tmpl)) return;

    if (cl_car(tmpl) == SYM_UNQUOTE || cl_car(tmpl) == SYM_UNQUOTE_SPLICING) {
        /* The unquoted expression is evaluated at runtime — scan it */
        scan_body_for_boxing(cl_car(cl_cdr(tmpl)), vars, n_vars,
                             mutated, captured, closure_depth);
        return;
    }

    /* Nested quasiquote — skip deeper nesting levels */
    if (cl_car(tmpl) == SYM_QUASIQUOTE) return;

    /* Walk list elements looking for unquotes */
    {
        CL_Obj cur = tmpl;
        while (CL_CONS_P(cur)) {
            scan_qq_for_boxing(cl_car(cur), vars, n_vars,
                               mutated, captured, closure_depth);
            cur = cl_cdr(cur);
        }
    }
}

/*
 * Recursive tree walker that determines which vars are mutated (target of setq)
 * and which are captured (referenced inside a lambda/flet/labels body).
 * closure_depth starts at 0; increments when entering closure-creating forms.
 */
void scan_body_for_boxing(CL_Obj form, CL_Obj *vars, int n_vars,
                          uint8_t *mutated, uint8_t *captured,
                          int closure_depth)
{
    CL_Obj head, rest;

    if (CL_NULL_P(form) || CL_FIXNUM_P(form) || CL_CHAR_P(form))
        return;

    /* Symbol reference */
    if (CL_SYMBOL_P(form)) {
        if (closure_depth > 0) {
            int idx = find_var_index(form, vars, n_vars);
            if (idx >= 0) captured[idx] = 1;
        }
        return;
    }

    if (!CL_CONS_P(form)) return;

    head = cl_car(form);
    rest = cl_cdr(form);

    /* (quote ...) — skip entirely */
    if (head == SYM_QUOTE) return;

    /* (quasiquote tmpl) — only scan unquoted subforms, not the template
     * structure itself. Without this, macro calls inside backquote templates
     * get spuriously expanded by the scanner with raw UNQUOTE forms. */
    if (head == SYM_QUASIQUOTE) {
        scan_qq_for_boxing(cl_car(rest), vars, n_vars,
                           mutated, captured, closure_depth);
        return;
    }

    /* (setq var val var val ...) or (setf place val place val ...) */
    if (head == SYM_SETQ || head == SYM_SETF) {
        CL_Obj pairs = rest;
        while (CL_CONS_P(pairs) && CL_CONS_P(cl_cdr(pairs))) {
            CL_Obj place = cl_car(pairs);
            CL_Obj val = cl_car(cl_cdr(pairs));
            /* For symbols (simple variable places), mark as mutated */
            if (CL_SYMBOL_P(place)) {
                int idx = find_var_index(place, vars, n_vars);
                if (idx >= 0) mutated[idx] = 1;
                if (closure_depth > 0 && idx >= 0) captured[idx] = 1;
            } else {
                /* Generalized place — scan sub-expressions for references */
                scan_body_for_boxing(place, vars, n_vars,
                                     mutated, captured, closure_depth);
            }
            scan_body_for_boxing(val, vars, n_vars, mutated, captured, closure_depth);
            pairs = cl_cdr(cl_cdr(pairs));
        }
        return;
    }

    /* (lambda params . body) — body is at increased closure depth */
    if (head == SYM_LAMBDA) {
        CL_Obj body;
        if (!CL_CONS_P(rest)) return; /* malformed lambda */
        body = cl_cdr(rest); /* skip param list */
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth + 1);
            body = cl_cdr(body);
        }
        return;
    }

    /* (defun name params . body) — body is at increased closure depth */
    if (head == SYM_DEFUN) {
        CL_Obj body;
        if (!CL_CONS_P(rest) || !CL_CONS_P(cl_cdr(rest))) return;
        body = cl_cdr(cl_cdr(rest)); /* skip name and param list */
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth + 1);
            body = cl_cdr(body);
        }
        return;
    }

    /* (defmacro ...) — skip entirely; macro bodies are template code
     * that shouldn't be scanned for variable mutations */
    if (head == SYM_DEFMACRO) return;

    /* (flet/labels ((name params . body) ...) . body) */
    if (head == SYM_FLET || head == SYM_LABELS) {
        CL_Obj defs, body;
        if (!CL_CONS_P(rest)) return;
        defs = cl_car(rest);
        body = cl_cdr(rest);
        /* Scan each function definition body at increased depth */
        while (CL_CONS_P(defs)) {
            CL_Obj def = cl_car(defs);
            CL_Obj fbody;
            if (!CL_CONS_P(def) || !CL_CONS_P(cl_cdr(def))) { defs = cl_cdr(defs); continue; }
            fbody = cl_cdr(cl_cdr(def)); /* skip name and params */
            while (CL_CONS_P(fbody)) {
                scan_body_for_boxing(cl_car(fbody), vars, n_vars,
                                     mutated, captured, closure_depth + 1);
                fbody = cl_cdr(fbody);
            }
            defs = cl_cdr(defs);
        }
        /* Scan outer body at current depth */
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

    /* Expand macros before scanning so we can see through macro-generated
     * lambdas and setf forms. Save/restore VM state to handle expansion
     * errors gracefully without corrupting compiler state. */
    if (CL_SYMBOL_P(head) && cl_macro_p(head)) {
        int saved_sp = cl_vm.sp;
        int saved_fp = cl_vm.fp;
        int saved_dyn = cl_dyn_top;
        int saved_nlx = cl_nlx_top;
        int saved_handler = cl_handler_top;
        int saved_restart = cl_restart_top;
        int saved_debugger = cl_debugger_enabled;
        cl_debugger_enabled = 0;  /* Suppress debugger during expansion */
#ifdef DEBUG_SCANNER
        fprintf(stderr, "[scanner] expanding macro: %s\n", cl_symbol_name(head));
#endif
        int err = CL_CATCH();
        if (err == 0) {
            CL_Obj expanded = cl_macroexpand_1(form);
            CL_UNCATCH();
            cl_debugger_enabled = saved_debugger;
            if (expanded != form) {
                scan_body_for_boxing(expanded, vars, n_vars,
                                     mutated, captured, closure_depth);
                return;
            }
        } else {
            CL_UNCATCH();
            /* Macro expansion failed — restore VM state and fall through */
            cl_vm.sp = saved_sp;
            cl_vm.fp = saved_fp;
            cl_dynbind_restore_to(saved_dyn);
            cl_nlx_top = saved_nlx;
            cl_handler_top = saved_handler;
            cl_restart_top = saved_restart;
            cl_debugger_enabled = saved_debugger;
        }
    }

    /* General form: scan all sub-expressions */
    {
        CL_Obj cur = form;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n_vars,
                                 mutated, captured, closure_depth);
            cur = cl_cdr(cur);
        }
    }
}

/*
 * Determine which vars need boxing (both mutated AND captured across closure boundary).
 * body is the list of body forms to scan.
 * Sets boxed_out[i] = 1 for each var that needs boxing.
 */
void determine_boxed_vars(CL_Obj body, CL_Obj *vars, int n_vars,
                          uint8_t *boxed_out)
{
    uint8_t mutated[CL_MAX_LOCALS];
    uint8_t captured[CL_MAX_LOCALS];
    int i;

    memset(mutated, 0, n_vars);
    memset(captured, 0, n_vars);
    memset(boxed_out, 0, n_vars);

    /* Scan all body forms */
    {
        CL_Obj cur = body;
        while (CL_CONS_P(cur)) {
            scan_body_for_boxing(cl_car(cur), vars, n_vars,
                                 mutated, captured, 0);
            cur = cl_cdr(cur);
        }
    }

    for (i = 0; i < n_vars; i++) {
        boxed_out[i] = (mutated[i] && captured[i]) ? 1 : 0;
    }
}

/* --- Let --- */

static int var_is_special(CL_Obj var, CL_Obj local_specials)
{
    return cl_symbol_specialp(var) || is_locally_special(var, local_specials);
}

static void compile_let(CL_Compiler *c, CL_Obj form, int sequential)
{
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int special_count = 0;

    /* Pre-scan body for (declare (special ...)) to find locally-special vars */
    CL_Obj local_specials = scan_local_specials(body);

    if (sequential) {
        /* Pre-scan all bindings + body for boxing analysis */
        CL_Obj all_vars[CL_MAX_LOCALS];
        uint8_t needs_boxing[CL_MAX_LOCALS];
        int n_all = 0;
        {
            CL_Obj b = bindings;
            while (!CL_NULL_P(b) && n_all < CL_MAX_LOCALS) {
                CL_Obj binding = cl_car(b);
                all_vars[n_all++] = CL_CONS_P(binding) ? cl_car(binding) : binding;
                b = cl_cdr(b);
            }
        }
        {
            uint8_t mutated[CL_MAX_LOCALS], captured[CL_MAX_LOCALS];
            int bi;
            CL_Obj b;
            memset(mutated, 0, (size_t)n_all);
            memset(captured, 0, (size_t)n_all);
            /* Scan each binding's init-form against only the vars defined
               so far (not including the current binding).  This ensures
               (let* ((x 10) (x (1+ x)))) resolves the init-form reference
               to the first x, not the second (which doesn't exist yet). */
            {
                int scan_count = 0;
                b = bindings;
                while (!CL_NULL_P(b)) {
                    CL_Obj binding = cl_car(b);
                    if (CL_CONS_P(binding))
                        scan_body_for_boxing(cl_car(cl_cdr(binding)), all_vars,
                                             scan_count, mutated, captured, 0);
                    scan_count++;
                    b = cl_cdr(b);
                }
            }
            {
                CL_Obj cur = body;
                while (CL_CONS_P(cur)) {
                    scan_body_for_boxing(cl_car(cur), all_vars, n_all,
                                         mutated, captured, 0);
                    cur = cl_cdr(cur);
                }
            }
            for (bi = 0; bi < n_all; bi++)
                needs_boxing[bi] = (mutated[bi] && captured[bi]) ? 1 : 0;
        }

        /* Compile bindings, boxing after each store if needed */
        {
            int var_idx = 0;
            while (!CL_NULL_P(bindings)) {
                CL_Obj binding = cl_car(bindings);
                CL_Obj var, val;

                if (CL_CONS_P(binding)) {
                    var = cl_car(binding);
                    val = cl_car(cl_cdr(binding));
                } else {
                    var = binding;
                    val = CL_NIL;
                }

                c->in_tail = 0;
                compile_expr(c, val);

                if (var_is_special(var, local_specials)) {
                    int idx = cl_add_constant(c, var);
                    cl_emit(c, OP_DYNBIND);
                    cl_emit_u16(c, (uint16_t)idx);
                    special_count++;
                } else {
                    int slot = cl_env_add_local(env, var);
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)slot);
                    cl_emit(c, OP_POP);
                    if (needs_boxing[var_idx]) {
                        cl_emit(c, OP_LOAD);
                        cl_emit(c, (uint8_t)slot);
                        cl_emit(c, OP_MAKE_CELL);
                        cl_emit(c, OP_STORE);
                        cl_emit(c, (uint8_t)slot);
                        cl_emit(c, OP_POP);
                        env->boxed[slot] = 1;
                    }
                }
                var_idx++;
                bindings = cl_cdr(bindings);
            }
        }
    } else {
        CL_Obj vars[CL_MAX_LOCALS];
        CL_Obj vals[CL_MAX_LOCALS];
        int n = 0, i;
        CL_Obj b = bindings;

        while (!CL_NULL_P(b) && n < CL_MAX_LOCALS) {
            CL_Obj binding = cl_car(b);
            if (CL_CONS_P(binding)) {
                vars[n] = cl_car(binding);
                vals[n] = cl_car(cl_cdr(binding));
            } else {
                vars[n] = binding;
                vals[n] = CL_NIL;
            }
            n++;
            b = cl_cdr(b);
        }

        for (i = 0; i < n; i++) {
            c->in_tail = 0;
            compile_expr(c, vals[i]);
        }

        {
            int lexical_slots[CL_MAX_LOCALS];
            for (i = 0; i < n; i++) {
                if (var_is_special(vars[i], local_specials)) {
                    lexical_slots[i] = -1;
                } else {
                    lexical_slots[i] = cl_env_add_local(env, vars[i]);
                }
            }
            for (i = n - 1; i >= 0; i--) {
                if (lexical_slots[i] >= 0) {
                    cl_emit(c, OP_STORE);
                    cl_emit(c, (uint8_t)lexical_slots[i]);
                    cl_emit(c, OP_POP);
                } else {
                    int idx = cl_add_constant(c, vars[i]);
                    cl_emit(c, OP_DYNBIND);
                    cl_emit_u16(c, (uint16_t)idx);
                    special_count++;
                }
            }

            /* Box lexical vars that are both mutated and captured */
            {
                CL_Obj lex_vars[CL_MAX_LOCALS];
                int lex_slots[CL_MAX_LOCALS];
                int n_lex = 0;
                uint8_t needs_boxing[CL_MAX_LOCALS];

                for (i = 0; i < n; i++) {
                    if (lexical_slots[i] >= 0) {
                        lex_vars[n_lex] = vars[i];
                        lex_slots[n_lex] = lexical_slots[i];
                        n_lex++;
                    }
                }
                if (n_lex > 0) {
                    determine_boxed_vars(body, lex_vars, n_lex, needs_boxing);
                    for (i = 0; i < n_lex; i++) {
                        if (needs_boxing[i]) {
                            cl_emit(c, OP_LOAD);
                            cl_emit(c, (uint8_t)lex_slots[i]);
                            cl_emit(c, OP_MAKE_CELL);
                            cl_emit(c, OP_STORE);
                            cl_emit(c, (uint8_t)lex_slots[i]);
                            cl_emit(c, OP_POP);
                            env->boxed[lex_slots[i]] = 1;
                        }
                    }
                }
            }
        }
    }

    /* If we have dynamic bindings, disable tail calls in the body so that
     * OP_DYNUNBIND executes before the function returns. Without this,
     * a tail call would replace the frame and skip the unwind. */
    c->in_tail = (special_count > 0) ? 0 : saved_tail;
    c->special_depth += special_count;
    compile_body(c, body);
    c->special_depth -= special_count;

    if (special_count > 0) {
        cl_emit(c, OP_DYNUNBIND);
        cl_emit(c, (uint8_t)special_count);
    }

    env->local_count = saved_local_count;
}

/* --- Setq / Setf --- */

static void compile_setf_place(CL_Compiler *c, CL_Obj place, CL_Obj val_form);

static void compile_setq(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);

    while (!CL_NULL_P(rest)) {
        CL_Obj var = cl_car(rest);
        CL_Obj val = cl_car(cl_cdr(rest));
        int slot;
        int saved_tail = c->in_tail;

        /* Check if var is a symbol macro — rewrite as (setf expansion val) */
        if (CL_SYMBOL_P(var)) {
            CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, var);
            if (!CL_NULL_P(expansion)) {
                compile_setf_place(c, expansion, val);
                rest = cl_cdr(cl_cdr(rest));
                if (!CL_NULL_P(rest)) {
                    cl_emit(c, OP_POP);
                }
                continue;
            }
        }

        /* Check for constant symbols */
        if (CL_SYMBOL_P(var)) {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(var);
            if (sym->flags & CL_SYM_CONSTANT) {
                cl_error(CL_ERR_GENERAL, "Cannot assign to constant variable: %s",
                         cl_symbol_name(var));
            }
        }

        c->in_tail = 0;
        compile_expr(c, val);
        c->in_tail = saved_tail;

        slot = cl_env_lookup(c->env, var);
        if (slot >= 0) {
            if (c->env->boxed[slot]) {
                cl_emit(c, OP_CELL_SET_LOCAL);
                cl_emit(c, (uint8_t)slot);
            } else {
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)slot);
            }
        } else if (c->env) {
            int uv_idx = cl_env_resolve_upvalue(c->env, var);
            if (uv_idx >= 0) {
                if (c->env->upvalues[uv_idx].is_boxed) {
                    cl_emit(c, OP_CELL_SET_UPVAL);
                    cl_emit(c, (uint8_t)uv_idx);
                } else {
                    /* Upvalue is not boxed — cannot mutate across closure boundary.
                     * This happens when the boxing scan missed a mutation (e.g. via macro).
                     * Fall back to global store as safety measure. */
                    int idx = cl_add_constant(c, var);
                    cl_emit(c, OP_GSTORE);
                    cl_emit_u16(c, (uint16_t)idx);
                }
            } else {
                int idx = cl_add_constant(c, var);
                cl_emit(c, OP_GSTORE);
                cl_emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = cl_add_constant(c, var);
            cl_emit(c, OP_GSTORE);
            cl_emit_u16(c, (uint16_t)idx);
        }

        rest = cl_cdr(cl_cdr(rest));
        if (!CL_NULL_P(rest)) {
            cl_emit(c, OP_POP);
        }
    }
}

static void compile_setf_place(CL_Compiler *c, CL_Obj place, CL_Obj val_form)
{
    int saved_tail = c->in_tail;
    c->in_tail = 0;

    if (CL_SYMBOL_P(place)) {
        int slot;
        /* Check if place is a symbol macro — rewrite as (setf expansion val) */
        CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, place);
        if (!CL_NULL_P(expansion)) {
            compile_setf_place(c, expansion, val_form);
            c->in_tail = saved_tail;
            return;
        }
        /* Check for constant symbols */
        {
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(place);
            if (sym->flags & CL_SYM_CONSTANT) {
                cl_error(CL_ERR_GENERAL, "Cannot assign to constant variable: %s",
                         cl_symbol_name(place));
            }
        }
        compile_expr(c, val_form);
        slot = cl_env_lookup(c->env, place);
        if (slot >= 0) {
            if (c->env->boxed[slot]) {
                cl_emit(c, OP_CELL_SET_LOCAL);
                cl_emit(c, (uint8_t)slot);
            } else {
                cl_emit(c, OP_STORE);
                cl_emit(c, (uint8_t)slot);
            }
        } else if (c->env) {
            int uv_idx = cl_env_resolve_upvalue(c->env, place);
            if (uv_idx >= 0) {
                if (c->env->upvalues[uv_idx].is_boxed) {
                    cl_emit(c, OP_CELL_SET_UPVAL);
                    cl_emit(c, (uint8_t)uv_idx);
                } else {
                    /* Upvalue not boxed — fall back to global store */
                    int idx = cl_add_constant(c, place);
                    cl_emit(c, OP_GSTORE);
                    cl_emit_u16(c, (uint16_t)idx);
                }
            } else {
                int idx = cl_add_constant(c, place);
                cl_emit(c, OP_GSTORE);
                cl_emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = cl_add_constant(c, place);
            cl_emit(c, OP_GSTORE);
            cl_emit_u16(c, (uint16_t)idx);
        }
    } else if (CL_CONS_P(place)) {
        CL_Obj head = cl_car(place);

        if (head == SETF_SYM_CAR || head == SETF_SYM_FIRST) {
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_RPLACA);
        } else if (head == SETF_SYM_CDR || head == SETF_SYM_REST) {
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_RPLACD);
        } else if (head == SETF_SYM_AREF || head == SETF_SYM_SVREF ||
                   head == SETF_SYM_CHAR || head == SETF_SYM_SCHAR) {
            /* Count indices: place = (aref arr idx1 idx2 ...) */
            CL_Obj indices = cl_cdr(cl_cdr(place));
            int nindices = 0;
            CL_Obj tmp = indices;
            while (!CL_NULL_P(tmp)) { nindices++; tmp = cl_cdr(tmp); }

            if (nindices <= 1 || head == SETF_SYM_SVREF) {
                /* 1D fast path: use OP_ASET */
                compile_expr(c, cl_car(cl_cdr(place)));
                compile_expr(c, cl_car(indices));
                compile_expr(c, val_form);
                cl_emit(c, OP_ASET);
            } else {
                /* Multi-dim: call %SETF-AREF(array, val, idx1, idx2, ...) */
                int ci = cl_add_constant(c, SETF_HELPER_AREF);
                cl_emit(c, OP_FLOAD);
                cl_emit_u16(c, (uint16_t)ci);
                compile_expr(c, cl_car(cl_cdr(place)));  /* array */
                compile_expr(c, val_form);               /* value */
                tmp = indices;
                while (!CL_NULL_P(tmp)) {
                    compile_expr(c, cl_car(tmp));
                    tmp = cl_cdr(tmp);
                }
                cl_emit(c, OP_CALL);
                cl_emit(c, (uint8_t)(2 + nindices));
            }
        } else if (head == SETF_SYM_NTH) {
            int idx = cl_add_constant(c, SETF_HELPER_NTH);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place))));
            compile_expr(c, val_form);
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_SYMBOL_VALUE) {
            int idx = cl_add_constant(c, SETF_HELPER_SV);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_CALL);
            cl_emit(c, 2);
        } else if (head == SETF_SYM_SYMBOL_FUNCTION || head == SETF_SYM_FDEFINITION) {
            int idx = cl_add_constant(c, SETF_HELPER_SF);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_CALL);
            cl_emit(c, 2);
        } else if (head == SETF_SYM_ROW_MAJOR_AREF) {
            /* (setf (row-major-aref arr idx) val) → (%setf-row-major-aref arr idx val) */
            int idx = cl_add_constant(c, SETF_HELPER_ROW_MAJOR_AREF);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* array */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* index */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_FILL_POINTER) {
            /* (setf (fill-pointer vec) val) → (%setf-fill-pointer vec val) */
            int idx = cl_add_constant(c, SETF_HELPER_FILL_POINTER);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* vector */
            compile_expr(c, val_form);                     /* new fill-pointer */
            cl_emit(c, OP_CALL);
            cl_emit(c, 2);
        } else if (head == SETF_SYM_GETHASH) {
            /* (setf (gethash key ht) val) → (%setf-gethash key ht val) */
            int idx = cl_add_constant(c, SETF_HELPER_GETHASH);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* key */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* hash-table */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_BIT) {
            /* (setf (bit bv idx) val) → (%setf-bit bv idx val) */
            int idx = cl_add_constant(c, SETF_HELPER_BIT);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* bit-vector */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* index */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_SBIT) {
            /* (setf (sbit bv idx) val) → (%setf-sbit bv idx val) */
            int idx = cl_add_constant(c, SETF_HELPER_SBIT);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* bit-vector */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* index */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_GET) {
            /* (setf (get sym indicator) val) → (%setf-get sym indicator val) */
            int idx = cl_add_constant(c, SETF_HELPER_GET);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* symbol */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* indicator */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_GETF) {
            /* (setf (getf plist indicator) val) → (%setf-getf plist indicator val) */
            int idx = cl_add_constant(c, SETF_HELPER_GETF);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, cl_car(cl_cdr(place)));       /* plist-place */
            compile_expr(c, cl_car(cl_cdr(cl_cdr(place)))); /* indicator */
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else {
            /* Check defsetf table */
            CL_Obj entry = setf_table;
            CL_Obj updater = CL_NIL;
            int found = 0;
            while (!CL_NULL_P(entry)) {
                CL_Obj pair = cl_car(entry);
                if (cl_car(pair) == head) {
                    updater = cl_cdr(pair);
                    found = 1;
                    break;
                }
                entry = cl_cdr(entry);
            }
            if (found) {
                CL_Obj args = cl_cdr(place);
                int nargs = 0;
                int idx = cl_add_constant(c, updater);
                cl_emit(c, OP_FLOAD);
                cl_emit_u16(c, (uint16_t)idx);
                while (!CL_NULL_P(args)) {
                    compile_expr(c, cl_car(args));
                    nargs++;
                    args = cl_cdr(args);
                }
                compile_expr(c, val_form);
                nargs++;
                cl_emit(c, OP_CALL);
                cl_emit(c, (uint8_t)nargs);
            } else {
                /* Setf function (late-bound): construct %SETF-<name> symbol,
                 * emit FLOAD — resolved at runtime like normal function calls.
                 * Handles both (defun (setf name) ...) and
                 * (defgeneric (setf name) ...) / (defmethod (setf name) ...).
                 * Check setf_fn_table first; if not found, intern %SETF-<name>. */
                CL_Obj setf_fn = CL_NIL;
                {
                    CL_Obj sfe = setf_fn_table;
                    while (!CL_NULL_P(sfe)) {
                        CL_Obj pair = cl_car(sfe);
                        if (cl_car(pair) == head) {
                            setf_fn = cl_cdr(pair);
                            break;
                        }
                        sfe = cl_cdr(sfe);
                    }
                }
                if (CL_NULL_P(setf_fn)) {
                    /* Optimistic late binding: intern %SETF-<name> in the
                     * same package as the accessor symbol, so it matches
                     * the setter created by defclass :accessor */
                    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(head);
                    CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
                    CL_Obj pkg = sym->package;
                    char buf[128];
                    int len = snprintf(buf, sizeof(buf), "%%SETF-%.*s",
                                       (int)sname->length, sname->data);
                    if (CL_NULL_P(pkg)) pkg = cl_package_cl;
                    setf_fn = cl_intern_in(buf, (uint32_t)len, pkg);
                }
                {
                    /* Late-bound setf: (setf (foo a b) val) → (%setf-foo val a b)
                     * Value first — matches CL (setf name) function convention */
                    CL_Obj args = cl_cdr(place);
                    int nargs = 0;
                    int idx = cl_add_constant(c, setf_fn);
                    cl_emit(c, OP_FLOAD);
                    cl_emit_u16(c, (uint16_t)idx);
                    compile_expr(c, val_form);  /* new-value FIRST */
                    nargs++;
                    while (!CL_NULL_P(args)) {
                        compile_expr(c, cl_car(args));
                        nargs++;
                        args = cl_cdr(args);
                    }
                    cl_emit(c, OP_CALL);
                    cl_emit(c, (uint8_t)nargs);
                }
            }
        }
    } else {
        if (CL_CONS_P(place) && CL_SYMBOL_P(cl_car(place)))
            cl_error(CL_ERR_GENERAL, "SETF: invalid place (%s ...)",
                     cl_symbol_name(cl_car(place)));
        else
            cl_error(CL_ERR_GENERAL, "SETF: invalid place");
    }

    c->in_tail = saved_tail;
}

static void compile_setf(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);

    while (!CL_NULL_P(rest)) {
        CL_Obj place = cl_car(rest);
        CL_Obj val_form;

        if (CL_NULL_P(cl_cdr(rest)))
            cl_error(CL_ERR_ARGS, "SETF: odd number of arguments");
        val_form = cl_car(cl_cdr(rest));

        compile_setf_place(c, place, val_form);

        rest = cl_cdr(cl_cdr(rest));
        if (!CL_NULL_P(rest)) {
            cl_emit(c, OP_POP);
        }
    }
}

/* --- Function ref --- */

static void compile_function(CL_Compiler *c, CL_Obj form)
{
    CL_Obj name = cl_car(cl_cdr(form));

    if (CL_CONS_P(name) && cl_car(name) == SYM_LAMBDA) {
        compile_lambda(c, name);
    } else if (CL_SYMBOL_P(name)) {
        int fun_slot = cl_env_lookup_local_fun(c->env, name);
        if (fun_slot >= 0) {
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)fun_slot);
            if (c->env->boxed[fun_slot])
                cl_emit(c, OP_CELL_REF);
        } else if (c->env) {
            int uv_idx = cl_env_resolve_fun_upvalue(c->env, name);
            if (uv_idx >= 0) {
                cl_emit(c, OP_UPVAL);
                cl_emit(c, (uint8_t)uv_idx);
                if (c->env->upvalues[uv_idx].is_boxed)
                    cl_emit(c, OP_CELL_REF);
            } else {
                int idx = cl_add_constant(c, name);
                cl_emit(c, OP_FLOAD);
                cl_emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = cl_add_constant(c, name);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
        }
    } else {
        int idx = cl_add_constant(c, name);
        cl_emit(c, OP_FLOAD);
        cl_emit_u16(c, (uint16_t)idx);
    }
}

/* --- Call --- */

static void compile_call(CL_Compiler *c, CL_Obj form)
{
    CL_Obj func = cl_car(form);
    CL_Obj args = cl_cdr(form);
    int nargs = 0;
    int saved_tail = c->in_tail;

    c->in_tail = 0;

    if (CL_SYMBOL_P(func)) {
        int fun_slot = cl_env_lookup_local_fun(c->env, func);
        if (fun_slot >= 0) {
            cl_emit(c, OP_LOAD);
            cl_emit(c, (uint8_t)fun_slot);
            if (c->env->boxed[fun_slot])
                cl_emit(c, OP_CELL_REF);
        } else if (c->env) {
            int uv_idx = cl_env_resolve_fun_upvalue(c->env, func);
            if (uv_idx >= 0) {
                cl_emit(c, OP_UPVAL);
                cl_emit(c, (uint8_t)uv_idx);
                if (c->env->upvalues[uv_idx].is_boxed)
                    cl_emit(c, OP_CELL_REF);
            } else {
                int idx = cl_add_constant(c, func);
                cl_emit(c, OP_FLOAD);
                cl_emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = cl_add_constant(c, func);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
        }
    } else {
        compile_expr(c, func);
    }

    while (!CL_NULL_P(args)) {
        compile_expr(c, cl_car(args));
        nargs++;
        args = cl_cdr(args);
    }

    c->in_tail = saved_tail;

    if (c->in_tail) {
        cl_emit(c, OP_TAILCALL);
    } else {
        cl_emit(c, OP_CALL);
    }
    cl_emit(c, (uint8_t)nargs);
}

void compile_body(CL_Compiler *c, CL_Obj forms)
{
    CL_Obj rest = process_body_declarations(c, forms);
    compile_progn(c, rest);
}

static void compile_symbol(CL_Compiler *c, CL_Obj sym)
{
    int slot;

    if (CL_NULL_P(sym)) { cl_emit(c, OP_NIL); return; }
    if (sym == SYM_T)    { cl_emit(c, OP_T);   return; }

    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->package == cl_package_keyword) {
            cl_emit_const(c, sym);
            return;
        }
    }

    /* Check symbol macros before variable lookup */
    {
        CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, sym);
        if (!CL_NULL_P(expansion)) {
            compile_expr(c, expansion);
            return;
        }
    }

    slot = cl_env_lookup(c->env, sym);
    if (slot >= 0) {
        cl_emit(c, OP_LOAD);
        cl_emit(c, (uint8_t)slot);
        if (c->env->boxed[slot])
            cl_emit(c, OP_CELL_REF);
        return;
    }

    if (c->env) {
        int uv_idx = cl_env_resolve_upvalue(c->env, sym);
        if (uv_idx >= 0) {
            cl_emit(c, OP_UPVAL);
            cl_emit(c, (uint8_t)uv_idx);
            if (c->env->upvalues[uv_idx].is_boxed)
                cl_emit(c, OP_CELL_REF);
            return;
        }
    }

    {
        int idx = cl_add_constant(c, sym);
        cl_emit(c, OP_GLOAD);
        cl_emit_u16(c, (uint16_t)idx);
    }
}

/* --- Main dispatcher --- */

void compile_expr(CL_Compiler *c, CL_Obj expr)
{
    if (CL_NULL_P(expr))    { cl_emit(c, OP_NIL); return; }
    if (CL_FIXNUM_P(expr))  { cl_emit_const(c, expr); return; }
    if (CL_CHAR_P(expr))    { cl_emit_const(c, expr); return; }
    if (CL_STRING_P(expr))  { cl_emit_const(c, expr); return; }
    if (CL_BIGNUM_P(expr))  { cl_emit_const(c, expr); return; }
    if (CL_RATIO_P(expr))   { cl_emit_const(c, expr); return; }
    if (CL_FLOATP(expr))    { cl_emit_const(c, expr); return; }
    if (CL_VECTOR_P(expr))  { cl_emit_const(c, expr); return; }
    if (CL_BIT_VECTOR_P(expr)) { cl_emit_const(c, expr); return; }
    if (CL_PATHNAME_P(expr))   { cl_emit_const(c, expr); return; }

    if (CL_SYMBOL_P(expr)) {
        compile_symbol(c, expr);
        return;
    }

    if (CL_CONS_P(expr)) {
        CL_Obj head = cl_car(expr);

        /* Record source location for this expression */
        {
            int line = cl_srcloc_lookup(expr);
            if (line > 0 && c->line_entry_count < CL_MAX_LINE_ENTRIES) {
                int last = c->line_entry_count - 1;
                /* Only add if different from last entry */
                if (last < 0 || c->line_entries[last].line != (uint16_t)line
                             || c->line_entries[last].pc != (uint16_t)c->code_pos) {
                    c->line_entries[c->line_entry_count].pc = (uint16_t)c->code_pos;
                    c->line_entries[c->line_entry_count].line = (uint16_t)line;
                    c->line_entry_count++;
                }
                c->current_line = line;
            }
        }

        /* Check local macros (macrolet) before global macros */
        if (CL_SYMBOL_P(head)) {
            CL_Obj local_expander = cl_env_lookup_local_macro(c->env, head);
            if (!CL_NULL_P(local_expander)) {
                /* Expand using local macro expander */
                CL_Obj expanded;
                CL_Obj arg_array[255];
                int nargs = 0;
                CL_Obj args_list = cl_cdr(expr);
                while (!CL_NULL_P(args_list) && nargs < 255) {
                    arg_array[nargs++] = cl_car(args_list);
                    args_list = cl_cdr(args_list);
                }
                CL_GC_PROTECT(expr);
                CL_GC_PROTECT(local_expander);
                expanded = cl_vm_apply(local_expander, arg_array, nargs);
                CL_GC_UNPROTECT(2);
                compile_expr(c, expanded);
                return;
            }
        }

        if (CL_SYMBOL_P(head) && cl_macro_p(head)) {
            CL_Obj expanded = cl_macroexpand_1(expr);
            compile_expr(c, expanded);
            return;
        }

        if (head == SYM_QUOTE)       { compile_quote(c, expr); return; }
        if (head == SYM_IF)          { compile_if(c, expr); return; }
        if (head == SYM_PROGN)       { compile_progn(c, cl_cdr(expr)); return; }
        if (head == SYM_LAMBDA)      { compile_lambda(c, expr); return; }
        if (head == SYM_LET)         { compile_let(c, expr, 0); return; }
        if (head == SYM_LETSTAR)     { compile_let(c, expr, 1); return; }
        if (head == SYM_SETQ)        { compile_setq(c, expr); return; }
        if (head == SYM_SETF)        { compile_setf(c, expr); return; }
        if (head == SYM_FUNCTION)    { compile_function(c, expr); return; }
        /* compiler_extra.c */
        if (head == SYM_DEFUN)       { compile_defun(c, expr); return; }
        if (head == SYM_DEFVAR)      { compile_defvar(c, expr); return; }
        if (head == SYM_DEFPARAMETER) { compile_defparameter(c, expr); return; }
        if (head == SYM_DEFCONSTANT)  { compile_defconstant(c, expr); return; }
        if (head == SYM_DEFMACRO)    { compile_defmacro(c, expr); return; }
        if (head == SYM_AND)         { compile_and(c, expr); return; }
        if (head == SYM_OR)          { compile_or(c, expr); return; }
        if (head == SYM_COND)        { compile_cond(c, expr); return; }
        if (head == SYM_CASE)        { compile_case(c, expr, 0); return; }
        if (head == SYM_ECASE)       { compile_case(c, expr, 1); return; }
        if (head == SYM_TYPECASE)    { compile_typecase(c, expr, 0); return; }
        if (head == SYM_ETYPECASE)   { compile_typecase(c, expr, 1); return; }
        if (head == SYM_QUASIQUOTE)  { compile_quasiquote(c, expr); return; }
        if (head == SYM_EVAL_WHEN)   { compile_eval_when(c, expr); return; }
        if (head == SYM_DEFSETF)     { compile_defsetf(c, expr); return; }
        if (head == SYM_DEFTYPE)     { compile_deftype(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_BIND)  { compile_multiple_value_bind(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_LIST)  { compile_multiple_value_list(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_PROG1) { compile_multiple_value_prog1(c, expr); return; }
        if (head == SYM_NTH_VALUE)            { compile_nth_value(c, expr); return; }
        /* compiler_special.c */
        if (head == SYM_BLOCK)       { compile_block(c, expr); return; }
        if (head == SYM_RETURN_FROM) { compile_return_from(c, expr); return; }
        if (head == SYM_RETURN)      { compile_return(c, expr); return; }
        if (head == SYM_FLET)        { compile_flet(c, expr); return; }
        if (head == SYM_LABELS)      { compile_labels(c, expr); return; }
        if (head == SYM_DO)          { compile_do(c, expr); return; }
        if (head == SYM_DOLIST)      { compile_dolist(c, expr); return; }
        if (head == SYM_DOTIMES)     { compile_dotimes(c, expr); return; }
        if (head == SYM_TAGBODY)     { compile_tagbody(c, expr); return; }
        if (head == SYM_GO)          { compile_go(c, expr); return; }
        if (head == SYM_CATCH)       { compile_catch(c, expr); return; }
        if (head == SYM_UNWIND_PROTECT) { compile_unwind_protect(c, expr); return; }
        if (head == SYM_DESTRUCTURING_BIND) { compile_destructuring_bind(c, expr); return; }
        if (head == SYM_HANDLER_BIND) { compile_handler_bind(c, expr); return; }
        if (head == SYM_RESTART_CASE) { compile_restart_case(c, expr); return; }
        if (head == SYM_DECLARE) {
            cl_error(CL_ERR_GENERAL, "DECLARE is not allowed here -- it must appear at the start of a body form");
            return;
        }
        if (head == SYM_DECLAIM)     { compile_declaim(c, expr); return; }
        if (head == SYM_LOCALLY)     { compile_locally(c, expr); return; }
        if (head == SYM_TRACE)       { compile_trace(c, expr); return; }
        if (head == SYM_UNTRACE)     { compile_untrace(c, expr); return; }
        if (head == SYM_TIME)        { compile_time(c, expr); return; }
        if (head == SYM_IN_PACKAGE)  { compile_in_package(c, expr); return; }
        if (head == SYM_MACROLET)        { compile_macrolet(c, expr); return; }
        if (head == SYM_SYMBOL_MACROLET) { compile_symbol_macrolet(c, expr); return; }
        if (head == SYM_THE)             { compile_the(c, expr); return; }

        compile_call(c, expr);
        return;
    }

    cl_error(CL_ERR_GENERAL, "Cannot compile: unexpected %s in expression position",
             cl_type_name(expr));
}

/* --- Macro expansion (runtime, via VM) --- */

CL_Obj cl_macroexpand_1(CL_Obj form)
{
    CL_Obj head = cl_car(form);
    CL_Obj expander = cl_get_macro(head);
    CL_Obj expanded;
    CL_Obj arg_array[255];
    int nargs = 0;
    CL_Obj args_list;

    if (CL_NULL_P(expander)) return form;

    args_list = cl_cdr(form);
    while (!CL_NULL_P(args_list) && nargs < 255) {
        arg_array[nargs++] = cl_car(args_list);
        args_list = cl_cdr(args_list);
    }

    CL_GC_PROTECT(form);
    CL_GC_PROTECT(expander);
    expanded = cl_vm_apply(expander, arg_array, nargs);
    CL_GC_UNPROTECT(2);

    return expanded;
}


/* --- Macro table --- */

void cl_register_macro(CL_Obj name, CL_Obj expander)
{
    CL_Obj pair;
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(expander);
    pair = cl_cons(name, expander);
    macro_table = cl_cons(pair, macro_table);
    CL_GC_UNPROTECT(2);
}

int cl_macro_p(CL_Obj name)
{
    CL_Obj list = macro_table;
    while (!CL_NULL_P(list)) {
        if (cl_car(cl_car(list)) == name) return 1;
        list = cl_cdr(list);
    }
    return 0;
}

CL_Obj cl_get_macro(CL_Obj name)
{
    CL_Obj list = macro_table;
    while (!CL_NULL_P(list)) {
        CL_Obj pair = cl_car(list);
        if (cl_car(pair) == name) return cl_cdr(pair);
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* --- Type table (for deftype) --- */

void cl_register_type(CL_Obj name, CL_Obj expander)
{
    CL_Obj pair;
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(expander);
    pair = cl_cons(name, expander);
    type_table = cl_cons(pair, type_table);
    CL_GC_UNPROTECT(2);
}

CL_Obj cl_get_type_expander(CL_Obj name)
{
    CL_Obj list = type_table;
    while (!CL_NULL_P(list)) {
        CL_Obj pair = cl_car(list);
        if (cl_car(pair) == name) return cl_cdr(pair);
        list = cl_cdr(list);
    }
    return CL_NIL;
}

/* --- Setf function table --- */

void cl_register_setf_function(CL_Obj accessor, CL_Obj setf_fn_sym)
{
    CL_Obj pair;
    CL_GC_PROTECT(accessor);
    CL_GC_PROTECT(setf_fn_sym);
    pair = cl_cons(accessor, setf_fn_sym);
    setf_fn_table = cl_cons(pair, setf_fn_table);
    CL_GC_UNPROTECT(2);
}

/* --- Public API --- */

CL_Obj cl_compile(CL_Obj expr)
{
    CL_Compiler *comp;
    CL_CompEnv *env;
    CL_Bytecode *bc;

    /* Heap-allocate compiler state (~45KB — too large for AmigaOS stack) */
    comp = (CL_Compiler *)platform_alloc(sizeof(CL_Compiler));
    if (!comp) return CL_NIL;
    memset(comp, 0, sizeof(*comp));
    env = cl_env_create(NULL);
    comp->env = env;
    comp->in_tail = 0;

    compile_expr(comp, expr);
    cl_emit(comp, OP_HALT);

    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) { cl_env_destroy(env); platform_free(comp); return CL_NIL; }

    bc->code = (uint8_t *)platform_alloc(comp->code_pos);
    if (bc->code) memcpy(bc->code, comp->code, comp->code_pos);
    bc->code_len = comp->code_pos;

    if (comp->const_count > 0) {
        int i;
        bc->constants = (CL_Obj *)platform_alloc(
            comp->const_count * sizeof(CL_Obj));
        if (bc->constants) {
            for (i = 0; i < comp->const_count; i++)
                bc->constants[i] = comp->constants[i];
        }
        bc->n_constants = comp->const_count;
    } else {
        bc->constants = NULL;
        bc->n_constants = 0;
    }

    bc->arity = 0;
    bc->n_locals = env->max_locals;
    bc->n_upvalues = 0;
    bc->name = CL_NIL;
    bc->n_optional = 0;
    bc->flags = 0;
    bc->n_keys = 0;
    bc->key_syms = NULL;
    bc->key_slots = NULL;
    bc->key_suppliedp_slots = NULL;

    /* Transfer source line map */
    if (comp->line_entry_count > 0) {
        int i;
        bc->line_map = (CL_LineEntry *)platform_alloc(
            comp->line_entry_count * sizeof(CL_LineEntry));
        if (bc->line_map) {
            for (i = 0; i < comp->line_entry_count; i++)
                bc->line_map[i] = comp->line_entries[i];
        }
        bc->line_map_count = (uint16_t)comp->line_entry_count;
    } else {
        bc->line_map = NULL;
        bc->line_map_count = 0;
    }
    bc->source_line = (uint16_t)comp->current_line;
    bc->source_file = cl_current_source_file;

    cl_env_destroy(env);
    platform_free(comp);
    return CL_PTR_TO_OBJ(bc);
}

CL_Obj cl_compile_defun(CL_Obj name, CL_Obj lambda_list, CL_Obj body)
{
    CL_Obj form = cl_cons(SYM_DEFUN,
                          cl_cons(name,
                                  cl_cons(lambda_list, body)));
    return cl_compile(form);
}

void cl_compiler_init(void)
{
    macro_table = CL_NIL;
    setf_table = CL_NIL;
    type_table = CL_NIL;

    SETF_SYM_CAR             = cl_intern_in("CAR", 3, cl_package_cl);
    SETF_SYM_CDR             = cl_intern_in("CDR", 3, cl_package_cl);
    SETF_SYM_FIRST           = cl_intern_in("FIRST", 5, cl_package_cl);
    SETF_SYM_REST            = cl_intern_in("REST", 4, cl_package_cl);
    SETF_SYM_NTH             = cl_intern_in("NTH", 3, cl_package_cl);
    SETF_SYM_AREF            = cl_intern_in("AREF", 4, cl_package_cl);
    SETF_SYM_SVREF           = cl_intern_in("SVREF", 5, cl_package_cl);
    SETF_SYM_CHAR            = cl_intern_in("CHAR", 4, cl_package_cl);
    SETF_SYM_SCHAR           = cl_intern_in("SCHAR", 5, cl_package_cl);
    SETF_SYM_SYMBOL_VALUE    = cl_intern_in("SYMBOL-VALUE", 12, cl_package_cl);
    SETF_SYM_SYMBOL_FUNCTION = cl_intern_in("SYMBOL-FUNCTION", 15, cl_package_cl);
    SETF_SYM_FDEFINITION     = cl_intern_in("FDEFINITION", 11, cl_package_cl);
    SETF_HELPER_NTH          = cl_intern_in("%SETF-NTH", 9, cl_package_cl);
    SETF_HELPER_SV           = cl_intern_in("%SET-SYMBOL-VALUE", 17, cl_package_cl);
    SETF_HELPER_SF           = cl_intern_in("%SET-SYMBOL-FUNCTION", 20, cl_package_cl);
    SETF_SYM_GETHASH         = cl_intern_in("GETHASH", 7, cl_package_cl);
    SETF_HELPER_GETHASH      = cl_intern_in("%SETF-GETHASH", 13, cl_package_cl);
    SETF_HELPER_AREF         = cl_intern_in("%SETF-AREF", 10, cl_package_cl);
    SETF_SYM_ROW_MAJOR_AREF = cl_intern_in("ROW-MAJOR-AREF", 14, cl_package_cl);
    SETF_HELPER_ROW_MAJOR_AREF = cl_intern_in("%SETF-ROW-MAJOR-AREF", 20, cl_package_cl);
    SETF_SYM_FILL_POINTER    = cl_intern_in("FILL-POINTER", 12, cl_package_cl);
    SETF_HELPER_FILL_POINTER = cl_intern_in("%SETF-FILL-POINTER", 18, cl_package_cl);
    SETF_SYM_BIT             = cl_intern_in("BIT", 3, cl_package_cl);
    SETF_HELPER_BIT          = cl_intern_in("%SETF-BIT", 9, cl_package_cl);
    SETF_SYM_SBIT            = cl_intern_in("SBIT", 4, cl_package_cl);
    SETF_HELPER_SBIT         = cl_intern_in("%SETF-SBIT", 10, cl_package_cl);
    SETF_SYM_GET             = cl_intern_in("GET", 3, cl_package_cl);
    SETF_HELPER_GET          = cl_intern_in("%SETF-GET", 9, cl_package_cl);
    SETF_SYM_GETF            = cl_intern_in("GETF", 4, cl_package_cl);
    SETF_HELPER_GETF         = cl_intern_in("%SETF-GETF", 10, cl_package_cl);
}
