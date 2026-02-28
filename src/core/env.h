#ifndef CL_ENV_H
#define CL_ENV_H

#include "types.h"

/*
 * Compile-time lexical environment.
 * Tracks local variable bindings for the compiler.
 * Runtime environments are implicit in the VM's local variable slots.
 */

#define CL_MAX_LOCALS 256

typedef struct CL_CompEnv {
    struct CL_CompEnv *parent;    /* Enclosing scope (for closures) */
    CL_Obj locals[CL_MAX_LOCALS]; /* Symbol for each local slot */
    int local_count;
    int max_locals;               /* High-water mark for local_count */
    int depth;                    /* Nesting depth (0 = top-level function) */
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

#endif /* CL_ENV_H */
