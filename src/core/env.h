#ifndef CL_ENV_H
#define CL_ENV_H

#include "types.h"

/*
 * Compile-time lexical environment.
 * Tracks local variable bindings for the compiler.
 * Runtime environments are implicit in the VM's local variable slots.
 */

#define CL_MAX_LOCALS 256
#define CL_MAX_BINDINGS 64   /* Max bindings per let/do form (stack-allocated) */
#define CL_MAX_UPVALUES 64
#define CL_MAX_LOCAL_FUNS 64  /* flet/labels fns per form; big state-machine
                               * labels (e.g. chipz inflate = 37) exceed 32 */
#define CL_MAX_LOCAL_MACROS 16
#define CL_MAX_SYMBOL_MACROS 32

typedef struct {
    int is_local;   /* 1 = parent's local, 0 = parent's upvalue */
    int index;      /* slot index in parent */
    int is_boxed;   /* 1 = refers to a heap-boxed cell (mutable shared binding) */
} CL_UpvalueDesc;

typedef struct {
    CL_Obj name;   /* function name (symbol) */
    int slot;      /* local slot index where closure is stored */
    /* Inline expansion (compiler_special.c, "Local-function inlining"): a
     * FLET/LABELS function the compiler proved non-escaping keeps no
     * closure — every call compiles to its body in place.  inline_body is
     * (BLOCK name (LOCALLY . body)) and inline_params the required
     * parameter list; both CL_NIL for an ordinary binding, whose slot then
     * holds the closure.  The *_mark fields record how much of the
     * defining env / compiler existed when the function was defined, so a
     * call site can hide everything bound since (see the hide_* bands
     * below) and the body's free names resolve in the definition
     * environment.  The block/tagbody marks index the DEFINING compiler's
     * tables: a call is inlined only from that same compiler. */
    CL_Obj inline_body;
    CL_Obj inline_params;
    int def_local_mark;
    int def_fun_mark;
    int def_macro_mark;
    int def_smacro_mark;
    int def_block_mark;
    int def_tagbody_mark;
} CL_LocalFun;

typedef struct {
    CL_Obj name;       /* macro name (symbol) */
    CL_Obj expander;   /* compiled closure */
} CL_LocalMacro;

typedef struct {
    CL_Obj name;       /* symbol */
    CL_Obj expansion;  /* form to substitute */
    int local_count_at_create; /* env->local_count when this symbol-macro was
                                * added; a lexical local at slot >= this value
                                * was bound in an inner scope and shadows it
                                * (CLHS: a LET binding shadows a symbol-macro). */
} CL_SymbolMacro;

typedef struct CL_CompEnv {
    struct CL_CompEnv *parent;    /* Enclosing scope (for closures) */
    CL_Obj locals[CL_MAX_LOCALS]; /* Symbol for each local slot */
    int local_count;
    int max_locals;               /* High-water mark for local_count */
    int depth;                    /* Nesting depth (0 = top-level function) */
    uint8_t boxed[CL_MAX_LOCALS]; /* 1 = local is a heap-boxed cell */
    CL_UpvalueDesc upvalues[CL_MAX_UPVALUES];
    int upvalue_count;
    CL_LocalFun local_funs[CL_MAX_LOCAL_FUNS];
    int local_fun_count;
    CL_LocalMacro local_macros[CL_MAX_LOCAL_MACROS];
    int local_macro_count;
    CL_SymbolMacro symbol_macros[CL_MAX_SYMBOL_MACROS];
    int symbol_macro_count;
    /* Count of symbol_macros[] entries inherited from the parent env at
     * env-creation time.  Entries at indices [0, inherited_symbol_macro_count)
     * are inherited from a parent scope and may be shadowed by a local with
     * the same name added in this env (per CL lexical scoping).  Entries at
     * indices [inherited_symbol_macro_count, symbol_macro_count) are added
     * directly via symbol-macrolet in this env and shadow any existing local
     * with the same name. */
    int inherited_symbol_macro_count;
    /* Name-hiding bands for local-function inlining.  While an inlined
     * body is being compiled, the entries with index in [lo, hi) of
     * locals / local_funs / local_macros / symbol_macros are invisible to
     * every lookup: they were bound between the function's definition and
     * the call site, and the body must resolve its free names in the
     * definition environment (CLHS 3.1.1).  Empty (lo == hi) otherwise;
     * nested inlining saves and restores them.  The compiler keeps the
     * matching bands for its BLOCK / TAGBODY tables. */
    int hide_local_lo, hide_local_hi;
    int hide_fun_lo, hide_fun_hi;
    int hide_macro_lo, hide_macro_hi;
    int hide_smacro_lo, hide_smacro_hi;
} CL_CompEnv;

/* 1 when local slot I is visible to name lookups (outside the hiding band). */
#define CL_ENV_LOCAL_VISIBLE(env, i) \
    ((i) < (env)->hide_local_lo || (i) >= (env)->hide_local_hi)

/* Clear boxed flags for slots [from, env->local_count) so reused slots
 * aren't incorrectly treated as heap-boxed cells.  Must be called before
 * restoring local_count when leaving a scope that may have boxed locals. */
static void cl_env_clear_boxed(CL_CompEnv *env, int from)
{
    int j;
    for (j = from; j < env->local_count; j++)
        env->boxed[j] = 0;
}

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

/* Local function bindings (flet/labels).  add returns the entry index (or
 * -1 when full); lookup returns the closure SLOT of the innermost visible
 * binding, lookup_index the ENTRY index (so the caller can read the
 * inline fields) — both -1 when NAME is not bound. */
int cl_env_add_local_fun(CL_CompEnv *env, CL_Obj name, int slot);
int cl_env_lookup_local_fun(CL_CompEnv *env, CL_Obj name);
int cl_env_lookup_local_fun_index(CL_CompEnv *env, CL_Obj name);
/* 1 when NAME resolves, in ENV's parent chain, to a local function that
 * was compiled inline (no closure exists to capture) — a reference from
 * inside a nested lambda that the inlining analysis should have ruled
 * out; the compiler reports it instead of capturing an empty slot. */
int cl_env_parent_fun_is_inline(CL_CompEnv *env, CL_Obj name);

/* Resolve a local function as an upvalue (across lambda boundaries) */
int cl_env_resolve_fun_upvalue(CL_CompEnv *env, CL_Obj name);

/* Local macro bindings (macrolet) */
int cl_env_add_local_macro(CL_CompEnv *env, CL_Obj name, CL_Obj expander);
CL_Obj cl_env_lookup_local_macro(CL_CompEnv *env, CL_Obj name);

/* Symbol macro bindings (symbol-macrolet) */
int cl_env_add_symbol_macro(CL_CompEnv *env, CL_Obj name, CL_Obj expansion);
CL_Obj cl_env_lookup_symbol_macro(CL_CompEnv *env, CL_Obj name);
/* Predicate form — distinguishes "found, expands to NIL" from "not found".
 * Use this wherever a NIL expansion must still be treated as a symbol-macro
 * (e.g. variable reference / setf place compilation). */
int cl_env_lookup_symbol_macro_p(CL_CompEnv *env, CL_Obj name, CL_Obj *out);

/* Build a Lisp-visible snapshot of the lexical environment's symbol
 * macros, for use as the &environment argument of a macro expander.
 * Returns an alist of (SYMBOL . EXPANSION) pairs with innermost
 * bindings first.  Inherited symbol-macros shadowed by a local with
 * the same name in ENV are omitted.  NULL env -> NIL. */
CL_Obj cl_build_lex_env(CL_CompEnv *env);

#endif /* CL_ENV_H */
