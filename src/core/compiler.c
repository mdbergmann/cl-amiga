#include "compiler.h"
#include "env.h"
#include "opcodes.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "../platform/platform.h"
#include <string.h>

/* Block tracking for return/return-from */
#define CL_MAX_BLOCKS 16
#define CL_MAX_BLOCK_PATCHES 16

typedef struct {
    CL_Obj tag;
    int exit_patches[CL_MAX_BLOCK_PATCHES];
    int n_patches;
    int result_slot;  /* local slot where return value is stored */
} CL_BlockInfo;

/* Tagbody tracking for go */
#define CL_MAX_TAGBODY_TAGS 32

typedef struct {
    CL_Obj tag;
    int code_pos;          /* bytecode offset, -1 if forward */
    int forward_patches[CL_MAX_BLOCK_PATCHES];
    int n_forward;
} CL_TagInfo;

typedef struct {
    CL_TagInfo tags[CL_MAX_TAGBODY_TAGS];
    int n_tags;
} CL_TagbodyInfo;

/* Compiler state */
typedef struct {
    uint8_t code[CL_MAX_CODE_SIZE];
    CL_Obj constants[CL_MAX_CONSTANTS];
    int code_pos;
    int const_count;
    CL_CompEnv *env;
    int in_tail;      /* Are we in tail position? */
    CL_BlockInfo blocks[CL_MAX_BLOCKS];
    int block_count;
    CL_TagbodyInfo tagbodies[CL_MAX_BLOCKS];
    int tagbody_count;
} CL_Compiler;

/* Macro table (simple association list: ((name . expander) ...)) */
static CL_Obj macro_table = CL_NIL;

/* Forward declarations */
static void compile_expr(CL_Compiler *c, CL_Obj expr);
static void compile_body(CL_Compiler *c, CL_Obj forms);

/* --- Code emission --- */

static void emit(CL_Compiler *c, uint8_t byte)
{
    if (c->code_pos < CL_MAX_CODE_SIZE) {
        c->code[c->code_pos++] = byte;
    } else {
        cl_error(CL_ERR_OVERFLOW, "Bytecode too large");
    }
}

static void emit_u16(CL_Compiler *c, uint16_t val)
{
    emit(c, (uint8_t)(val >> 8));
    emit(c, (uint8_t)(val & 0xFF));
}

static void emit_i16(CL_Compiler *c, int16_t val)
{
    emit_u16(c, (uint16_t)val);
}

static int add_constant(CL_Compiler *c, CL_Obj obj)
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

static void emit_const(CL_Compiler *c, CL_Obj obj)
{
    int idx = add_constant(c, obj);
    emit(c, OP_CONST);
    emit_u16(c, (uint16_t)idx);
}

/* Returns the position of the jump offset (for patching) */
static int emit_jump(CL_Compiler *c, uint8_t op)
{
    int pos;
    emit(c, op);
    pos = c->code_pos;
    emit_i16(c, 0); /* placeholder */
    return pos;
}

static void patch_jump(CL_Compiler *c, int patch_pos)
{
    int offset = c->code_pos - (patch_pos + 2);
    c->code[patch_pos]     = (uint8_t)(offset >> 8);
    c->code[patch_pos + 1] = (uint8_t)(offset & 0xFF);
}

/* Emit a backward jump (loop) to a known target position */
static void emit_loop_jump(CL_Compiler *c, uint8_t op, int target)
{
    emit(c, op);
    emit_i16(c, (int16_t)(target - (c->code_pos + 2)));
}

/* --- Special form compilation --- */

static void compile_quote(CL_Compiler *c, CL_Obj form)
{
    CL_Obj val = cl_car(cl_cdr(form));
    if (CL_NULL_P(val))
        emit(c, OP_NIL);
    else if (val == SYM_T)
        emit(c, OP_T);
    else
        emit_const(c, val);
}

static void compile_if(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);
    CL_Obj test = cl_car(rest);
    CL_Obj then_form = cl_car(cl_cdr(rest));
    CL_Obj else_form = cl_car(cl_cdr(cl_cdr(rest)));
    int saved_tail = c->in_tail;
    int jnil_pos, jmp_pos;

    /* Compile test (not in tail position) */
    c->in_tail = 0;
    compile_expr(c, test);

    /* Jump to else if nil */
    jnil_pos = emit_jump(c, OP_JNIL);

    /* Compile then branch */
    c->in_tail = saved_tail;
    compile_expr(c, then_form);

    /* Jump over else */
    jmp_pos = emit_jump(c, OP_JMP);
    patch_jump(c, jnil_pos);

    /* Compile else branch */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(rest)))) {
        compile_expr(c, else_form);
    } else {
        emit(c, OP_NIL);
    }

    patch_jump(c, jmp_pos);
}

static void compile_progn(CL_Compiler *c, CL_Obj forms)
{
    if (CL_NULL_P(forms)) {
        emit(c, OP_NIL);
        return;
    }
    while (!CL_NULL_P(forms)) {
        int is_last = CL_NULL_P(cl_cdr(forms));
        if (!is_last) {
            int saved_tail = c->in_tail;
            c->in_tail = 0;
            compile_expr(c, cl_car(forms));
            c->in_tail = saved_tail;
            emit(c, OP_POP);
        } else {
            compile_expr(c, cl_car(forms));
        }
        forms = cl_cdr(forms);
    }
}

/* Parsed lambda list structure */
typedef struct {
    CL_Obj required[CL_MAX_LOCALS];
    int n_required;
    CL_Obj opt_names[CL_MAX_LOCALS];
    CL_Obj opt_defaults[CL_MAX_LOCALS]; /* CL_NIL if no default */
    int n_optional;
    CL_Obj rest_name;       /* CL_NIL if no &rest */
    int has_rest;
    CL_Obj key_names[CL_MAX_LOCALS];
    CL_Obj key_keywords[CL_MAX_LOCALS]; /* keyword symbols */
    CL_Obj key_defaults[CL_MAX_LOCALS];
    int n_keys;
    int allow_other_keys;
} CL_ParsedLambdaList;

static void parse_lambda_list(CL_Obj params, CL_ParsedLambdaList *ll)
{
    CL_Obj p = params;
    int state = 0; /* 0=required, 1=optional, 2=rest, 3=key */

    memset(ll, 0, sizeof(*ll));
    ll->rest_name = CL_NIL;

    while (!CL_NULL_P(p)) {
        CL_Obj item = cl_car(p);
        p = cl_cdr(p);

        if (item == SYM_AMP_OPTIONAL) {
            state = 1;
            continue;
        }
        if (item == SYM_AMP_REST || item == SYM_AMP_BODY) {
            state = 2;
            continue;
        }
        if (item == SYM_AMP_KEY) {
            state = 3;
            continue;
        }
        if (item == SYM_AMP_ALLOW_OTHER_KEYS) {
            ll->allow_other_keys = 1;
            continue;
        }

        switch (state) {
        case 0: /* required */
            ll->required[ll->n_required++] = item;
            break;
        case 1: /* optional */
            if (CL_CONS_P(item)) {
                ll->opt_names[ll->n_optional] = cl_car(item);
                ll->opt_defaults[ll->n_optional] = cl_car(cl_cdr(item));
            } else {
                ll->opt_names[ll->n_optional] = item;
                ll->opt_defaults[ll->n_optional] = CL_NIL;
            }
            ll->n_optional++;
            break;
        case 2: /* rest */
            ll->rest_name = item;
            ll->has_rest = 1;
            state = 3; /* after rest, only &key allowed */
            break;
        case 3: /* key */
            if (CL_CONS_P(item)) {
                CL_Obj kname = cl_car(item);
                ll->key_names[ll->n_keys] = kname;
                ll->key_defaults[ll->n_keys] = cl_car(cl_cdr(item));
            } else {
                ll->key_names[ll->n_keys] = item;
                ll->key_defaults[ll->n_keys] = CL_NIL;
            }
            /* Intern the keyword symbol (e.g., X -> :X) */
            {
                const char *name_str = cl_symbol_name(ll->key_names[ll->n_keys]);
                ll->key_keywords[ll->n_keys] = cl_intern_keyword(
                    name_str, (uint32_t)strlen(name_str));
            }
            ll->n_keys++;
            break;
        }
    }
}

static void compile_lambda(CL_Compiler *c, CL_Obj form)
{
    /* (lambda (params...) body...) */
    CL_Obj params = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_Compiler inner;
    CL_CompEnv *env;
    CL_ParsedLambdaList ll;
    CL_Bytecode *bc;
    int const_idx;
    int i;
    int key_slot_indices[CL_MAX_LOCALS];

    /* Parse lambda list */
    parse_lambda_list(params, &ll);

    /* Set up inner compiler */
    memset(&inner, 0, sizeof(inner));
    env = cl_env_create(c->env);
    inner.env = env;
    inner.in_tail = 1;

    /* Add required params as locals */
    for (i = 0; i < ll.n_required; i++)
        cl_env_add_local(env, ll.required[i]);

    /* Add optional params as locals */
    for (i = 0; i < ll.n_optional; i++)
        cl_env_add_local(env, ll.opt_names[i]);

    /* Add rest param */
    if (ll.has_rest)
        cl_env_add_local(env, ll.rest_name);

    /* Add key params as locals, record their slot indices */
    for (i = 0; i < ll.n_keys; i++) {
        key_slot_indices[i] = cl_env_add_local(env, ll.key_names[i]);
    }

    /* Emit prologue for optional defaults */
    for (i = 0; i < ll.n_optional; i++) {
        if (!CL_NULL_P(ll.opt_defaults[i])) {
            int skip_pos;
            /* if nargs >= required + i + 1, skip default */
            emit(&inner, OP_ARGC);
            emit_const(&inner, CL_MAKE_FIXNUM(ll.n_required + i + 1));
            emit(&inner, OP_GE);
            skip_pos = emit_jump(&inner, OP_JTRUE);
            /* Apply default */
            {
                int saved = inner.in_tail;
                inner.in_tail = 0;
                compile_expr(&inner, ll.opt_defaults[i]);
                inner.in_tail = saved;
            }
            emit(&inner, OP_STORE);
            emit(&inner, (uint8_t)(ll.n_required + i));
            emit(&inner, OP_POP);
            patch_jump(&inner, skip_pos);
        }
    }

    /* Emit prologue for key defaults */
    for (i = 0; i < ll.n_keys; i++) {
        if (!CL_NULL_P(ll.key_defaults[i])) {
            int skip_pos;
            /* If slot is non-NIL, key was provided — skip default */
            emit(&inner, OP_LOAD);
            emit(&inner, (uint8_t)key_slot_indices[i]);
            skip_pos = emit_jump(&inner, OP_JTRUE);
            /* Apply default */
            {
                int saved = inner.in_tail;
                inner.in_tail = 0;
                compile_expr(&inner, ll.key_defaults[i]);
                inner.in_tail = saved;
            }
            emit(&inner, OP_STORE);
            emit(&inner, (uint8_t)key_slot_indices[i]);
            emit(&inner, OP_POP);
            patch_jump(&inner, skip_pos);
        }
    }

    /* Compile body */
    compile_body(&inner, body);
    emit(&inner, OP_RET);

    /* Build bytecode object */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) { cl_env_destroy(env); return; }

    bc->code = (uint8_t *)platform_alloc(inner.code_pos);
    if (bc->code) memcpy(bc->code, inner.code, inner.code_pos);
    bc->code_len = inner.code_pos;

    if (inner.const_count > 0) {
        bc->constants = (CL_Obj *)platform_alloc(
            inner.const_count * sizeof(CL_Obj));
        if (bc->constants) {
            for (i = 0; i < inner.const_count; i++)
                bc->constants[i] = inner.constants[i];
        }
        bc->n_constants = inner.const_count;
    } else {
        bc->constants = NULL;
        bc->n_constants = 0;
    }

    bc->arity = ll.has_rest ? (ll.n_required | 0x8000) : ll.n_required;
    bc->n_locals = env->max_locals;
    bc->n_upvalues = env->upvalue_count;
    bc->name = CL_NIL;
    bc->n_optional = (uint8_t)ll.n_optional;
    bc->flags = (ll.n_keys > 0 ? 1 : 0) | (ll.allow_other_keys ? 2 : 0);
    bc->n_keys = (uint8_t)ll.n_keys;

    /* Allocate and fill key_syms and key_slots */
    if (ll.n_keys > 0) {
        bc->key_syms = (CL_Obj *)platform_alloc(ll.n_keys * sizeof(CL_Obj));
        bc->key_slots = (uint8_t *)platform_alloc(ll.n_keys);
        for (i = 0; i < ll.n_keys; i++) {
            bc->key_syms[i] = ll.key_keywords[i];
            bc->key_slots[i] = (uint8_t)key_slot_indices[i];
        }
    } else {
        bc->key_syms = NULL;
        bc->key_slots = NULL;
    }

    /* Emit closure instruction in outer compiler, then capture descriptors */
    const_idx = add_constant(c, CL_PTR_TO_OBJ(bc));
    emit(c, OP_CLOSURE);
    emit_u16(c, (uint16_t)const_idx);

    /* Emit capture descriptors: [is_local:u8, index:u8] per upvalue */
    {
        for (i = 0; i < env->upvalue_count; i++) {
            emit(c, (uint8_t)env->upvalues[i].is_local);
            emit(c, (uint8_t)env->upvalues[i].index);
        }
    }

    cl_env_destroy(env);
}

static void compile_let(CL_Compiler *c, CL_Obj form, int sequential)
{
    /* (let ((var val) ...) body...) or (let* ((var val) ...) body...)
     *
     * Locals are added to the current env (flat local slots) so they
     * map directly to VM pre-allocated local slots. After the body,
     * we restore local_count to "pop" them from scope.
     *
     * Special (dynamic) variables use OP_DYNBIND/OP_DYNUNBIND instead
     * of local slots, and are NOT added to the lexical env.
     */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int special_count = 0;

    if (sequential) {
        /* let* — each binding visible to the next */
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

            if (cl_symbol_specialp(var)) {
                int idx = add_constant(c, var);
                emit(c, OP_DYNBIND);
                emit_u16(c, (uint16_t)idx);
                special_count++;
            } else {
                int slot = cl_env_add_local(env, var);
                emit(c, OP_STORE);
                emit(c, (uint8_t)slot);
                emit(c, OP_POP);
            }
            bindings = cl_cdr(bindings);
        }
    } else {
        /* let — evaluate all values first, then bind */
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

        /* Evaluate all values in current scope */
        for (i = 0; i < n; i++) {
            c->in_tail = 0;
            compile_expr(c, vals[i]);
        }

        /* Bind values (stack top = last val, bind back-to-front).
         * For let (parallel), register all lexical locals first,
         * then store/dynbind back-to-front. */
        {
            /* First pass: register lexical locals (not specials) */
            int lexical_slots[CL_MAX_LOCALS];
            for (i = 0; i < n; i++) {
                if (cl_symbol_specialp(vars[i])) {
                    lexical_slots[i] = -1;
                } else {
                    lexical_slots[i] = cl_env_add_local(env, vars[i]);
                }
            }
            /* Second pass: pop values back-to-front */
            for (i = n - 1; i >= 0; i--) {
                if (lexical_slots[i] >= 0) {
                    emit(c, OP_STORE);
                    emit(c, (uint8_t)lexical_slots[i]);
                    emit(c, OP_POP);
                } else {
                    int idx = add_constant(c, vars[i]);
                    emit(c, OP_DYNBIND);
                    emit_u16(c, (uint16_t)idx);
                    special_count++;
                }
            }
        }
    }

    /* Compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

    /* Unbind dynamic variables */
    if (special_count > 0) {
        emit(c, OP_DYNUNBIND);
        emit(c, (uint8_t)special_count);
    }

    /* Restore scope (locals "go out of scope") */
    env->local_count = saved_local_count;
}

static void compile_setq(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);

    while (!CL_NULL_P(rest)) {
        CL_Obj var = cl_car(rest);
        CL_Obj val = cl_car(cl_cdr(rest));
        int slot;
        int saved_tail = c->in_tail;

        c->in_tail = 0;
        compile_expr(c, val);
        c->in_tail = saved_tail;

        /* Check for local variable */
        slot = cl_env_lookup(c->env, var);
        if (slot >= 0) {
            emit(c, OP_DUP);
            emit(c, OP_STORE);
            emit(c, (uint8_t)slot);
        } else {
            /* Global */
            int idx = add_constant(c, var);
            emit(c, OP_DUP);
            emit(c, OP_GSTORE);
            emit_u16(c, (uint16_t)idx);
        }

        rest = cl_cdr(cl_cdr(rest));
        /* If there are more pairs, pop the intermediate value */
        if (!CL_NULL_P(rest)) {
            emit(c, OP_POP);
        }
    }
}

static void compile_defvar(CL_Compiler *c, CL_Obj form)
{
    /* (defvar name) or (defvar name init-form) */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);

    sym->flags |= CL_SYM_SPECIAL;

    if (!CL_NULL_P(rest) && sym->value == CL_UNBOUND) {
        int idx;
        compile_expr(c, cl_car(rest));
        idx = add_constant(c, name);
        emit(c, OP_GSTORE);
        emit_u16(c, (uint16_t)idx);
        emit(c, OP_POP);
    }
    emit_const(c, name);
}

static void compile_defparameter(CL_Compiler *c, CL_Obj form)
{
    /* (defparameter name init-form) — always sets value */
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj rest = cl_cdr(cl_cdr(form));
    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(name);
    int idx;

    sym->flags |= CL_SYM_SPECIAL;

    if (!CL_NULL_P(rest)) {
        compile_expr(c, cl_car(rest));
        idx = add_constant(c, name);
        emit(c, OP_GSTORE);
        emit_u16(c, (uint16_t)idx);
        emit(c, OP_POP);
    }
    emit_const(c, name);
}

static void compile_defun(CL_Compiler *c, CL_Obj form)
{
    CL_Obj name = cl_car(cl_cdr(form));
    CL_Obj lambda_list = cl_car(cl_cdr(cl_cdr(form)));
    CL_Obj body = cl_cdr(cl_cdr(cl_cdr(form)));

    /* Build (lambda (params) body...) and compile it */
    CL_Obj lambda_form = cl_cons(SYM_LAMBDA,
                                  cl_cons(lambda_list, body));
    CL_GC_PROTECT(lambda_form);

    compile_expr(c, lambda_form);

    CL_GC_UNPROTECT(1);

    /* Store as function binding of name */
    {
        int idx = add_constant(c, name);
        /* The closure is on the stack; store as function binding */
        emit(c, OP_DUP);
        emit(c, OP_GSTORE);
        emit_u16(c, (uint16_t)idx);
    }

    /* Set the bytecode name for display purposes */
    /* Return the symbol name */
    emit(c, OP_POP);
    emit_const(c, name);
}

static void compile_defmacro(CL_Compiler *c, CL_Obj form)
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
    idx = add_constant(c, name);
    emit(c, OP_DEFMACRO);
    emit_u16(c, (uint16_t)idx);
}

static void compile_function(CL_Compiler *c, CL_Obj form)
{
    /* (function name) => look up function binding */
    CL_Obj name = cl_car(cl_cdr(form));

    if (CL_CONS_P(name) && cl_car(name) == SYM_LAMBDA) {
        /* (function (lambda ...)) */
        compile_lambda(c, name);
    } else if (CL_SYMBOL_P(name)) {
        /* Check local function bindings (flet/labels) first */
        int fun_slot = cl_env_lookup_local_fun(c->env, name);
        if (fun_slot >= 0) {
            emit(c, OP_LOAD);
            emit(c, (uint8_t)fun_slot);
        } else if (c->env) {
            int uv_idx = cl_env_resolve_fun_upvalue(c->env, name);
            if (uv_idx >= 0) {
                emit(c, OP_UPVAL);
                emit(c, (uint8_t)uv_idx);
            } else {
                int idx = add_constant(c, name);
                emit(c, OP_FLOAD);
                emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = add_constant(c, name);
            emit(c, OP_FLOAD);
            emit_u16(c, (uint16_t)idx);
        }
    } else {
        int idx = add_constant(c, name);
        emit(c, OP_FLOAD);
        emit_u16(c, (uint16_t)idx);
    }
}

static void compile_block(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_block_count = c->block_count;
    int result_slot;
    CL_BlockInfo *bi;
    int i;

    /* Allocate anonymous result slot */
    result_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Push block info */
    bi = &c->blocks[c->block_count++];
    bi->tag = tag;
    bi->n_patches = 0;
    bi->result_slot = result_slot;

    /* Compile body */
    compile_body(c, body);

    /* Normal exit: store result in slot */
    emit(c, OP_STORE);
    emit(c, (uint8_t)result_slot);
    emit(c, OP_POP);

    /* Patch all return-from jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        patch_jump(c, bi->exit_patches[i]);

    /* Load result from slot */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)result_slot);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

static void compile_return_from(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tag = cl_car(cl_cdr(form));
    CL_Obj val_form = cl_car(cl_cdr(cl_cdr(form)));
    int saved_tail = c->in_tail;
    int i;

    /* Find matching block (innermost first) */
    for (i = c->block_count - 1; i >= 0; i--) {
        if (c->blocks[i].tag == tag) {
            CL_BlockInfo *bi = &c->blocks[i];

            /* Compile value */
            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(cl_cdr(form))))
                compile_expr(c, val_form);
            else
                emit(c, OP_NIL);
            c->in_tail = saved_tail;

            /* Store in result slot and jump to exit */
            emit(c, OP_STORE);
            emit(c, (uint8_t)bi->result_slot);
            emit(c, OP_POP);

            if (bi->n_patches < CL_MAX_BLOCK_PATCHES)
                bi->exit_patches[bi->n_patches++] = emit_jump(c, OP_JMP);
            return;
        }
    }

    cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
             CL_NULL_P(tag) ? "NIL" : cl_symbol_name(tag));
}

static void compile_return(CL_Compiler *c, CL_Obj form)
{
    /* (return [value]) => return-from NIL */
    CL_Obj val_form = cl_car(cl_cdr(form));
    int saved_tail = c->in_tail;
    int i;

    for (i = c->block_count - 1; i >= 0; i--) {
        if (CL_NULL_P(c->blocks[i].tag)) {
            CL_BlockInfo *bi = &c->blocks[i];

            c->in_tail = 0;
            if (!CL_NULL_P(cl_cdr(form)))
                compile_expr(c, val_form);
            else
                emit(c, OP_NIL);
            c->in_tail = saved_tail;

            emit(c, OP_STORE);
            emit(c, (uint8_t)bi->result_slot);
            emit(c, OP_POP);

            if (bi->n_patches < CL_MAX_BLOCK_PATCHES)
                bi->exit_patches[bi->n_patches++] = emit_jump(c, OP_JMP);
            return;
        }
    }

    cl_error(CL_ERR_GENERAL, "RETURN: no block named NIL");
}

/* --- tagbody / go --- */

static int is_tagbody_tag(CL_Obj form)
{
    /* Tags are symbols or integers (CL spec) */
    return CL_SYMBOL_P(form) || CL_FIXNUM_P(form);
}

static void compile_tagbody(CL_Compiler *c, CL_Obj form)
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
                        patch_jump(c, tb->tags[i].forward_patches[j]);
                    }
                    tb->tags[i].n_forward = 0;
                    break;
                }
            }
        } else {
            /* Compile statement, discard result */
            compile_expr(c, item);
            emit(c, OP_POP);
        }
        cursor = cl_cdr(cursor);
    }

    /* tagbody returns NIL */
    emit(c, OP_NIL);

    /* Restore */
    c->tagbody_count = saved_tagbody_count;
    c->in_tail = saved_tail;
}

static void compile_go(CL_Compiler *c, CL_Obj form)
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
                    emit_loop_jump(c, OP_JMP, ti->code_pos);
                } else {
                    /* Forward jump — patch later */
                    if (ti->n_forward < CL_MAX_BLOCK_PATCHES)
                        ti->forward_patches[ti->n_forward++] = emit_jump(c, OP_JMP);
                }
                return;
            }
        }
    }

    cl_error(CL_ERR_GENERAL, "GO: no tag named %s",
             CL_SYMBOL_P(tag) ? cl_symbol_name(tag) : "?");
}

/* --- catch --- */

static void compile_catch(CL_Compiler *c, CL_Obj form)
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
    emit(c, OP_CATCH);
    catch_pos = c->code_pos;
    emit_i16(c, 0); /* placeholder */

    /* Compile body (progn) */
    c->in_tail = saved_tail;
    if (CL_NULL_P(body))
        emit(c, OP_NIL);
    else
        compile_progn(c, body);

    /* OP_UNCATCH: pop catch frame (normal exit) */
    emit(c, OP_UNCATCH);

    /* JMP past the throw landing */
    jmp_pos = emit_jump(c, OP_JMP);

    /* [landing]: throw arrives here with result on stack */
    patch_jump(c, catch_pos);

    /* [past_landing]: both paths converge, result is on stack */
    patch_jump(c, jmp_pos);
}

/* --- unwind-protect --- */

static void compile_unwind_protect(CL_Compiler *c, CL_Obj form)
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
    emit(c, OP_UWPROT);
    uwprot_pos = c->code_pos;
    emit_i16(c, 0); /* placeholder */

    /* Compile protected form */
    compile_expr(c, protected_form);

    /* Save result in slot */
    emit(c, OP_STORE);
    emit(c, (uint8_t)result_slot);
    emit(c, OP_POP);

    /* OP_UWPOP: normal exit, pop frame, clear pending */
    emit(c, OP_UWPOP);

    /* JMP to cleanup_start (skip the landing) */
    jmp_pos = emit_jump(c, OP_JMP);

    /* [cleanup_landing]: longjmp arrives here */
    patch_jump(c, uwprot_pos);

    /* [cleanup_start]: both paths merge */
    patch_jump(c, jmp_pos);

    /* Compile cleanup forms, discarding results */
    {
        CL_Obj cf = cleanup_forms;
        while (!CL_NULL_P(cf)) {
            compile_expr(c, cl_car(cf));
            emit(c, OP_POP);
            cf = cl_cdr(cf);
        }
    }

    /* OP_UWRETHROW: if pending throw, re-initiate (never returns); else nop */
    emit(c, OP_UWRETHROW);

    /* Normal path: load saved result */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)result_slot);

    /* Restore */
    c->in_tail = saved_tail;
    env->local_count = saved_local_count;
}

static void compile_and(CL_Compiler *c, CL_Obj form)
{
    CL_Obj args = cl_cdr(form);
    int saved_tail = c->in_tail;
    int nil_patches[64];
    int n_patches = 0;

    if (CL_NULL_P(args)) {
        /* (and) => T */
        emit(c, OP_T);
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
            emit(c, OP_DUP);
            nil_patches[n_patches++] = emit_jump(c, OP_JNIL);
            emit(c, OP_POP);
        }
        args = cl_cdr(args);
    }

    /* Patch all nil-jumps to here (done label) */
    {
        int i;
        for (i = 0; i < n_patches; i++)
            patch_jump(c, nil_patches[i]);
    }
}

static void compile_or(CL_Compiler *c, CL_Obj form)
{
    CL_Obj args = cl_cdr(form);
    int saved_tail = c->in_tail;
    int true_patches[64];
    int n_patches = 0;

    if (CL_NULL_P(args)) {
        /* (or) => NIL */
        emit(c, OP_NIL);
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
            emit(c, OP_DUP);
            true_patches[n_patches++] = emit_jump(c, OP_JTRUE);
            emit(c, OP_POP);
        }
        args = cl_cdr(args);
    }

    /* Patch all true-jumps to here (done label) */
    {
        int i;
        for (i = 0; i < n_patches; i++)
            patch_jump(c, true_patches[i]);
    }
}

static void compile_cond(CL_Compiler *c, CL_Obj form)
{
    CL_Obj clauses = cl_cdr(form);
    int saved_tail = c->in_tail;
    int done_patches[64];
    int n_done = 0;

    if (CL_NULL_P(clauses)) {
        emit(c, OP_NIL);
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
                emit(c, OP_T);
            else
                compile_progn(c, body);
        } else {
            int jnil_pos;
            /* Compile test */
            c->in_tail = 0;
            compile_expr(c, test);
            jnil_pos = emit_jump(c, OP_JNIL);

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body)) {
                /* (cond (test)) with no body — return test value
                 * But test was already consumed by JNIL, so we'd need DUP.
                 * CL spec says return test value. For simplicity, push T. */
                emit(c, OP_T);
            } else {
                compile_progn(c, body);
            }

            /* Always JMP past NIL fallthrough (even last non-t clause) */
            done_patches[n_done++] = emit_jump(c, OP_JMP);

            patch_jump(c, jnil_pos);
        }

        clauses = cl_cdr(clauses);
    }

    /* If the last clause wasn't a t-default, we need a NIL fallthrough */
    {
        CL_Obj last_clause = cl_car(cl_cdr(form));
        CL_Obj c2 = cl_cdr(form);
        /* Walk to last clause */
        while (!CL_NULL_P(cl_cdr(c2))) c2 = cl_cdr(c2);
        last_clause = cl_car(c2);
        if (cl_car(last_clause) != SYM_T)
            emit(c, OP_NIL);
    }

    /* Patch all done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            patch_jump(c, done_patches[i]);
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
                    emit(c, OP_CONS);
            }
            return;
        }

        compile_qq(c, elem);
        n++;
        cursor = rest;
    }

    if (CL_NULL_P(cursor)) {
        /* Proper list */
        emit(c, OP_LIST);
        emit(c, (uint8_t)n);
    } else {
        /* Dotted tail (non-unquote atom) — compile the cdr, then n CONSes */
        compile_qq(c, cursor);
        {
            int i;
            for (i = 0; i < n; i++)
                emit(c, OP_CONS);
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
    idx = add_constant(c, sym_append);
    emit(c, OP_FLOAD);
    emit_u16(c, (uint16_t)idx);

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
            emit(c, OP_LIST);
            emit(c, (uint8_t)run);
            n_segments++;
            continue;  /* Don't advance cursor again */
        }
        cursor = cl_cdr(cursor);
    }

    emit(c, OP_CALL);
    emit(c, (uint8_t)n_segments);
}

/* Recursive quasiquote template walker */
static void compile_qq(CL_Compiler *c, CL_Obj tmpl)
{
    /* Atom (non-cons, including NIL): emit as constant */
    if (!CL_CONS_P(tmpl)) {
        if (CL_NULL_P(tmpl))
            emit(c, OP_NIL);
        else
            emit_const(c, tmpl);
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

static void compile_quasiquote(CL_Compiler *c, CL_Obj form)
{
    CL_Obj tmpl = cl_car(cl_cdr(form));
    compile_qq(c, tmpl);
}

/* --- Case forms --- */

static void compile_case(CL_Compiler *c, CL_Obj form, int error_if_no_match)
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
    emit(c, OP_STORE);
    emit(c, (uint8_t)temp_slot);
    emit(c, OP_POP);

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
                emit(c, OP_NIL);
            else
                compile_progn(c, body);
            done_patches[n_done++] = emit_jump(c, OP_JMP);
        } else {
            int body_patches[64];
            int n_body = 0;
            int next_clause_pos;

            /* Emit EQ tests for each key */
            if (CL_CONS_P(keys)) {
                /* Multiple keys: ((k1 k2 k3) body...) */
                CL_Obj k = keys;
                while (!CL_NULL_P(k)) {
                    emit(c, OP_LOAD);
                    emit(c, (uint8_t)temp_slot);
                    emit_const(c, cl_car(k));
                    emit(c, OP_EQ);
                    body_patches[n_body++] = emit_jump(c, OP_JTRUE);
                    k = cl_cdr(k);
                }
            } else {
                /* Single key: (key body...) */
                emit(c, OP_LOAD);
                emit(c, (uint8_t)temp_slot);
                emit_const(c, keys);
                emit(c, OP_EQ);
                body_patches[n_body++] = emit_jump(c, OP_JTRUE);
            }

            /* No key matched — jump to next clause */
            next_clause_pos = emit_jump(c, OP_JMP);

            /* body: patch all key-match jumps here */
            {
                int j;
                for (j = 0; j < n_body; j++)
                    patch_jump(c, body_patches[j]);
            }

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                emit(c, OP_NIL);
            else
                compile_progn(c, body);
            done_patches[n_done++] = emit_jump(c, OP_JMP);

            /* next_clause: */
            patch_jump(c, next_clause_pos);
        }

        clauses = cl_cdr(clauses);
    }

    /* No default matched */
    if (!had_default) {
        if (error_if_no_match) {
            /* ecase: signal error */
            CL_Obj sym_error = cl_intern("ERROR", 5);
            CL_Obj errmsg = cl_make_string("ECASE: no matching clause", 25);
            int idx = add_constant(c, sym_error);
            emit(c, OP_FLOAD);
            emit_u16(c, (uint16_t)idx);
            emit_const(c, errmsg);
            emit(c, OP_CALL);
            emit(c, 1);
        } else {
            emit(c, OP_NIL);
        }
    }

    /* Patch all done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            patch_jump(c, done_patches[i]);
    }

    /* Restore scope */
    env->local_count = saved_local_count;
}

static void compile_typecase(CL_Compiler *c, CL_Obj form, int error_if_no_match)
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
    type_of_idx = add_constant(c, sym_type_of);
    emit(c, OP_FLOAD);
    emit_u16(c, (uint16_t)type_of_idx);
    compile_expr(c, keyform);
    emit(c, OP_CALL);
    emit(c, 1);
    emit(c, OP_STORE);
    emit(c, (uint8_t)temp_slot);
    emit(c, OP_POP);

    /* Process clauses (same structure as case but compare type symbols) */
    while (!CL_NULL_P(clauses)) {
        CL_Obj clause = cl_car(clauses);
        CL_Obj type_spec = cl_car(clause);
        CL_Obj body = cl_cdr(clause);

        if (type_spec == SYM_T || type_spec == SYM_OTHERWISE) {
            had_default = 1;
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                emit(c, OP_NIL);
            else
                compile_progn(c, body);
            done_patches[n_done++] = emit_jump(c, OP_JMP);
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

            emit(c, OP_LOAD);
            emit(c, (uint8_t)temp_slot);
            emit_const(c, type_sym);
            emit(c, OP_EQ);
            jnil_pos = emit_jump(c, OP_JNIL);

            /* Compile body */
            c->in_tail = saved_tail;
            if (CL_NULL_P(body))
                emit(c, OP_NIL);
            else
                compile_progn(c, body);
            done_patches[n_done++] = emit_jump(c, OP_JMP);

            patch_jump(c, jnil_pos);
        }

        clauses = cl_cdr(clauses);
    }

    /* No default */
    if (!had_default) {
        if (error_if_no_match) {
            CL_Obj sym_error = cl_intern("ERROR", 5);
            CL_Obj errmsg = cl_make_string("ETYPECASE: no matching clause", 29);
            int idx = add_constant(c, sym_error);
            emit(c, OP_FLOAD);
            emit_u16(c, (uint16_t)idx);
            emit_const(c, errmsg);
            emit(c, OP_CALL);
            emit(c, 1);
        } else {
            emit(c, OP_NIL);
        }
    }

    /* Patch done-jumps */
    {
        int i;
        for (i = 0; i < n_done; i++)
            patch_jump(c, done_patches[i]);
    }

    env->local_count = saved_local_count;
}

/* --- flet / labels --- */

static void compile_flet(CL_Compiler *c, CL_Obj form)
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

            /* Build (lambda (params) body...) */
            lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, fbody));
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

            emit(c, OP_STORE);
            emit(c, (uint8_t)slot);
            emit(c, OP_POP);

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

static void compile_labels(CL_Compiler *c, CL_Obj form)
{
    /* (labels ((name (params) body...) ...) body...)
     *
     * Uses temporary global bindings so recursive/mutual references
     * resolve at runtime via FLOAD. This works because FLOAD checks
     * the value binding, and the closures are stored there before any
     * function body executes.
     *
     * Limitation: labels function names are visible globally (will be
     * fixed when reference capture / heap-boxed cells are implemented).
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

            lambda_form = cl_cons(SYM_LAMBDA, cl_cons(lambda_list, fbody));
            CL_GC_PROTECT(lambda_form);

            c->in_tail = 0;
            compile_expr(c, lambda_form);
            CL_GC_UNPROTECT(1);

            /* Store as global value binding (FLOAD falls back to value) */
            idx = add_constant(c, fname);
            emit(c, OP_GSTORE);
            emit_u16(c, (uint16_t)idx);
            emit(c, OP_POP);

            b = cl_cdr(b);
        }
    }

    /* Compile body — calls use FLOAD which finds the value binding */
    c->in_tail = saved_tail;
    compile_body(c, body);
}

/* --- Loop forms --- */

static void compile_dolist(CL_Compiler *c, CL_Obj form)
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
    emit(c, OP_STORE);
    emit(c, (uint8_t)iter_slot);
    emit(c, OP_POP);

    /* loop_start: LOAD iter_slot, JNIL -> end */
    loop_start = c->code_pos;
    emit(c, OP_LOAD);
    emit(c, (uint8_t)iter_slot);
    jnil_pos = emit_jump(c, OP_JNIL);

    /* Set var = (car iter) */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)iter_slot);
    emit(c, OP_CAR);
    emit(c, OP_STORE);
    emit(c, (uint8_t)var_slot);
    emit(c, OP_POP);

    /* Advance iter = (cdr iter) */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)iter_slot);
    emit(c, OP_CDR);
    emit(c, OP_STORE);
    emit(c, (uint8_t)iter_slot);
    emit(c, OP_POP);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Backward jump to loop_start */
    emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    patch_jump(c, jnil_pos);

    /* CL spec: var is NIL during result-form evaluation */
    emit(c, OP_NIL);
    emit(c, OP_STORE);
    emit(c, (uint8_t)var_slot);
    emit(c, OP_POP);

    /* Compile result-form or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(binding)))) {
        compile_expr(c, result_form);
    } else {
        emit(c, OP_NIL);
    }
    emit(c, OP_STORE);
    emit(c, (uint8_t)result_slot);
    emit(c, OP_POP);

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)result_slot);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

static void compile_dotimes(CL_Compiler *c, CL_Obj form)
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
    emit(c, OP_STORE);
    emit(c, (uint8_t)limit_slot);
    emit(c, OP_POP);

    /* var = 0 */
    emit_const(c, CL_MAKE_FIXNUM(0));
    emit(c, OP_STORE);
    emit(c, (uint8_t)var_slot);
    emit(c, OP_POP);

    /* loop_start: LOAD var, LOAD limit, GE, JTRUE -> end */
    loop_start = c->code_pos;
    emit(c, OP_LOAD);
    emit(c, (uint8_t)var_slot);
    emit(c, OP_LOAD);
    emit(c, (uint8_t)limit_slot);
    emit(c, OP_GE);
    jtrue_pos = emit_jump(c, OP_JTRUE);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Increment: var = var + 1 */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)var_slot);
    emit_const(c, CL_MAKE_FIXNUM(1));
    emit(c, OP_ADD);
    emit(c, OP_STORE);
    emit(c, (uint8_t)var_slot);
    emit(c, OP_POP);

    /* Backward jump to loop_start */
    emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    patch_jump(c, jtrue_pos);

    /* Compile result-form or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(cl_cdr(cl_cdr(binding)))) {
        compile_expr(c, result_form);
    } else {
        emit(c, OP_NIL);
    }
    emit(c, OP_STORE);
    emit(c, (uint8_t)result_slot);
    emit(c, OP_POP);

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)result_slot);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

static void compile_do(CL_Compiler *c, CL_Obj form)
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
        emit(c, OP_STORE);
        emit(c, (uint8_t)(saved_local_count + i));
        emit(c, OP_POP);
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
    jtrue_pos = emit_jump(c, OP_JTRUE);

    /* Compile body forms, each followed by POP */
    {
        CL_Obj b = body;
        while (!CL_NULL_P(b)) {
            compile_expr(c, cl_car(b));
            emit(c, OP_POP);
            b = cl_cdr(b);
        }
    }

    /* Parallel step: evaluate all step forms (or load current value) */
    for (i = 0; i < n; i++) {
        if (has_step[i]) {
            compile_expr(c, steps[i]);
        } else {
            emit(c, OP_LOAD);
            emit(c, (uint8_t)(saved_local_count + i));
        }
    }

    /* Store all back-to-front */
    for (i = n - 1; i >= 0; i--) {
        emit(c, OP_STORE);
        emit(c, (uint8_t)(saved_local_count + i));
        emit(c, OP_POP);
    }

    /* Backward jump to loop_start */
    emit_loop_jump(c, OP_JMP, loop_start);

    /* end: */
    patch_jump(c, jtrue_pos);

    /* Compile result forms as progn, or NIL, store in result_slot */
    c->in_tail = saved_tail;
    if (!CL_NULL_P(result_forms)) {
        compile_progn(c, result_forms);
    } else {
        emit(c, OP_NIL);
    }
    emit(c, OP_STORE);
    emit(c, (uint8_t)result_slot);
    emit(c, OP_POP);

    /* Patch all return-from NIL jumps to here */
    for (i = 0; i < bi->n_patches; i++)
        patch_jump(c, bi->exit_patches[i]);

    /* Load result */
    emit(c, OP_LOAD);
    emit(c, (uint8_t)result_slot);

    /* Restore */
    c->block_count = saved_block_count;
    env->local_count = saved_local_count;
}

/* --- Main compilation dispatch --- */

static void compile_call(CL_Compiler *c, CL_Obj form)
{
    CL_Obj func = cl_car(form);
    CL_Obj args = cl_cdr(form);
    int nargs = 0;
    int saved_tail = c->in_tail;

    /* Compile function expression */
    c->in_tail = 0;

    if (CL_SYMBOL_P(func)) {
        /* Check local function bindings (flet/labels) first */
        int fun_slot = cl_env_lookup_local_fun(c->env, func);
        if (fun_slot >= 0) {
            emit(c, OP_LOAD);
            emit(c, (uint8_t)fun_slot);
        } else if (c->env) {
            int uv_idx = cl_env_resolve_fun_upvalue(c->env, func);
            if (uv_idx >= 0) {
                emit(c, OP_UPVAL);
                emit(c, (uint8_t)uv_idx);
            } else {
                int idx = add_constant(c, func);
                emit(c, OP_FLOAD);
                emit_u16(c, (uint16_t)idx);
            }
        } else {
            int idx = add_constant(c, func);
            emit(c, OP_FLOAD);
            emit_u16(c, (uint16_t)idx);
        }
    } else {
        compile_expr(c, func);
    }

    /* Compile arguments left to right */
    while (!CL_NULL_P(args)) {
        compile_expr(c, cl_car(args));
        nargs++;
        args = cl_cdr(args);
    }

    c->in_tail = saved_tail;

    /* Emit call */
    if (c->in_tail) {
        emit(c, OP_TAILCALL);
    } else {
        emit(c, OP_CALL);
    }
    emit(c, (uint8_t)nargs);
}

static void compile_body(CL_Compiler *c, CL_Obj forms)
{
    compile_progn(c, forms);
}

static void compile_symbol(CL_Compiler *c, CL_Obj sym)
{
    int slot;

    /* NIL and T are special */
    if (CL_NULL_P(sym)) { emit(c, OP_NIL); return; }
    if (sym == SYM_T)    { emit(c, OP_T);   return; }

    /* Keyword? Self-evaluating */
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
        if (s->package == cl_package_keyword) {
            emit_const(c, sym);
            return;
        }
    }

    /* Local variable? */
    slot = cl_env_lookup(c->env, sym);
    if (slot >= 0) {
        emit(c, OP_LOAD);
        emit(c, (uint8_t)slot);
        return;
    }

    /* Upvalue? (captured from enclosing scope) */
    if (c->env) {
        int uv_idx = cl_env_resolve_upvalue(c->env, sym);
        if (uv_idx >= 0) {
            emit(c, OP_UPVAL);
            emit(c, (uint8_t)uv_idx);
            return;
        }
    }

    /* Global variable */
    {
        int idx = add_constant(c, sym);
        emit(c, OP_GLOAD);
        emit_u16(c, (uint16_t)idx);
    }
}

/* --- Multiple Values --- */

static void compile_multiple_value_bind(CL_Compiler *c, CL_Obj form)
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
            emit(c, OP_STORE);
            emit(c, (uint8_t)slot);
            emit(c, OP_POP);
        } else {
            emit(c, OP_MV_LOAD);
            emit(c, (uint8_t)var_index);
            emit(c, OP_STORE);
            emit(c, (uint8_t)slot);
            emit(c, OP_POP);
        }
        var_index++;
        vl = cl_cdr(vl);
    }

    /* If no vars, still need to pop the primary */
    if (var_index == 0)
        emit(c, OP_POP);

    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;

    /* Compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

    /* Restore scope */
    env->local_count = saved_local_count;
}

static void compile_multiple_value_list(CL_Compiler *c, CL_Obj form)
{
    /* (multiple-value-list form) */
    CL_Obj values_form = cl_car(cl_cdr(form));
    int saved_tail = c->in_tail;

    c->in_tail = 0;
    compile_expr(c, values_form);
    emit(c, OP_MV_TO_LIST);   /* pops primary, builds list from MV state */

    c->in_tail = saved_tail;
}

static void compile_nth_value(CL_Compiler *c, CL_Obj form)
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
            emit(c, OP_POP);  /* discard primary */
            emit(c, OP_MV_LOAD);
            emit(c, (uint8_t)(idx < 0 ? 255 : idx > 255 ? 255 : idx));
        }
    } else {
        /* Dynamic index: stack will be [index] [primary] */
        compile_expr(c, n_form);
        compile_expr(c, values_form);
        emit(c, OP_NTH_VALUE); /* pops primary, pops index, pushes result */
    }

    c->in_tail = saved_tail;
}

static void compile_multiple_value_prog1(CL_Compiler *c, CL_Obj form)
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
    emit(c, OP_MV_TO_LIST);   /* pops primary, saves all values as a list */

    /* Allocate slot for saved list */
    list_slot = env->local_count;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;
    emit(c, OP_STORE);
    emit(c, (uint8_t)list_slot);
    emit(c, OP_POP);

    /* Evaluate remaining forms for effect */
    {
        CL_Obj rf = rest_forms;
        while (!CL_NULL_P(rf)) {
            compile_expr(c, cl_car(rf));
            emit(c, OP_POP);
            rf = cl_cdr(rf);
        }
    }

    /* Restore MV state by calling VALUES-LIST on saved list */
    {
        CL_Obj vl_sym = cl_intern_in("VALUES-LIST", 11, cl_package_cl);
        int sym_idx = add_constant(c, vl_sym);
        emit(c, OP_FLOAD);
        emit_u16(c, (uint16_t)sym_idx);
        emit(c, OP_LOAD);
        emit(c, (uint8_t)list_slot);
        emit(c, OP_CALL);
        emit(c, 1);
    }

    c->in_tail = saved_tail;
    env->local_count = saved_local_count;
}

CL_Obj cl_macroexpand_1(CL_Obj form);

static void compile_expr(CL_Compiler *c, CL_Obj expr)
{
    /* Self-evaluating atoms */
    if (CL_NULL_P(expr))    { emit(c, OP_NIL); return; }
    if (CL_FIXNUM_P(expr))  { emit_const(c, expr); return; }
    if (CL_CHAR_P(expr))    { emit_const(c, expr); return; }
    if (CL_STRING_P(expr))  { emit_const(c, expr); return; }

    /* Symbol reference */
    if (CL_SYMBOL_P(expr)) {
        compile_symbol(c, expr);
        return;
    }

    /* List (function call or special form) */
    if (CL_CONS_P(expr)) {
        CL_Obj head = cl_car(expr);

        /* Macro expansion */
        if (CL_SYMBOL_P(head) && cl_macro_p(head)) {
            CL_Obj expanded = cl_macroexpand_1(expr);
            compile_expr(c, expanded);
            return;
        }

        /* Special forms */
        if (head == SYM_QUOTE)       { compile_quote(c, expr); return; }
        if (head == SYM_IF)          { compile_if(c, expr); return; }
        if (head == SYM_PROGN)       { compile_progn(c, cl_cdr(expr)); return; }
        if (head == SYM_LAMBDA)      { compile_lambda(c, expr); return; }
        if (head == SYM_LET)         { compile_let(c, expr, 0); return; }
        if (head == SYM_LETSTAR)     { compile_let(c, expr, 1); return; }
        if (head == SYM_SETQ)        { compile_setq(c, expr); return; }
        if (head == SYM_DEFUN)       { compile_defun(c, expr); return; }
        if (head == SYM_DEFVAR)      { compile_defvar(c, expr); return; }
        if (head == SYM_DEFPARAMETER) { compile_defparameter(c, expr); return; }
        if (head == SYM_DEFMACRO)    { compile_defmacro(c, expr); return; }
        if (head == SYM_FUNCTION)    { compile_function(c, expr); return; }
        if (head == SYM_BLOCK)       { compile_block(c, expr); return; }
        if (head == SYM_RETURN_FROM) { compile_return_from(c, expr); return; }
        if (head == SYM_RETURN)      { compile_return(c, expr); return; }
        if (head == SYM_CASE)        { compile_case(c, expr, 0); return; }
        if (head == SYM_ECASE)       { compile_case(c, expr, 1); return; }
        if (head == SYM_TYPECASE)    { compile_typecase(c, expr, 0); return; }
        if (head == SYM_ETYPECASE)   { compile_typecase(c, expr, 1); return; }
        if (head == SYM_FLET)        { compile_flet(c, expr); return; }
        if (head == SYM_LABELS)      { compile_labels(c, expr); return; }
        if (head == SYM_AND)         { compile_and(c, expr); return; }
        if (head == SYM_OR)          { compile_or(c, expr); return; }
        if (head == SYM_COND)        { compile_cond(c, expr); return; }
        if (head == SYM_DO)          { compile_do(c, expr); return; }
        if (head == SYM_DOLIST)      { compile_dolist(c, expr); return; }
        if (head == SYM_DOTIMES)     { compile_dotimes(c, expr); return; }
        if (head == SYM_QUASIQUOTE)  { compile_quasiquote(c, expr); return; }
        if (head == SYM_TAGBODY)     { compile_tagbody(c, expr); return; }
        if (head == SYM_GO)          { compile_go(c, expr); return; }
        if (head == SYM_CATCH)       { compile_catch(c, expr); return; }
        if (head == SYM_UNWIND_PROTECT) { compile_unwind_protect(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_BIND)  { compile_multiple_value_bind(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_LIST)  { compile_multiple_value_list(c, expr); return; }
        if (head == SYM_MULTIPLE_VALUE_PROG1) { compile_multiple_value_prog1(c, expr); return; }
        if (head == SYM_NTH_VALUE)            { compile_nth_value(c, expr); return; }

        /* Regular function call */
        compile_call(c, expr);
        return;
    }

    cl_error(CL_ERR_GENERAL, "Cannot compile: unknown expression type");
}

/* --- Macro expansion (runtime, via VM) --- */

CL_Obj cl_macroexpand_1(CL_Obj form)
{
    CL_Obj head = cl_car(form);
    CL_Obj expander = cl_get_macro(head);
    CL_Obj args_list, expanded;
    CL_Obj arg_array[64];
    int nargs = 0;

    if (CL_NULL_P(expander)) return form;

    /* Extract arguments from the form's cdr into a C array */
    args_list = cl_cdr(form);
    while (!CL_NULL_P(args_list) && nargs < 64) {
        arg_array[nargs++] = cl_car(args_list);
        args_list = cl_cdr(args_list);
    }

    /* GC-protect the form during expansion */
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

/* --- Public API --- */

CL_Obj cl_compile(CL_Obj expr)
{
    CL_Compiler comp;
    CL_CompEnv *env;
    CL_Bytecode *bc;

    memset(&comp, 0, sizeof(comp));
    env = cl_env_create(NULL);
    comp.env = env;
    comp.in_tail = 0;

    compile_expr(&comp, expr);
    emit(&comp, OP_HALT);

    /* Build bytecode object */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) { cl_env_destroy(env); return CL_NIL; }

    bc->code = (uint8_t *)platform_alloc(comp.code_pos);
    if (bc->code) memcpy(bc->code, comp.code, comp.code_pos);
    bc->code_len = comp.code_pos;

    if (comp.const_count > 0) {
        int i;
        bc->constants = (CL_Obj *)platform_alloc(
            comp.const_count * sizeof(CL_Obj));
        if (bc->constants) {
            for (i = 0; i < comp.const_count; i++)
                bc->constants[i] = comp.constants[i];
        }
        bc->n_constants = comp.const_count;
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

    cl_env_destroy(env);
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
}
