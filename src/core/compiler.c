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

/* Compiler state */
typedef struct {
    uint8_t code[CL_MAX_CODE_SIZE];
    CL_Obj constants[CL_MAX_CONSTANTS];
    int code_pos;
    int const_count;
    CL_CompEnv *env;
    int in_tail;      /* Are we in tail position? */
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

static void compile_lambda(CL_Compiler *c, CL_Obj form)
{
    /* (lambda (params...) body...) */
    CL_Obj params = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_Compiler inner;
    CL_CompEnv *env;
    CL_Obj param;
    int arity = 0;
    int has_rest = 0;
    CL_Bytecode *bc;
    int const_idx;

    /* Set up inner compiler */
    memset(&inner, 0, sizeof(inner));
    env = cl_env_create(c->env);
    inner.env = env;
    inner.in_tail = 1;

    /* Process parameter list */
    param = params;
    while (!CL_NULL_P(param)) {
        CL_Obj p = cl_car(param);
        if (p == SYM_AMP_REST) {
            has_rest = 1;
            param = cl_cdr(param);
            if (!CL_NULL_P(param)) {
                cl_env_add_local(env, cl_car(param));
            }
            break;
        }
        cl_env_add_local(env, p);
        arity++;
        param = cl_cdr(param);
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
        int i;
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

    bc->arity = has_rest ? (arity | 0x8000) : arity;
    bc->n_locals = env->max_locals;
    bc->n_upvalues = env->upvalue_count;
    bc->name = CL_NIL;

    /* Emit closure instruction in outer compiler, then capture descriptors */
    const_idx = add_constant(c, CL_PTR_TO_OBJ(bc));
    emit(c, OP_CLOSURE);
    emit_u16(c, (uint16_t)const_idx);

    /* Emit capture descriptors: [is_local:u8, index:u8] per upvalue */
    {
        int i;
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
     */
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;

    if (sequential) {
        /* let* — each binding visible to the next */
        while (!CL_NULL_P(bindings)) {
            CL_Obj binding = cl_car(bindings);
            CL_Obj var, val;
            int slot;

            if (CL_CONS_P(binding)) {
                var = cl_car(binding);
                val = cl_car(cl_cdr(binding));
            } else {
                var = binding;
                val = CL_NIL;
            }

            c->in_tail = 0;
            compile_expr(c, val);
            slot = cl_env_add_local(env, var);
            emit(c, OP_STORE);
            emit(c, (uint8_t)slot);
            emit(c, OP_POP);
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

        /* Register locals and store values (stack top = last val) */
        for (i = 0; i < n; i++)
            cl_env_add_local(env, vars[i]);

        for (i = n - 1; i >= 0; i--) {
            emit(c, OP_STORE);
            emit(c, (uint8_t)(saved_local_count + i));
            emit(c, OP_POP);
        }
    }

    /* Compile body */
    c->in_tail = saved_tail;
    compile_body(c, body);

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
    } else {
        /* (function sym) => load function binding */
        int idx = add_constant(c, name);
        emit(c, OP_FLOAD);
        emit_u16(c, (uint16_t)idx);
    }
}

static void compile_block(CL_Compiler *c, CL_Obj form)
{
    /* Simplified: just compile body for now */
    CL_Obj body = cl_cdr(cl_cdr(form));
    compile_body(c, body);
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
        /* Named function call — load function binding */
        int idx = add_constant(c, func);
        emit(c, OP_FLOAD);
        emit_u16(c, (uint16_t)idx);
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

static CL_Obj macroexpand_1(CL_Obj form);

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
            CL_Obj expanded = macroexpand_1(expr);
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
        if (head == SYM_DEFMACRO)    { compile_defmacro(c, expr); return; }
        if (head == SYM_FUNCTION)    { compile_function(c, expr); return; }
        if (head == SYM_BLOCK)       { compile_block(c, expr); return; }
        if (head == SYM_AND)         { compile_and(c, expr); return; }
        if (head == SYM_OR)          { compile_or(c, expr); return; }
        if (head == SYM_COND)        { compile_cond(c, expr); return; }

        /* Regular function call */
        compile_call(c, expr);
        return;
    }

    cl_error(CL_ERR_GENERAL, "Cannot compile: unknown expression type");
}

/* --- Macro expansion (runtime, via VM) --- */

static CL_Obj macroexpand_1(CL_Obj form)
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
