#include "env.h"
#include "mem.h"
#include "compiler.h"
#include "../platform/platform.h"
#include <string.h>

CL_CompEnv *cl_env_create(CL_CompEnv *parent)
{
    CL_CompEnv *env = (CL_CompEnv *)platform_alloc(sizeof(CL_CompEnv));
    if (!env) return NULL;
    env->parent = parent;
    env->local_count = 0;
    env->max_locals = 0;
    env->depth = parent ? parent->depth + 1 : 0;
    memset(env->boxed, 0, sizeof(env->boxed));
    env->upvalue_count = 0;
    env->local_fun_count = 0;

    /* Inherit macrolet and symbol-macrolet bindings from parent scope.
     * Per CL spec, these are lexically scoped and visible in nested lambdas. */
    if (parent) {
        int i, n;
        n = parent->local_macro_count;
        if (n > CL_MAX_LOCAL_MACROS) n = CL_MAX_LOCAL_MACROS;
        for (i = 0; i < n; i++)
            env->local_macros[i] = parent->local_macros[i];
        env->local_macro_count = n;

        /* Inherit symbol-macros from parent, but skip any that are already
         * shadowed by a local with the same name in the parent.  Without this
         * filter, an inner lambda nested inside (symbol-macrolet ((self ...))
         * (defmethod ... ((self ...)) (lambda () self)))) would re-expand the
         * inherited symbol-macro for self, even though the outer parameter
         * shadowed it — leading to infinite recursion when the macro
         * expansion references the same name. */
        n = parent->symbol_macro_count;
        if (n > CL_MAX_SYMBOL_MACROS) n = CL_MAX_SYMBOL_MACROS;
        {
            int dst = 0;
            for (i = 0; i < n; i++) {
                CL_Obj sm_name = parent->symbol_macros[i].name;
                int is_locally_shadowed = 0;
                if (i < parent->inherited_symbol_macro_count) {
                    int j;
                    for (j = 0; j < parent->local_count; j++) {
                        if (parent->locals[j] == sm_name) {
                            is_locally_shadowed = 1;
                            break;
                        }
                    }
                }
                if (!is_locally_shadowed) {
                    env->symbol_macros[dst++] = parent->symbol_macros[i];
                }
            }
            env->symbol_macro_count = dst;
            env->inherited_symbol_macro_count = dst;
        }
    } else {
        env->local_macro_count = 0;
        env->symbol_macro_count = 0;
        env->inherited_symbol_macro_count = 0;
    }
    return env;
}

void cl_env_destroy(CL_CompEnv *env)
{
    if (env) platform_free(env);
}

int cl_env_add_local(CL_CompEnv *env, CL_Obj symbol)
{
    if (env->local_count >= CL_MAX_LOCALS) return -1;
    env->locals[env->local_count] = symbol;
    env->local_count++;
    if (env->local_count > env->max_locals)
        env->max_locals = env->local_count;
    return env->local_count - 1;
}

int cl_env_lookup(CL_CompEnv *env, CL_Obj symbol)
{
    int i;
    for (i = env->local_count - 1; i >= 0; i--) {
        if (env->locals[i] == symbol) return i;
    }
    return -1;
}

int cl_env_lookup_upvalue(CL_CompEnv *env, CL_Obj symbol,
                          int *depth_out, int *index_out)
{
    CL_CompEnv *e = env->parent;
    int depth = 1;

    while (e) {
        int idx = cl_env_lookup(e, symbol);
        if (idx >= 0) {
            *depth_out = depth;
            *index_out = idx;
            return 1;
        }
        e = e->parent;
        depth++;
    }
    return 0;
}

int cl_env_resolve_upvalue(CL_CompEnv *env, CL_Obj symbol)
{
    int i;

    if (!env->parent) return -1;

    /* Already tracked? Return existing index */
    for (i = 0; i < env->upvalue_count; i++) {
        /* Match by descriptor: check if this upvalue refers to the same
         * symbol. We need to look at what the descriptor points to.
         * Simpler: re-derive and check if we'd get the same descriptor. */
    }

    /* Check if symbol is a local in the immediate parent */
    {
        int parent_slot = cl_env_lookup(env->parent, symbol);
        if (parent_slot >= 0) {
            /* Check if already tracked */
            for (i = 0; i < env->upvalue_count; i++) {
                if (env->upvalues[i].is_local && env->upvalues[i].index == parent_slot)
                    return i;
            }
            if (env->upvalue_count >= CL_MAX_UPVALUES) return -1;
            env->upvalues[env->upvalue_count].is_local = 1;
            env->upvalues[env->upvalue_count].index = parent_slot;
            env->upvalues[env->upvalue_count].is_boxed = env->parent->boxed[parent_slot];
            return env->upvalue_count++;
        }
    }

    /* Not in parent's locals — recursively resolve in parent
     * (this makes the parent capture it too, so we can reference
     * the parent's upvalue slot) */
    {
        int parent_upval = cl_env_resolve_upvalue(env->parent, symbol);
        if (parent_upval >= 0) {
            /* Check if already tracked */
            for (i = 0; i < env->upvalue_count; i++) {
                if (!env->upvalues[i].is_local && env->upvalues[i].index == parent_upval)
                    return i;
            }
            if (env->upvalue_count >= CL_MAX_UPVALUES) return -1;
            env->upvalues[env->upvalue_count].is_local = 0;
            env->upvalues[env->upvalue_count].index = parent_upval;
            env->upvalues[env->upvalue_count].is_boxed = env->parent->upvalues[parent_upval].is_boxed;
            return env->upvalue_count++;
        }
    }

    return -1;
}

int cl_env_add_local_fun(CL_CompEnv *env, CL_Obj name, int slot)
{
    if (env->local_fun_count >= CL_MAX_LOCAL_FUNS) return -1;
    env->local_funs[env->local_fun_count].name = name;
    env->local_funs[env->local_fun_count].slot = slot;
    return env->local_fun_count++;
}

int cl_env_lookup_local_fun(CL_CompEnv *env, CL_Obj name)
{
    int i;
    for (i = env->local_fun_count - 1; i >= 0; i--) {
        if (env->local_funs[i].name == name) return env->local_funs[i].slot;
    }
    return -1;
}

int cl_env_add_local_macro(CL_CompEnv *env, CL_Obj name, CL_Obj expander)
{
    if (env->local_macro_count >= CL_MAX_LOCAL_MACROS) return -1;
    env->local_macros[env->local_macro_count].name = name;
    env->local_macros[env->local_macro_count].expander = expander;
    return env->local_macro_count++;
}

CL_Obj cl_env_lookup_local_macro(CL_CompEnv *env, CL_Obj name)
{
    int i;
    for (i = env->local_macro_count - 1; i >= 0; i--) {
        if (env->local_macros[i].name == name)
            return env->local_macros[i].expander;
    }
    return CL_NIL;
}

int cl_env_add_symbol_macro(CL_CompEnv *env, CL_Obj name, CL_Obj expansion)
{
    if (env->symbol_macro_count >= CL_MAX_SYMBOL_MACROS) return -1;
    env->symbol_macros[env->symbol_macro_count].name = name;
    env->symbol_macros[env->symbol_macro_count].expansion = expansion;
    return env->symbol_macro_count++;
}

CL_Obj cl_env_lookup_symbol_macro(CL_CompEnv *env, CL_Obj name)
{
    int i;
    for (i = env->symbol_macro_count - 1; i >= 0; i--) {
        if (env->symbol_macros[i].name != name) continue;
        /* Locally-added symbol-macro (added via symbol-macrolet in this env) —
         * always wins. */
        if (i >= env->inherited_symbol_macro_count)
            return env->symbol_macros[i].expansion;
        /* Inherited from parent — shadowed by any local of the same name in
         * this env (e.g. a lambda parameter shadows an outer symbol-macrolet
         * binding of the same name, per CL lexical scoping). */
        {
            int j;
            for (j = 0; j < env->local_count; j++) {
                if (env->locals[j] == name) return CL_NIL;
            }
        }
        return env->symbol_macros[i].expansion;
    }
    return CL_NIL;
}

CL_Obj cl_build_lex_env(CL_CompEnv *env)
{
    CL_Obj result = CL_NIL;
    int i;

    if (!env || (env->symbol_macro_count == 0 && env->local_macro_count == 0))
        return CL_NIL;

    CL_GC_PROTECT(result);

    /* Local macros (macrolet) — emit each as
     *   (SYM_LEX_LOCAL_MACRO . (name . expander-fn))
     * so &environment-aware expanders that call MACROEXPAND can find
     * macrolet-bound macros.  Innermost first so duplicate names shadow
     * outer bindings during ASSOC walk. */
    for (i = env->local_macro_count - 1; i >= 0; i--) {
        CL_Obj name = env->local_macros[i].name;
        CL_Obj expander = env->local_macros[i].expander;
        CL_Obj inner, pair;
        CL_GC_PROTECT(name);
        CL_GC_PROTECT(expander);
        inner = cl_cons(name, expander);
        CL_GC_PROTECT(inner);
        pair = cl_cons(SYM_LEX_LOCAL_MACRO, inner);
        CL_GC_PROTECT(pair);
        result = cl_cons(pair, result);
        CL_GC_UNPROTECT(4);
    }

    if (env->symbol_macro_count == 0) {
        CL_GC_UNPROTECT(1);
        return result;
    }

    /* Walk from highest index (innermost / most recently added) to lowest.
     * Innermost bindings end up at the head of the alist, so a plain
     * ASSOC walk respects shadowing automatically. */
    for (i = env->symbol_macro_count - 1; i >= 0; i--) {
        CL_Obj name = env->symbol_macros[i].name;
        CL_Obj expansion = env->symbol_macros[i].expansion;

        /* Inherited entries are shadowed by a local of the same name. */
        if (i < env->inherited_symbol_macro_count) {
            int j;
            int shadowed = 0;
            for (j = 0; j < env->local_count; j++) {
                if (env->locals[j] == name) { shadowed = 1; break; }
            }
            if (shadowed) continue;
        }

        /* Skip if a nearer binding with the same name is already in result. */
        {
            CL_Obj walk = result;
            int dup = 0;
            while (!CL_NULL_P(walk)) {
                if (cl_car(cl_car(walk)) == name) { dup = 1; break; }
                walk = cl_cdr(walk);
            }
            if (dup) continue;
        }

        {
            CL_Obj pair;
            CL_GC_PROTECT(name);
            CL_GC_PROTECT(expansion);
            pair = cl_cons(name, expansion);
            CL_GC_PROTECT(pair);
            result = cl_cons(pair, result);
            CL_GC_UNPROTECT(3);
        }
    }
    CL_GC_UNPROTECT(1);
    return result;
}

int cl_env_resolve_fun_upvalue(CL_CompEnv *env, CL_Obj name)
{
    int i;

    if (!env->parent) return -1;

    /* Check if name is a local function in the immediate parent */
    {
        int parent_slot = cl_env_lookup_local_fun(env->parent, name);
        if (parent_slot >= 0) {
            /* Capture as local from parent */
            for (i = 0; i < env->upvalue_count; i++) {
                if (env->upvalues[i].is_local && env->upvalues[i].index == parent_slot)
                    return i;
            }
            if (env->upvalue_count >= CL_MAX_UPVALUES) return -1;
            env->upvalues[env->upvalue_count].is_local = 1;
            env->upvalues[env->upvalue_count].index = parent_slot;
            env->upvalues[env->upvalue_count].is_boxed = env->parent->boxed[parent_slot];
            return env->upvalue_count++;
        }
    }

    /* Recursively resolve in parent */
    {
        int parent_upval = cl_env_resolve_fun_upvalue(env->parent, name);
        if (parent_upval >= 0) {
            for (i = 0; i < env->upvalue_count; i++) {
                if (!env->upvalues[i].is_local && env->upvalues[i].index == parent_upval)
                    return i;
            }
            if (env->upvalue_count >= CL_MAX_UPVALUES) return -1;
            env->upvalues[env->upvalue_count].is_local = 0;
            env->upvalues[env->upvalue_count].index = parent_upval;
            env->upvalues[env->upvalue_count].is_boxed = env->parent->upvalues[parent_upval].is_boxed;
            return env->upvalue_count++;
        }
    }

    return -1;
}
