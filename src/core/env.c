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
