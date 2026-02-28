#ifndef CL_ENV_H
#define CL_ENV_H

#include "types.h"

/*
 * Compile-time lexical environment.
 * Tracks local variable bindings for the compiler.
 * Runtime environments are implicit in the VM's local variable slots.
 */

#define CL_MAX_LOCALS 256
#define CL_MAX_UPVALUES 64
#define CL_MAX_LOCAL_FUNS 32

typedef struct {
    int is_local;   /* 1 = parent's local, 0 = parent's upvalue */
    int index;      /* slot index in parent */
} CL_UpvalueDesc;

typedef struct {
    CL_Obj name;   /* function name (symbol) */
    int slot;      /* local slot index where closure is stored */
} CL_LocalFun;

typedef struct CL_CompEnv {
    struct CL_CompEnv *parent;    /* Enclosing scope (for closures) */
    CL_Obj locals[CL_MAX_LOCALS]; /* Symbol for each local slot */
    int local_count;
    int max_locals;               /* High-water mark for local_count */
    int depth;                    /* Nesting depth (0 = top-level function) */
    CL_UpvalueDesc upvalues[CL_MAX_UPVALUES];
    int upvalue_count;
    CL_LocalFun local_funs[CL_MAX_LOCAL_FUNS];
    int local_fun_count;
} CL_CompEnv;

/* Create a new compile-time environment */
CL_CompEnv *cl_env_create(CL_CompEnv *parent);

/* Free a compile-time environment */
void cl_env_destroy(CL_CompEnv *env);

/* Add a local variable, returns its slot index */
int cl_env_add_local(CL_CompEnv *env, CL_Obj symbol);

/* Look up a local variable, returns slot index or -1 */
int cl_env_lookup(CL_CompEnv *env, CL_Obj symbol);

/* Look up in parent envs for upvalue, returns 1 if found */
int cl_env_lookup_upvalue(CL_CompEnv *env, CL_Obj symbol,
                          int *depth_out, int *index_out);

/* Resolve a symbol as an upvalue in env's flat upvalue array.
 * Returns upvalue index (>=0) or -1 if not found in any parent scope. */
int cl_env_resolve_upvalue(CL_CompEnv *env, CL_Obj symbol);

/* Local function bindings (flet/labels) */
int cl_env_add_local_fun(CL_CompEnv *env, CL_Obj name, int slot);
int cl_env_lookup_local_fun(CL_CompEnv *env, CL_Obj name);

/* Resolve a local function as an upvalue (across lambda boundaries) */
int cl_env_resolve_fun_upvalue(CL_CompEnv *env, CL_Obj name);

#endif /* CL_ENV_H */
