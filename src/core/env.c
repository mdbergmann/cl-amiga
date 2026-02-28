#include "env.h"
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
    env->upvalue_count = 0;
    env->local_fun_count = 0;
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
            return env->upvalue_count++;
        }
    }

    return -1;
}
