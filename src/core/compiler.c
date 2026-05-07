#include "compiler_internal.h"
#include "thread.h"
#include "../platform/platform_thread.h"
#include <stdio.h>

/* --- Shared globals --- */
/* cl_active_compiler and pending_lambda_name are now in CL_Thread */

CL_Obj macro_table = CL_NIL;
CL_Obj setf_table = CL_NIL;
CL_Obj setf_fn_table = CL_NIL;  /* (setf name) functions: ((accessor . setf-fn-sym) ...) val-first calling */
CL_Obj setf_expander_table = CL_NIL;  /* define-setf-expander: ((name . expander-fn) ...) */
CL_Obj type_table = CL_NIL;

void *cl_tables_rwlock = NULL;

/* Forward declarations for the CL_Compiler pool helpers — defined near
 * cl_compile.  Declared here so compile_lambda (above the definitions)
 * can use them. */
static CL_Compiler *cl_compiler_pool_acquire(void);
static void cl_compiler_pool_release(CL_Compiler *c);

/* Per-thread-tracked rwlock helpers (see compiler.h).
 * cl_tables_rdlock is a macro at every call site that tags the call
 * with __FILE__ ":" __LINE__ and forwards to cl_tables_rdlock_at. */
/* Lock state tracking is keyed off whether the rwlock has been
 * initialized, NOT off CL_MT() — the latter can flip from true → false
 * while a thread holds an rdlock (e.g. when the last sento worker exits
 * after a test fixture's actor-system shutdown), which would skip the
 * unlock and leave the held counter — and the underlying platform
 * rwlock — permanently leaked.  As long as cl_tables_rwlock is non-NULL
 * (initialized in cl_compiler_init), pair every rdlock with a rwunlock
 * regardless of current thread count. */
void cl_tables_rdlock_at(const char *site)
{
    if (!cl_tables_rwlock) return;
    platform_rwlock_rdlock(cl_tables_rwlock);
    if (CT->rdlock_tables_sites_top < CL_RDLOCK_SITES_MAX)
        CT->rdlock_tables_sites[CT->rdlock_tables_sites_top] = site;
    CT->rdlock_tables_sites_top++;
    CT->rdlock_tables_held++;
}

void cl_tables_wrlock(void)
{
    if (!cl_tables_rwlock) return;
    platform_rwlock_wrlock(cl_tables_rwlock);
}

void cl_tables_rwunlock(void)
{
    if (!cl_tables_rwlock) return;
    if (CT->rdlock_tables_held > 0) {
        CT->rdlock_tables_held--;
        if (CT->rdlock_tables_sites_top > 0)
            CT->rdlock_tables_sites_top--;
    }
    platform_rwlock_unlock(cl_tables_rwlock);
}

void cl_tables_dump_rdlock_holders(const char *header)
{
    int i;
    if (!CL_MT()) return;
    fprintf(stderr, "%s\n", header ? header : "[rwlock] cl_tables_rwlock readers:");
    for (i = 0; i < CL_MAX_THREADS; i++) {
        CL_Thread *t = cl_thread_table[i];
        if (t && t->rdlock_tables_held > 0) {
            int s, top;
            fprintf(stderr, "  thread tid=%u status=%u name=", t->id, t->status);
            if (CL_NULL_P(t->name)) {
                fprintf(stderr, "(unnamed)");
            } else if (CL_STRING_P(t->name)) {
                CL_String *str = (CL_String *)CL_OBJ_TO_PTR(t->name);
                fprintf(stderr, "\"%.*s\"", (int)str->length, str->data);
            } else {
                fprintf(stderr, "?");
            }
            fprintf(stderr, " held=%d\n", t->rdlock_tables_held);
            top = t->rdlock_tables_sites_top;
            if (top > CL_RDLOCK_SITES_MAX) top = CL_RDLOCK_SITES_MAX;
            for (s = top - 1; s >= 0; s--) {
                fprintf(stderr, "    [%d] %s\n", s,
                        t->rdlock_tables_sites[s]
                          ? t->rdlock_tables_sites[s] : "(unknown)");
            }
        }
    }
    fflush(stderr);
}

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
CL_Obj SETF_SYM_SYMBOL_PLIST = CL_NIL;
CL_Obj SETF_SYM_FDEFINITION = CL_NIL;
CL_Obj SETF_HELPER_NTH = CL_NIL;
CL_Obj SETF_HELPER_SV = CL_NIL;
CL_Obj SETF_HELPER_SF = CL_NIL;
CL_Obj SETF_HELPER_SP = CL_NIL;
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

/* Sentinel symbol used inside the lex-env alist built by
 * cl_build_lex_env to mark MACROLET-bound expanders.  An entry of the
 * form (SYM_LEX_LOCAL_MACRO . (name . expander-fn)) carries a local
 * macro; an ordinary (name . expansion-form) entry carries a symbol
 * macro.  Lookup walkers check the car against this sentinel to
 * disambiguate. */
CL_Obj SYM_LEX_LOCAL_MACRO = CL_NIL;

/* Global optimization settings */
CL_OptimizeSettings cl_optimize_settings = {1, 1, 1, 1};

/* --- Source-file intern pool --- */
/* CL_Bytecode.source_file holds a const char* that must outlive the bytecode
 * (which can persist for the entire process lifetime).  Earlier versions
 * stored a pointer into bi_compile_file's stack-local in_path[] buffer; once
 * compile-file returned the pointer dangled and any later access (FASL
 * serialize, FATAL crash trace, debugger backtrace, vm dispatch) would read
 * arbitrary bytes from a re-used stack frame, occasionally corrupting CLOS
 * metadata referenced by ASDF and crashing in completely unrelated code.
 *
 * Fix: intern paths once into a process-lifetime pool and hand back a stable
 * pointer.  Linear search is fine — typical sessions touch a few hundred
 * source files at most. */
typedef struct CL_SourceFileEntry_s {
    char *path;
    struct CL_SourceFileEntry_s *next;
} CL_SourceFileEntry;

static CL_SourceFileEntry *cl_source_file_pool = NULL;
static void *cl_source_file_pool_lock = NULL;

const char *cl_intern_source_file(const char *path)
{
    CL_SourceFileEntry *e;
    char *copy;
    size_t len;

    if (!path || !path[0]) return NULL;
    len = strlen(path);

    if (!cl_source_file_pool_lock)
        platform_rwlock_init(&cl_source_file_pool_lock);

    platform_rwlock_wrlock(cl_source_file_pool_lock);
    for (e = cl_source_file_pool; e != NULL; e = e->next) {
        if (strcmp(e->path, path) == 0) {
            platform_rwlock_unlock(cl_source_file_pool_lock);
            return e->path;
        }
    }
    e = (CL_SourceFileEntry *)platform_alloc(sizeof(CL_SourceFileEntry));
    copy = (char *)platform_alloc(len + 1);
    if (!e || !copy) {
        if (e) platform_free(e);
        if (copy) platform_free(copy);
        platform_rwlock_unlock(cl_source_file_pool_lock);
        return NULL;
    }
    memcpy(copy, path, len + 1);
    e->path = copy;
    e->next = cl_source_file_pool;
    cl_source_file_pool = e;
    platform_rwlock_unlock(cl_source_file_pool_lock);
    return copy;
}

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

void cl_emit_i32(CL_Compiler *c, int32_t val)
{
    cl_emit(c, (uint8_t)((val >> 24) & 0xFF));
    cl_emit(c, (uint8_t)((val >> 16) & 0xFF));
    cl_emit(c, (uint8_t)((val >> 8) & 0xFF));
    cl_emit(c, (uint8_t)(val & 0xFF));
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
    cl_emit_i32(c, 0); /* placeholder */
    return pos;
}

void cl_patch_jump(CL_Compiler *c, int patch_pos)
{
    int32_t offset = c->code_pos - (patch_pos + 4);
    c->code[patch_pos]     = (uint8_t)((offset >> 24) & 0xFF);
    c->code[patch_pos + 1] = (uint8_t)((offset >> 16) & 0xFF);
    c->code[patch_pos + 2] = (uint8_t)((offset >> 8) & 0xFF);
    c->code[patch_pos + 3] = (uint8_t)(offset & 0xFF);
}

void cl_emit_loop_jump(CL_Compiler *c, uint8_t op, int target)
{
    int32_t offset;
    cl_emit(c, op);
    offset = target - (c->code_pos + 4);
    cl_emit_i32(c, offset);
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

/* Trampoline-aware IF using a two-stage continuation pattern.
 *
 * Stage 1 (this fn): compile TEST, emit JNIL → jnil_pos, push an
 *   IF_AFTER_THEN frame carrying jnil_pos + the ELSE form, and return
 *   the THEN form.  The driver compiles THEN inside its trampoline,
 *   keeping the C stack flat.
 * Stage 2 (IF_AFTER_THEN postlude): emit JMP → jmp_pos, patch jnil_pos.
 *   If there's an ELSE form to dispatch, push an IF_AFTER_ELSE frame
 *   (carrying jmp_pos) and return the ELSE form so the driver
 *   trampolines into it; otherwise emit OP_NIL inline and patch jmp_pos
 *   immediately.
 * Stage 3 (IF_AFTER_ELSE postlude): patch jmp_pos.
 *
 * Without this, the ELSE branch was already trampolined but the THEN
 * branch still recursed via compile_expr — every IF nesting level on
 * the THEN side ate one C frame.  Heavily-inlined macro output
 * (serapeum's dispatch-case) blew the stack at a few hundred levels. */
static CL_Obj compile_if(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);
    CL_Obj test = cl_car(rest);
    CL_Obj then_form = cl_car(cl_cdr(rest));
    CL_Obj else_form;
    int saved_tail = c->in_tail;
    int jnil_pos;
    CL_TailFrame *tf;

    /* `(if test then)` and `(if test then nil)` are equivalent (CLHS
     * 5.2): treat both as "no else" — we'll emit OP_NIL inline in the
     * AFTER_THEN postlude rather than dispatching the literal NIL. */
    {
        CL_Obj cdr2 = cl_cdr(cl_cdr(rest));
        else_form = CL_NULL_P(cdr2) ? CL_NIL : cl_car(cdr2);
    }

    CL_GC_PROTECT(form);

    c->in_tail = 0;
    compile_expr(c, test);
    jnil_pos = cl_emit_jump(c, OP_JNIL);

    CL_GC_UNPROTECT(1);

    c->in_tail = saved_tail;
    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_IF_AFTER_THEN;
    tf->block_push_pos = jnil_pos;
    tf->saved_tail = saved_tail;
    tf->cont_form = else_form;  /* GC-traced via tail_stack walk */
    return then_form;
}

/* --- Tail-trampoline frame management --- */

/* Push a new tail frame, growing the stack lazily.  Returns a pointer to
 * the frame (caller fills in kind/state).  Aborts on alloc failure — the
 * compiler can't continue in that state. */
CL_TailFrame *cl_tail_push(CL_Compiler *c)
{
    CL_TailFrame *tf;
    if (c->tail_count == c->tail_capacity) {
        int new_cap = c->tail_capacity ? c->tail_capacity * 2 : 64;
        CL_TailFrame *nf = (CL_TailFrame *)platform_alloc((size_t)new_cap * sizeof(CL_TailFrame));
        if (!nf) cl_error(CL_ERR_STORAGE, "compiler: out of memory growing tail stack");
        if (c->tail_stack) {
            memcpy(nf, c->tail_stack, (size_t)c->tail_count * sizeof(CL_TailFrame));
            platform_free(c->tail_stack);
        }
        c->tail_stack = nf;
        c->tail_capacity = new_cap;
    }
    tf = &c->tail_stack[c->tail_count++];
    tf->cont_form = CL_NIL;  /* default: no continuation */
    return tf;
}

/* Forward declaration: drain helper used by compile_progn below to
 * handle any PROGN_ITER frames left over after compile_progn_tail. */
static CL_Obj emit_tail_postlude(CL_Compiler *c, CL_TailFrame *tf);

/* Compile every form in `forms` for value, with OP_POP between non-last
 * forms.  Used by trampoline-aware callers that want to splice the
 * body's first form into their outer trampoline.
 *
 * Returns CL_NIL for an empty body (caller emits OP_NIL itself).
 * Returns the only body form when forms has length 1 (no frame pushed).
 * Otherwise pushes a CL_TAIL_PROGN_ITER frame carrying the remainder
 * of the form list and returns the first form — the postlude emits
 * OP_POP and continues with the next form, keeping body iteration off
 * the C stack. */
CL_Obj compile_progn_tail(CL_Compiler *c, CL_Obj forms)
{
    CL_TailFrame *tf;
    int saved_tail;

    if (CL_NULL_P(forms)) return CL_NIL;
    if (CL_NULL_P(cl_cdr(forms)))
        return cl_car(forms);  /* single tail form — caller dispatches */

    saved_tail = c->in_tail;
    c->in_tail = 0;  /* non-tail position for first body form */
    tf = cl_tail_push(c);
    tf->kind = CL_TAIL_PROGN_ITER;
    tf->saved_tail = saved_tail;
    tf->cont_form = cl_cdr(forms);  /* remaining forms after the first */
    return cl_car(forms);
}

/* Backward-compatible wrapper for callers that don't want to manage the
 * tail form themselves.  Compiles the entire sequence inline.
 *
 * compile_progn_tail may push PROGN_ITER frames whose postludes need to
 * drain.  compile_expr's drain stops at its own tail_base (which
 * captures c->tail_count at entry, including any frames we pushed
 * before calling it).  So we run a manual drain here to clean those
 * PROGN_ITER frames up. */
void compile_progn(CL_Compiler *c, CL_Obj forms)
{
    int tail_base = c->tail_count;
    CL_Obj tail = compile_progn_tail(c, forms);
    if (CL_NULL_P(tail)) {
        cl_emit(c, OP_NIL);
        return;
    }
    compile_expr(c, tail);
    while (c->tail_count > tail_base) {
        CL_TailFrame frame = c->tail_stack[--c->tail_count];
        CL_Obj cont = emit_tail_postlude(c, &frame);
        if (!CL_NULL_P(cont))
            compile_expr(c, cont);
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
            if (ll->n_required >= CL_MAX_LOCALS)
                cl_error(CL_ERR_GENERAL, "Too many required parameters (max %d)", CL_MAX_LOCALS);
            ll->required[ll->n_required++] = item;
            break;
        case 1:
            if (CL_CONS_P(item)) {
                ll->opt_names[ll->n_optional] = cl_car(item);
                ll->opt_defaults[ll->n_optional] = cl_car(cl_cdr(item));
                /* Third element is supplied-p variable: (name default svar) */
                {
                    CL_Obj cddr = cl_cdr(cl_cdr(item));
                    ll->opt_suppliedp[ll->n_optional] = CL_NULL_P(cddr) ? CL_NIL : cl_car(cddr);
                }
            } else {
                ll->opt_names[ll->n_optional] = item;
                ll->opt_defaults[ll->n_optional] = CL_NIL;
                ll->opt_suppliedp[ll->n_optional] = CL_NIL;
            }
            if (ll->n_optional >= CL_MAX_LOCALS)
                cl_error(CL_ERR_GENERAL, "Too many optional parameters (max %d)", CL_MAX_LOCALS);
            ll->n_optional++;
            break;
        case 2:
            ll->rest_name = item;
            ll->has_rest = 1;
            state = 3;
            break;
        case 3:
            if (CL_CONS_P(item)) {
                CL_Obj name_part = cl_car(item);
                if (CL_CONS_P(name_part)) {
                    /* ((:keyword var) default svar) — explicit keyword name */
                    ll->key_keywords[ll->n_keys] = cl_car(name_part);
                    ll->key_names[ll->n_keys] = cl_car(cl_cdr(name_part));
                } else {
                    /* (name default svar) — keyword inferred from name */
                    ll->key_names[ll->n_keys] = name_part;
                    ll->key_keywords[ll->n_keys] = CL_NIL; /* set below if not set */
                }
                ll->key_defaults[ll->n_keys] = cl_car(cl_cdr(item));
                /* Third element is supplied-p variable: (...  default svar) */
                {
                    CL_Obj cddr = cl_cdr(cl_cdr(item));
                    ll->key_suppliedp[ll->n_keys] = CL_NULL_P(cddr) ? CL_NIL : cl_car(cddr);
                }
            } else {
                ll->key_names[ll->n_keys] = item;
                ll->key_defaults[ll->n_keys] = CL_NIL;
                ll->key_suppliedp[ll->n_keys] = CL_NIL;
                ll->key_keywords[ll->n_keys] = CL_NIL;
            }
            /* Infer keyword from variable name if not explicitly set */
            if (ll->key_keywords[ll->n_keys] == CL_NIL) {
                const char *name_str = cl_symbol_name(ll->key_names[ll->n_keys]);
                ll->key_keywords[ll->n_keys] = cl_intern_keyword(
                    name_str, (uint32_t)strlen(name_str));
            }
            if (ll->n_keys >= CL_MAX_LOCALS)
                cl_error(CL_ERR_GENERAL, "Too many keyword parameters (max %d)", CL_MAX_LOCALS);
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
            if (ll->n_aux >= CL_MAX_LOCALS)
                cl_error(CL_ERR_GENERAL, "Too many &aux bindings (max %d)", CL_MAX_LOCALS);
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
    CL_Bytecode *bc;
    int const_idx;
    int i;
    /* ll, key_slot_indices, key_suppliedp_indices, param_vars, param_slots,
     * lambda_needs_boxing are all in CL_Compiler struct (heap-allocated)
     * to avoid stack overflow on AmigaOS 65KB stack */


    /* Heap-allocate inner compiler (too large for AmigaOS stack).
     * Routed through cl_compiler_pool_acquire so the 155KB block is
     * recycled across calls and never returned to AmigaOS — see the
     * pool comment in cl_compile. */
    inner = cl_compiler_pool_acquire();
    if (!inner) {
        platform_write_string("[compile_lambda] pool_acquire FAILED\n");
        return;
    }
    memset(inner, 0, sizeof(*inner));

    /* Register inner compiler for GC root marking.
     * Protect from NLX-triggered cl_compiler_restore_to: this compiler
     * is referenced by C stack frames throughout compile_lambda. */
    inner->parent = cl_active_compiler;
    inner->protect = 1;
    cl_active_compiler = inner;

    parse_lambda_list(params, &inner->ll);

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


    for (i = 0; i < inner->ll.n_required; i++)
        cl_env_add_local(env, inner->ll.required[i]);

    /* Emit prologue for optional defaults.
     * Add each optional local AFTER compiling its default expression,
     * so the default can refer to earlier params but not the current one.
     * This is critical for &optional (*special* *special*) where the
     * default should read the dynamic/global value, not the uninitialized slot. */
    for (i = 0; i < inner->ll.n_optional; i++) {
        if (!CL_NULL_P(inner->ll.opt_defaults[i])) {
            int skip_pos;
            cl_emit(inner, OP_ARGC);
            cl_emit_const(inner, CL_MAKE_FIXNUM(inner->ll.n_required + i + 1));
            cl_emit(inner, OP_GE);
            skip_pos = cl_emit_jump(inner, OP_JTRUE);
            {
                int saved = inner->in_tail;
                inner->in_tail = 0;
                compile_expr(inner, inner->ll.opt_defaults[i]);
                inner->in_tail = saved;
            }
            cl_emit(inner, OP_STORE);
            cl_emit(inner, (uint8_t)(inner->ll.n_required + i));
            cl_emit(inner, OP_POP);
            cl_patch_jump(inner, skip_pos);
        }
        cl_env_add_local(env, inner->ll.opt_names[i]);
    }
    /* Allocate slots for &optional supplied-p variables and emit init code.
     * Each supplied-p var is T if argc >= n_required + i + 1, else NIL. */
    for (i = 0; i < inner->ll.n_optional; i++) {
        if (!CL_NULL_P(inner->ll.opt_suppliedp[i])) {
            int sp_slot = cl_env_add_local(env, inner->ll.opt_suppliedp[i]);
            int skip_pos;
            /* Default is NIL (already zero-initialized by VM).
             * If argument was supplied, set to T. */
            cl_emit(inner, OP_ARGC);
            cl_emit_const(inner, CL_MAKE_FIXNUM(inner->ll.n_required + i + 1));
            cl_emit(inner, OP_GE);
            skip_pos = cl_emit_jump(inner, OP_JNIL);
            cl_emit_const(inner, CL_T);
            cl_emit(inner, OP_STORE);
            cl_emit(inner, (uint8_t)sp_slot);
            cl_emit(inner, OP_POP);
            cl_patch_jump(inner, skip_pos);
        }
    }

    if (inner->ll.has_rest)
        cl_env_add_local(env, inner->ll.rest_name);
    for (i = 0; i < inner->ll.n_keys; i++)
        inner->key_slot_indices[i] = cl_env_add_local(env, inner->ll.key_names[i]);
    /* Always allocate a tracking slot per key for VM-level supplied-p.
     * If user also declared a supplied-p var, reuse the same slot. */
    for (i = 0; i < inner->ll.n_keys; i++) {
        if (!CL_NULL_P(inner->ll.key_suppliedp[i]))
            inner->key_suppliedp_indices[i] = cl_env_add_local(env, inner->ll.key_suppliedp[i]);
        else
            inner->key_suppliedp_indices[i] = alloc_temp_slot(env);
    }
    /* &aux locals are added one at a time during prologue emission
     * (see below) so each init form is compiled in an environment where
     * the variable being bound is NOT yet visible — matching LET* semantics
     * per CLHS 3.4.1.4.  This lets `(&aux (record (list record)))` see the
     * outer parameter `record` in the init form rather than the freshly-
     * shadowed (NIL) slot. */

    /* Emit prologue for key defaults: check the VM-set tracking slot,
     * not the key value itself (which could legitimately be NIL).
     * Per CL spec (CLHS 3.4.1), init-forms can reference earlier params
     * but NOT the current param or later ones.  We temporarily hide
     * params i..N-1 from the local env so that if a key param shadows a
     * special variable, the default form sees the dynamic value. */
    for (i = 0; i < inner->ll.n_keys; i++) {
        if (!CL_NULL_P(inner->ll.key_defaults[i])) {
            int skip_pos, j;
            /* Hide current and later key params from local env */
            CL_Obj saved_key_locals[CL_MAX_LOCALS];
            for (j = i; j < inner->ll.n_keys; j++) {
                int s = inner->key_slot_indices[j];
                saved_key_locals[j - i] = env->locals[s];
                env->locals[s] = CL_MAKE_FIXNUM(0); /* non-symbol sentinel */
            }
            cl_emit(inner, OP_LOAD);
            cl_emit(inner, (uint8_t)inner->key_suppliedp_indices[i]);
            skip_pos = cl_emit_jump(inner, OP_JTRUE);
            {
                int saved = inner->in_tail;
                inner->in_tail = 0;
                compile_expr(inner, inner->ll.key_defaults[i]);
                inner->in_tail = saved;
            }
            cl_emit(inner, OP_STORE);
            cl_emit(inner, (uint8_t)inner->key_slot_indices[i]);
            cl_emit(inner, OP_POP);
            cl_patch_jump(inner, skip_pos);
            /* Restore hidden key params */
            for (j = i; j < inner->ll.n_keys; j++) {
                env->locals[inner->key_slot_indices[j]] = saved_key_locals[j - i];
            }
        }
    }

    /* Emit prologue for &aux bindings.  Add the local AFTER compiling
     * its init form so the init sees the outer scope (parameter or earlier
     * binding) rather than a freshly-shadowed NIL slot of the same name —
     * LET* semantics per CLHS 3.4.1.4. */
    for (i = 0; i < inner->ll.n_aux; i++) {
        int aux_slot;
        {
            int saved = inner->in_tail;
            inner->in_tail = 0;
            compile_expr(inner, inner->ll.aux_inits[i]);
            inner->in_tail = saved;
        }
        aux_slot = cl_env_add_local(env, inner->ll.aux_names[i]);
        cl_emit(inner, OP_STORE);
        cl_emit(inner, (uint8_t)aux_slot);
        cl_emit(inner, OP_POP);
    }

    /* Box params that are both mutated and captured across closure boundary */
    {
        int n_params = 0;

        for (i = 0; i < inner->ll.n_required; i++) {
            inner->param_slots[n_params] = cl_env_lookup(env, inner->ll.required[i]);
            inner->param_vars[n_params] = inner->ll.required[i];
            n_params++;
        }
        for (i = 0; i < inner->ll.n_optional; i++) {
            inner->param_slots[n_params] = cl_env_lookup(env, inner->ll.opt_names[i]);
            inner->param_vars[n_params] = inner->ll.opt_names[i];
            n_params++;
        }
        if (inner->ll.has_rest) {
            inner->param_slots[n_params] = cl_env_lookup(env, inner->ll.rest_name);
            inner->param_vars[n_params] = inner->ll.rest_name;
            n_params++;
        }
        for (i = 0; i < inner->ll.n_keys; i++) {
            inner->param_slots[n_params] = inner->key_slot_indices[i];
            inner->param_vars[n_params] = inner->ll.key_names[i];
            n_params++;
        }
        for (i = 0; i < inner->ll.n_aux; i++) {
            inner->param_slots[n_params] = cl_env_lookup(env, inner->ll.aux_names[i]);
            inner->param_vars[n_params] = inner->ll.aux_names[i];
            n_params++;
        }

        if (n_params > 0) {
            determine_boxed_vars(body, inner->param_vars, n_params, inner->lambda_needs_boxing);
            for (i = 0; i < n_params; i++) {
                if (inner->lambda_needs_boxing[i] && inner->param_slots[i] >= 0) {
                    cl_emit(inner, OP_LOAD);
                    cl_emit(inner, (uint8_t)inner->param_slots[i]);
                    cl_emit(inner, OP_MAKE_CELL);
                    cl_emit(inner, OP_STORE);
                    cl_emit(inner, (uint8_t)inner->param_slots[i]);
                    cl_emit(inner, OP_POP);
                    env->boxed[inner->param_slots[i]] = 1;
                }
            }
        }
    }

    /* Emit OP_DYNBIND for parameters that are special.
     * Check both globally-proclaimed specials and locally-declared specials
     * from (declare (special ...)) in the function body.
     * The VM stores parameter values in local slots, but called functions
     * and closures need to see them via the dynamic binding stack. */
    {
        int special_param_count = 0;
        int pi;
        CL_Obj local_specials = scan_local_specials(body);
        for (pi = 0; pi < inner->ll.n_required; pi++) {
            if (cl_symbol_specialp(inner->ll.required[pi]) ||
                is_locally_special(inner->ll.required[pi], local_specials)) {
                int slot = cl_env_lookup(env, inner->ll.required[pi]);
                int idx = cl_add_constant(inner, inner->ll.required[pi]);
                cl_emit(inner, OP_LOAD);
                cl_emit(inner, (uint8_t)slot);
                cl_emit(inner, OP_DYNBIND);
                cl_emit_u16(inner, (uint16_t)idx);
                special_param_count++;
            }
        }
        for (pi = 0; pi < inner->ll.n_optional; pi++) {
            if (cl_symbol_specialp(inner->ll.opt_names[pi]) ||
                is_locally_special(inner->ll.opt_names[pi], local_specials)) {
                int slot = cl_env_lookup(env, inner->ll.opt_names[pi]);
                int idx = cl_add_constant(inner, inner->ll.opt_names[pi]);
                cl_emit(inner, OP_LOAD);
                cl_emit(inner, (uint8_t)slot);
                cl_emit(inner, OP_DYNBIND);
                cl_emit_u16(inner, (uint16_t)idx);
                special_param_count++;
            }
        }
        if (inner->ll.has_rest && (cl_symbol_specialp(inner->ll.rest_name) ||
            is_locally_special(inner->ll.rest_name, local_specials))) {
            int slot = cl_env_lookup(env, inner->ll.rest_name);
            int idx = cl_add_constant(inner, inner->ll.rest_name);
            cl_emit(inner, OP_LOAD);
            cl_emit(inner, (uint8_t)slot);
            cl_emit(inner, OP_DYNBIND);
            cl_emit_u16(inner, (uint16_t)idx);
            special_param_count++;
        }
        for (pi = 0; pi < inner->ll.n_keys; pi++) {
            if (cl_symbol_specialp(inner->ll.key_names[pi]) ||
                is_locally_special(inner->ll.key_names[pi], local_specials)) {
                int slot = inner->key_slot_indices[pi];
                int idx = cl_add_constant(inner, inner->ll.key_names[pi]);
                cl_emit(inner, OP_LOAD);
                cl_emit(inner, (uint8_t)slot);
                cl_emit(inner, OP_DYNBIND);
                cl_emit_u16(inner, (uint16_t)idx);
                special_param_count++;
            }
        }
        for (pi = 0; pi < inner->ll.n_aux; pi++) {
            if (cl_symbol_specialp(inner->ll.aux_names[pi]) ||
                is_locally_special(inner->ll.aux_names[pi], local_specials)) {
                int slot = cl_env_lookup(env, inner->ll.aux_names[pi]);
                int idx = cl_add_constant(inner, inner->ll.aux_names[pi]);
                cl_emit(inner, OP_LOAD);
                cl_emit(inner, (uint8_t)slot);
                cl_emit(inner, OP_DYNBIND);
                cl_emit_u16(inner, (uint16_t)idx);
                special_param_count++;
            }
        }
        if (special_param_count > 0)
            inner->in_tail = 0;
        inner->special_depth += special_param_count;

        compile_body(inner, body);

        inner->special_depth -= special_param_count;
        if (special_param_count > 0) {
            cl_emit(inner, OP_DYNUNBIND);
            cl_emit(inner, (uint8_t)special_param_count);
        }
    }
    cl_emit(inner, OP_RET);

    /* Build bytecode object */
    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) {
        inner->protect = 0;
        cl_active_compiler = inner->parent;
        cl_env_destroy(env);
        cl_compiler_pool_release(inner);
        return;
    }

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

    bc->arity = inner->ll.has_rest ? (inner->ll.n_required | 0x8000) : inner->ll.n_required;
    bc->n_locals = env->max_locals;
    bc->n_upvalues = env->upvalue_count;
    bc->name = pending_lambda_name;
    pending_lambda_name = CL_NIL;
    bc->n_optional = (uint8_t)inner->ll.n_optional;
    bc->flags = ((inner->ll.n_keys > 0 || inner->ll.allow_other_keys) ? 1 : 0)
              | (inner->ll.allow_other_keys ? 2 : 0);
    bc->n_keys = (uint8_t)inner->ll.n_keys;

    if (inner->ll.n_keys > 0) {
        bc->key_syms = (CL_Obj *)platform_alloc(inner->ll.n_keys * sizeof(CL_Obj));
        bc->key_slots = (uint8_t *)platform_alloc(inner->ll.n_keys);
        bc->key_suppliedp_slots = (uint8_t *)platform_alloc(inner->ll.n_keys);
        for (i = 0; i < inner->ll.n_keys; i++) {
            bc->key_syms[i] = inner->ll.key_keywords[i];
            bc->key_slots[i] = (uint8_t)inner->key_slot_indices[i];
            bc->key_suppliedp_slots[i] = (uint8_t)inner->key_suppliedp_indices[i];
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

    /* Unregister inner compiler from GC root chain */
    inner->protect = 0;
    cl_active_compiler = inner->parent;

    cl_env_destroy(env);
    cl_compiler_pool_release(inner);
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
    /* Vector template — scan each element as a sub-template
     * (unquotes inside #(... ,foo ...) need to box `foo` if captured). */
    if (CL_VECTOR_P(tmpl)) {
        CL_Vector *v = (CL_Vector *)CL_OBJ_TO_PTR(tmpl);
        uint32_t i, n = cl_vector_active_length(v);
        CL_Obj *data = cl_vector_data(v);
        for (i = 0; i < n; i++)
            scan_qq_for_boxing(data[i], vars, n_vars,
                               mutated, captured, closure_depth);
        return;
    }

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
/* Recursion depth limits for scanner — prevents C stack overflow from deeply
 * nested forms and macro chains (e.g., misc-extensions new-let in fset) */
static int scan_macro_depth = 0;
#define SCAN_MACRO_MAX_DEPTH 50
static int scan_recurse_depth = 0;
#define SCAN_MAX_RECURSE_DEPTH 500

void scan_body_for_boxing(CL_Obj form, CL_Obj *vars, int n_vars,
                          uint8_t *mutated, uint8_t *captured,
                          int closure_depth)
{
    CL_Obj head, rest;

top:
    if (CL_NULL_P(form) || CL_FIXNUM_P(form) || CL_CHAR_P(form))
        return;

    if (scan_recurse_depth >= SCAN_MAX_RECURSE_DEPTH)
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

    /* (declare specifier...) — declarations are metadata, never evaluated.
     * Walking them with the general fall-through tries to macroexpand any
     * cons whose head names a registered macro, but inside (type T-SPEC ...)
     * or (ftype T-SPEC ...) the T-SPEC is a *type* specifier, not a form.
     * Symbols can be both deftype'd and macrobound to different things
     * (e.g. serapeum's `->`), so the spurious expansion calls the macro
     * with type-spec arguments and corrupts compiler state. */
    if (head == SYM_DECLARE) return;

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
            } else if (CL_CONS_P(place) && cl_car(place) == SYM_THE) {
                /* (setf (the type var) val) — the inner var is mutated */
                CL_Obj inner = cl_car(cl_cdr(cl_cdr(place)));
                if (CL_SYMBOL_P(inner)) {
                    int idx = find_var_index(inner, vars, n_vars);
                    if (idx >= 0) mutated[idx] = 1;
                    if (closure_depth > 0 && idx >= 0) captured[idx] = 1;
                } else {
                    scan_body_for_boxing(place, vars, n_vars,
                                         mutated, captured, closure_depth);
                }
            } else if (CL_CONS_P(place) && cl_car(place) == SYM_PROGN) {
                /* (setf (progn form... var) val) — the inner place is mutated */
                CL_Obj pforms = cl_cdr(place);
                /* Walk to last element (the actual place) */
                while (CL_CONS_P(pforms) && CL_CONS_P(cl_cdr(pforms))) {
                    scan_body_for_boxing(cl_car(pforms), vars, n_vars,
                                         mutated, captured, closure_depth);
                    pforms = cl_cdr(pforms);
                }
                if (CL_CONS_P(pforms)) {
                    CL_Obj inner = cl_car(pforms);
                    if (CL_SYMBOL_P(inner)) {
                        int idx = find_var_index(inner, vars, n_vars);
                        if (idx >= 0) mutated[idx] = 1;
                        if (closure_depth > 0 && idx >= 0) captured[idx] = 1;
                    } else {
                        scan_body_for_boxing(inner, vars, n_vars,
                                             mutated, captured, closure_depth);
                    }
                }
            } else if (CL_CONS_P(place)) {
                /* Generalized place — check for define-setf-expander.
                 * If one is registered, call it to get the expansion form
                 * and scan THAT for mutations.  This catches cases like
                 * (setf (lookup coll key) val) where the expander generates
                 * (setq coll ...) internally. */
                CL_Obj place_head = cl_car(place);
                int expanded_setf = 0;
                if (CL_SYMBOL_P(place_head) && scan_macro_depth < SCAN_MACRO_MAX_DEPTH) {
                    CL_Obj expander_fn_found = CL_NIL;
                    {
                        CL_Obj exp_entry;
                        cl_tables_rdlock();
                        exp_entry = setf_expander_table;
                        while (!CL_NULL_P(exp_entry)) {
                            CL_Obj pair = cl_car(exp_entry);
                            if (cl_car(pair) == place_head) {
                                expander_fn_found = cl_cdr(pair);
                                break;
                            }
                            exp_entry = cl_cdr(exp_entry);
                        }
                        cl_tables_rwunlock();
                    }
                    if (!CL_NULL_P(expander_fn_found)) {
                        {
                            /* Found expander — call it with error recovery */
                            CL_Obj expander_fn = expander_fn_found;
                            CL_Obj call_args[2];
                            int saved_sp = cl_vm.sp;
                            int saved_fp = cl_vm.fp;
                            int saved_dyn = cl_dyn_top;
                            int saved_nlx = cl_nlx_top;
                            int saved_handler = cl_handler_top;
                            int saved_restart = cl_restart_top;
                            int saved_debugger = cl_debugger_enabled;
                            int saved_gc_roots = gc_root_count;
                            cl_debugger_enabled = 0;
                            call_args[0] = place;
                            call_args[1] = val;
                            scan_macro_depth++;
                            {
                                int err; CL_CATCH(err);
                                if (err == 0) {
                                    CL_Obj expansion;
                                    expansion = cl_vm_apply(expander_fn, call_args, 2);
                                    CL_UNCATCH();
                                    cl_debugger_enabled = saved_debugger;
                                    cl_handler_top = saved_handler;
                                    cl_restart_top = saved_restart;
                                    CL_GC_PROTECT(expansion);
                                    scan_body_for_boxing(expansion, vars, n_vars,
                                                         mutated, captured, closure_depth);
                                    CL_GC_UNPROTECT(1);
                                    expanded_setf = 1;
                                } else {
                                    CL_UNCATCH();
                                    cl_vm.sp = saved_sp;
                                    cl_vm.fp = saved_fp;
                                    cl_dynbind_restore_to(saved_dyn);
                                    cl_nlx_top = saved_nlx;
                                    cl_handler_top = saved_handler;
                                    cl_restart_top = saved_restart;
                                    cl_debugger_enabled = saved_debugger;
                                    gc_root_count = saved_gc_roots;
                                }
                            }
                            scan_macro_depth--;
                        }
                    }
                }
                if (!expanded_setf) {
                    /* No expander or expansion failed — scan sub-expressions */
                    scan_body_for_boxing(place, vars, n_vars,
                                         mutated, captured, closure_depth);
                }
            } else {
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

    /* (case/ecase/typecase/etypecase keyform (key body...)...) —
     * Scan keyform and clause bodies, but NOT the keys (they are literals,
     * not evaluable forms; a key like TUPLE could be a macro name). */
    if (head == SYM_CASE || head == SYM_ECASE ||
        head == SYM_TYPECASE || head == SYM_ETYPECASE) {
        CL_Obj clauses;
        if (!CL_CONS_P(rest)) return;
        /* Scan keyform */
        scan_body_for_boxing(cl_car(rest), vars, n_vars,
                             mutated, captured, closure_depth);
        /* Scan clause bodies (skip keys) */
        clauses = cl_cdr(rest);
        while (CL_CONS_P(clauses)) {
            CL_Obj clause = cl_car(clauses);
            if (CL_CONS_P(clause)) {
                /* Skip key (car), scan body forms (cdr) */
                CL_Obj cbody = cl_cdr(clause);
                while (CL_CONS_P(cbody)) {
                    scan_body_for_boxing(cl_car(cbody), vars, n_vars,
                                         mutated, captured, closure_depth);
                    cbody = cl_cdr(cbody);
                }
            }
            clauses = cl_cdr(clauses);
        }
        return;
    }

    /* (macrolet ((name lambda-list . body) ...) . body)
     * The macro definitions are templates compiled at the macrolet site
     * (see compile_macrolet) — they are NOT regular code in this scope.
     * Walking them as code mistakes the binding car (the macro name) for
     * a function call, e.g. (plet-if (params) `template) gets seen as a
     * 2-arg call to a globally-defined plet-if and the scanner expands it
     * with bogus args.  Cascading expansions can hit a macro that errors
     * (signaling lparallel's %plet-if expecting 4 args, getting 2), which
     * longjmps through scan_body_for_boxing's CL_CATCH and — empirically —
     * smashes bi_compile_file's stack canary on return.  Skip the bindings
     * entirely and only scan the macrolet body. */
    if (head == SYM_MACROLET) {
        CL_Obj body;
        if (!CL_CONS_P(rest)) return;
        body = cl_cdr(rest); /* skip the bindings list */
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

    /* (symbol-macrolet ((sym expansion) ...) . body)
     * Same reasoning as MACROLET — the bindings are (sym expansion) pairs,
     * not forms to evaluate.  Walking them generically would treat sym as
     * a function call head if it has any arguments. */
    if (head == SYM_SYMBOL_MACROLET) {
        CL_Obj body;
        if (!CL_CONS_P(rest)) return;
        body = cl_cdr(rest);
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

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

    /* (let/let* ((var value)...) body...)
     * Must NOT scan binding clauses as macro calls — the var name in
     * (check-type (unless ...)) would be mistaken for a macro call
     * when the var name shadows a registered macro (e.g. log4cl's use of
     * CHECK-TYPE as a let* variable shadowing the standard macro). */
    if (head == SYM_LET || head == SYM_LETSTAR) {
        CL_Obj bindings, body;
        if (!CL_CONS_P(rest)) return;
        bindings = cl_car(rest);
        body = cl_cdr(rest);
        /* Scan value forms only (skip var names) */
        while (CL_CONS_P(bindings)) {
            CL_Obj clause = cl_car(bindings);
            if (CL_CONS_P(clause) && CL_CONS_P(cl_cdr(clause))) {
                /* (var value-form) — scan value-form */
                scan_body_for_boxing(cl_car(cl_cdr(clause)), vars, n_vars,
                                     mutated, captured, closure_depth);
            }
            /* (var) with no value-form, or bare symbol: nothing to scan */
            bindings = cl_cdr(bindings);
        }
        /* Scan body */
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

    /* (destructuring-bind pattern value-form body...)
     * The pattern is a lambda-list-like structure, NOT a form to evaluate.
     * Without this handler the general-form walk would recursively scan the
     * pattern and try to macroexpand any cons whose head collides with a
     * registered macro (e.g. fiveam's TEST vs a `(test)` pattern), which
     * can invoke the expander with wrong arity and crash the VM. */
    if (head == SYM_DESTRUCTURING_BIND) {
        CL_Obj body;
        if (!CL_CONS_P(rest) || !CL_CONS_P(cl_cdr(rest))) return;
        /* Skip pattern (first element of rest) — structural, not evaluated */
        scan_body_for_boxing(cl_car(cl_cdr(rest)), vars, n_vars,
                             mutated, captured, closure_depth);
        body = cl_cdr(cl_cdr(rest));
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

    /* (multiple-value-bind (var...) values-form body...)
     * Same reasoning as let/let*: the var list is not a macro call. */
    if (head == SYM_MULTIPLE_VALUE_BIND) {
        CL_Obj body;
        if (!CL_CONS_P(rest) || !CL_CONS_P(cl_cdr(rest))) return;
        /* Scan values-form (second element of rest) */
        scan_body_for_boxing(cl_car(cl_cdr(rest)), vars, n_vars,
                             mutated, captured, closure_depth);
        /* Scan body */
        body = cl_cdr(cl_cdr(rest));
        while (CL_CONS_P(body)) {
            scan_body_for_boxing(cl_car(body), vars, n_vars,
                                 mutated, captured, closure_depth);
            body = cl_cdr(body);
        }
        return;
    }

    /* (do/do* ((var init [step])...) (end-test result...) body...)
     * Must NOT scan binding clauses as macro calls — the var name in
     * (TUPLE TUPLE (CDR TUPLE)) would be mistaken for a macro call
     * when the var name has a macro definition (e.g. FSet's TUPLE macro). */
    if (head == SYM_DO || head == SYM_DO_STAR) {
        CL_Obj var_clauses, end_clause, body;
        if (!CL_CONS_P(rest)) return;
        var_clauses = cl_car(rest);
        if (!CL_CONS_P(cl_cdr(rest))) return;
        end_clause = cl_car(cl_cdr(rest));
        body = cl_cdr(cl_cdr(rest));
        /* Scan init and step forms (skip var names) */
        while (CL_CONS_P(var_clauses)) {
            CL_Obj clause = cl_car(var_clauses);
            if (CL_CONS_P(clause) && CL_CONS_P(cl_cdr(clause))) {
                /* init form */
                scan_body_for_boxing(cl_car(cl_cdr(clause)), vars, n_vars,
                                     mutated, captured, closure_depth);
                /* step form (if present) */
                if (CL_CONS_P(cl_cdr(cl_cdr(clause))))
                    scan_body_for_boxing(cl_car(cl_cdr(cl_cdr(clause))), vars, n_vars,
                                         mutated, captured, closure_depth);
            }
            var_clauses = cl_cdr(var_clauses);
        }
        /* Scan end-test and result forms */
        if (CL_CONS_P(end_clause)) {
            scan_body_for_boxing(cl_car(end_clause), vars, n_vars,
                                 mutated, captured, closure_depth);
            CL_Obj res = cl_cdr(end_clause);
            while (CL_CONS_P(res)) {
                scan_body_for_boxing(cl_car(res), vars, n_vars,
                                     mutated, captured, closure_depth);
                res = cl_cdr(res);
            }
        }
        /* Scan body */
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
    if (CL_SYMBOL_P(head) && cl_macro_p(head) &&
        scan_macro_depth < SCAN_MACRO_MAX_DEPTH) {
        int saved_sp = cl_vm.sp;
        int saved_fp = cl_vm.fp;
        int saved_dyn = cl_dyn_top;
        int saved_nlx = cl_nlx_top;
        int saved_handler = cl_handler_top;
        int saved_restart = cl_restart_top;
        int saved_debugger = cl_debugger_enabled;
        int saved_gc_roots = gc_root_count;
        cl_debugger_enabled = 0;  /* Suppress debugger during expansion */
#ifdef DEBUG_SCANNER
        fprintf(stderr, "[scanner] expanding macro: %s\n", cl_symbol_name(head));
#endif
        scan_macro_depth++;
        {
        int err; CL_CATCH(err);
        if (err == 0) {
            CL_Obj expanded;
            CL_Obj scan_env = CL_NIL;
            if (cl_active_compiler)
                scan_env = cl_build_lex_env(cl_active_compiler->env);
            expanded = cl_macroexpand_1_env(form, scan_env);
            CL_UNCATCH();
            cl_debugger_enabled = saved_debugger;
            cl_handler_top = saved_handler;
            cl_restart_top = saved_restart;
            if (expanded != form) {
                CL_GC_PROTECT(expanded);
                scan_body_for_boxing(expanded, vars, n_vars,
                                     mutated, captured, closure_depth);
                CL_GC_UNPROTECT(1);
                scan_macro_depth--;
                return;
            }
        } else {
#ifdef DEBUG_SCANNER
            fprintf(stderr, "[scanner-err] macro %s expansion failed: %s\n",
                    cl_symbol_name(head), cl_error_msg);
            fflush(stderr);
#endif
            CL_UNCATCH();
            cl_vm.sp = saved_sp;
            cl_vm.fp = saved_fp;
            cl_dynbind_restore_to(saved_dyn);
            cl_nlx_top = saved_nlx;
            cl_handler_top = saved_handler;
            cl_restart_top = saved_restart;
            cl_debugger_enabled = saved_debugger;
            gc_root_count = saved_gc_roots;
        }
        }
        scan_macro_depth--;
    }

    /* General form: scan all sub-expressions (iterative list walk) */
    {
        CL_Obj cur = form;
        scan_recurse_depth++;
        while (CL_CONS_P(cur)) {
            CL_Obj elem = cl_car(cur);
            cur = cl_cdr(cur);
            if (CL_NULL_P(cur)) {
                /* Last element: tail-call via goto to save stack */
                scan_recurse_depth--;
                form = elem;
                goto top;
            }
            scan_body_for_boxing(elem, vars, n_vars,
                                 mutated, captured, closure_depth);
        }
        scan_recurse_depth--;
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
    uint8_t mutated[CL_MAX_BINDINGS];
    uint8_t captured[CL_MAX_BINDINGS];
    int i;

    if (n_vars > CL_MAX_BINDINGS) n_vars = CL_MAX_BINDINGS;
    memset(mutated, 0, (size_t)n_vars);
    memset(captured, 0, (size_t)n_vars);
    memset(boxed_out, 0, (size_t)n_vars);

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

/* Prelude for (let ...) / (let* ...).  Does all the binding setup and
 * compiles every non-tail body form, then pushes a CL_TAIL_LET frame
 * carrying the postlude state so compile_expr's drain can emit OP_DYNUNBIND
 * and restore env->local_count.  Returns the body's last form so the
 * trampoline can continue with it (no extra C frame), or CL_NIL when the
 * body is empty (caller emits OP_NIL).
 *
 * Splitting compile_let into prelude+postlude is what lets a chain of
 * nested LETs compile in a flat C stack: each let-prelude runs in a
 * transient C frame, returns, and the trampoline loops back to dispatch
 * the next form — instead of a fresh compile_let frame for each level. */
static CL_Obj compile_let(CL_Compiler *c, CL_Obj form, int sequential)
{
    CL_Obj bindings = cl_car(cl_cdr(form));
    CL_Obj body = cl_cdr(cl_cdr(form));
    CL_CompEnv *env = c->env;
    int saved_local_count = env->local_count;
    int saved_tail = c->in_tail;
    int special_count = 0;

    /* GC-protect form components that survive across allocating calls */
    CL_GC_PROTECT(bindings);
    CL_GC_PROTECT(body);

    /* Pre-scan body for (declare (special ...)) to find locally-special vars */
    CL_Obj local_specials = scan_local_specials(body);

    if (sequential) {
        /* Pre-scan all bindings + body for boxing analysis */
        CL_Obj all_vars[CL_MAX_BINDINGS];
        uint8_t needs_boxing[CL_MAX_BINDINGS];
        int n_all = 0;
        {
            CL_Obj b = bindings;
            while (!CL_NULL_P(b) && n_all < CL_MAX_BINDINGS) {
                CL_Obj binding = cl_car(b);
                all_vars[n_all++] = CL_CONS_P(binding) ? cl_car(binding) : binding;
                b = cl_cdr(b);
            }
        }
        {
            uint8_t mutated[CL_MAX_BINDINGS], captured[CL_MAX_BINDINGS];
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
        CL_Obj vars[CL_MAX_BINDINGS];
        CL_Obj vals[CL_MAX_BINDINGS];
        int n = 0, i;
        CL_Obj b = bindings;

        while (!CL_NULL_P(b) && n < CL_MAX_BINDINGS) {
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
            int lexical_slots[CL_MAX_BINDINGS];
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
                CL_Obj lex_vars[CL_MAX_BINDINGS];
                int lex_slots[CL_MAX_BINDINGS];
                int n_lex = 0;
                uint8_t needs_boxing[CL_MAX_BINDINGS];

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

    /* Compile non-tail body forms here; return the last form for the
     * trampoline to take over.  Mirrors compile_body→compile_progn but
     * splits off the last form. */
    {
        CL_Obj rest = process_body_declarations(c, body);
        CL_Obj tail;
        CL_TailFrame *tf;

        if (CL_NULL_P(rest)) {
            tail = CL_NIL;  /* empty body — caller emits OP_NIL */
        } else {
            while (!CL_NULL_P(cl_cdr(rest))) {
                int prev_tail = c->in_tail;
                c->in_tail = 0;
                compile_expr(c, cl_car(rest));
                c->in_tail = prev_tail;
                cl_emit(c, OP_POP);
                rest = cl_cdr(rest);
            }
            tail = cl_car(rest);
        }

        CL_GC_UNPROTECT(2);  /* bindings, body */

        tf = cl_tail_push(c);
        tf->kind = CL_TAIL_LET;
        tf->saved_local_count = saved_local_count;
        tf->special_count = special_count;
        tf->saved_tail = saved_tail;
        tf->n_gc_roots = 0;
        return tail;
    }
}

/* Emit the deferred postlude for a popped tail frame.  Called from
 * compile_expr's drain loop in LIFO order.  Returns a "continuation
 * form" the driver should dispatch next, or CL_NIL if no further work
 * is needed.  Most postludes return CL_NIL; only IF_AFTER_THEN uses
 * the continuation channel (to dispatch the ELSE form after THEN has
 * compiled). */
static CL_Obj emit_tail_postlude(CL_Compiler *c, CL_TailFrame *tf)
{
    switch ((CL_TailKind)tf->kind) {
    case CL_TAIL_LET:
        c->special_depth -= tf->special_count;
        if (tf->special_count > 0) {
            cl_emit(c, OP_DYNUNBIND);
            cl_emit(c, (uint8_t)tf->special_count);
        }
        cl_env_clear_boxed(c->env, tf->saved_local_count);
        c->env->local_count = tf->saved_local_count;
        return CL_NIL;
    case CL_TAIL_PROGN:
    case CL_TAIL_LOCALLY:
        /* No postlude — pure body wrapper. */
        return CL_NIL;
    case CL_TAIL_SYMBOL_MACROLET:
        c->env->symbol_macro_count = tf->saved_macro_count;
        return CL_NIL;
    case CL_TAIL_MACROLET:
        c->env->local_macro_count = tf->saved_macro_count;
        return CL_NIL;
    case CL_TAIL_PROGV:
        cl_emit(c, OP_PROGV_UNBIND);
        c->in_tail = tf->saved_tail;
        return CL_NIL;
    case CL_TAIL_IF_AFTER_THEN: {
        /* THEN's bytecode is fully emitted.  We now need to JMP past
         * the ELSE branch, patch the JNIL landing, and either dispatch
         * the ELSE form (if there is one) or emit OP_NIL inline. */
        int jmp_pos = cl_emit_jump(c, OP_JMP);
        cl_patch_jump(c, tf->block_push_pos);  /* jnil_pos: JNIL → here */
        if (CL_NULL_P(tf->cont_form)) {
            /* No ELSE (or `(if t b nil)` collapsed): emit NIL inline
             * and patch the JMP immediately. */
            cl_emit(c, OP_NIL);
            cl_patch_jump(c, jmp_pos);
            return CL_NIL;
        } else {
            /* Push AFTER_ELSE frame to patch jmp_pos once ELSE drains,
             * and return the ELSE form so the driver trampolines into
             * it without growing the C stack. */
            CL_TailFrame *new_tf = cl_tail_push(c);
            new_tf->kind = CL_TAIL_IF_AFTER_ELSE;
            new_tf->block_push_pos = jmp_pos;
            new_tf->saved_tail = tf->saved_tail;
            c->in_tail = tf->saved_tail;
            return tf->cont_form;
        }
    }
    case CL_TAIL_IF_AFTER_ELSE:
        cl_patch_jump(c, tf->block_push_pos);  /* jmp_pos: JMP past ELSE → here */
        return CL_NIL;
    case CL_TAIL_HANDLER_BIND:
        cl_emit(c, OP_HANDLER_POP);
        cl_emit(c, (uint8_t)tf->saved_macro_count); /* count of handlers */
        return CL_NIL;
    case CL_TAIL_MULTIPLE_VALUE_BIND:
        cl_env_clear_boxed(c->env, tf->saved_local_count);
        c->env->local_count = tf->saved_local_count;
        c->in_tail = tf->saved_tail;
        return CL_NIL;
    case CL_TAIL_FLET:
    case CL_TAIL_LABELS:
        cl_env_clear_boxed(c->env, tf->saved_local_count);
        c->env->local_count = tf->saved_local_count;
        c->env->local_fun_count = tf->saved_block_count;  /* reused: saved_local_fun_count */
        c->in_tail = tf->saved_tail;
        return CL_NIL;
    case CL_TAIL_EVAL_WHEN:
        /* No postlude — pure body wrapper. */
        return CL_NIL;
    case CL_TAIL_PROGN_ITER: {
        /* Non-tail body form just drained; emit OP_POP and continue
         * with the next form in cont_form.
         *
         * GOTCHA: the drain loop in compile_expr uses CL_NIL as the
         * "no continuation" sentinel.  But CL_NIL is also a perfectly
         * valid body form: iter expands BLOCK bodies to
         * `((TAGBODY ...) NIL)`, where the final NIL is the body's
         * tail value.  Returning CL_NIL here would make the drain
         * loop *skip* dispatching it — leaving no value on the stack
         * for the surrounding BLOCK_LOCAL postlude to peek-store,
         * which then captures whatever was below (e.g. a function
         * loaded for the enclosing CALL) and corrupts the caller's
         * stack frame.  Workaround: handle literal-NIL body forms
         * inline by emitting OP_NIL + (OP_POP for non-last) directly
         * here, so the drain never sees CL_NIL as a continuation.  We
         * loop, advancing past consecutive NIL non-last forms. */
        CL_Obj forms = tf->cont_form;
        cl_emit(c, OP_POP);  /* discard the just-drained form's value */

        /* Skip over leading NIL non-last forms inline. */
        while (!CL_NULL_P(cl_cdr(forms)) && CL_NULL_P(cl_car(forms))) {
            cl_emit(c, OP_NIL);
            cl_emit(c, OP_POP);
            forms = cl_cdr(forms);
        }

        if (CL_NULL_P(cl_cdr(forms))) {
            /* Single remaining form (the body tail). */
            CL_Obj last = cl_car(forms);
            c->in_tail = tf->saved_tail;
            if (CL_NULL_P(last)) {
                cl_emit(c, OP_NIL);
                return CL_NIL;
            }
            return last;
        }
        /* More forms remain; first form is non-NIL (we skipped any leading
         * NILs above).  Push a fresh PROGN_ITER frame. */
        {
            CL_TailFrame *new_tf = cl_tail_push(c);
            new_tf->kind = CL_TAIL_PROGN_ITER;
            new_tf->saved_tail = tf->saved_tail;
            new_tf->cont_form = cl_cdr(forms);
            c->in_tail = 0;
            return cl_car(forms);
        }
    }
    case CL_TAIL_BLOCK_LOCAL:
    case CL_TAIL_BLOCK_NLX:
    case CL_TAIL_RETURN_FROM_LOCAL:
    case CL_TAIL_RETURN_FROM_NLX:
    case CL_TAIL_OUTER_RETURN_FROM:
        emit_block_or_return_postlude(c, tf);
        return CL_NIL;
    }
    return CL_NIL;
}

/* --- Setq / Setf --- */

static void compile_setf_place(CL_Compiler *c, CL_Obj place, CL_Obj val_form);

static void compile_setq(CL_Compiler *c, CL_Obj form)
{
    CL_Obj rest = cl_cdr(form);

    CL_GC_PROTECT(rest);
    while (!CL_NULL_P(rest)) {
        CL_Obj var = cl_car(rest);
        CL_Obj val = cl_car(cl_cdr(rest));
        int slot;
        int saved_tail = c->in_tail;

        /* Check if var is a symbol macro — rewrite as (setf expansion val) */
        if (CL_SYMBOL_P(var)) {
            CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, var);
            if (CL_NULL_P(expansion))
                expansion = cl_lookup_global_symbol_macro(var);
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
    CL_GC_UNPROTECT(1);
}

/* Check if sym is a composite c[ad]+r accessor (caar, cadr, cdar, cddr, etc.)
 * Returns 1 if so, 0 otherwise. */
/* SECOND..TENTH → NTH index mapping for setf */
static int is_nth_accessor(CL_Obj sym)
{
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    CL_String *name = (CL_String *)CL_OBJ_TO_PTR(s->name);
    if (s->package != cl_package_cl) return 0;
    static const char *names[] = {
        "SECOND", "THIRD", "FOURTH", "FIFTH", "SIXTH",
        "SEVENTH", "EIGHTH", "NINTH", "TENTH", NULL
    };
    int i;
    for (i = 0; names[i]; i++) {
        if (name->length == strlen(names[i]) &&
            memcmp(name->data, names[i], name->length) == 0)
            return 1;
    }
    return 0;
}

static int get_nth_index(CL_Obj sym)
{
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    CL_String *name = (CL_String *)CL_OBJ_TO_PTR(s->name);
    static const struct { const char *n; int idx; } map[] = {
        {"SECOND",1}, {"THIRD",2}, {"FOURTH",3}, {"FIFTH",4}, {"SIXTH",5},
        {"SEVENTH",6}, {"EIGHTH",7}, {"NINTH",8}, {"TENTH",9}, {NULL,0}
    };
    int i;
    for (i = 0; map[i].n; i++) {
        if (name->length == strlen(map[i].n) &&
            memcmp(name->data, map[i].n, name->length) == 0)
            return map[i].idx;
    }
    return -1;
}

static int is_composite_cadr(CL_Obj sym)
{
    CL_Symbol *s;
    CL_String *name;
    uint32_t k;
    if (!CL_SYMBOL_P(sym)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    name = (CL_String *)CL_OBJ_TO_PTR(s->name);
    if (name->length < 4 || name->length > 6) return 0;
    if (name->data[0] != 'C' || name->data[name->length - 1] != 'R') return 0;
    for (k = 1; k < name->length - 1; k++) {
        if (name->data[k] != 'A' && name->data[k] != 'D') return 0;
    }
    return 1;
}

static void compile_setf_place(CL_Compiler *c, CL_Obj place, CL_Obj val_form)
{
    int saved_tail = c->in_tail;
    c->in_tail = 0;

    if (CL_SYMBOL_P(place)) {
        int slot;
        /* Check if place is a symbol macro — rewrite as (setf expansion val) */
        CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, place);
        if (CL_NULL_P(expansion))
            expansion = cl_lookup_global_symbol_macro(place);
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

        /* Per CLHS 5.1.2.9: if a setf expander exists for the head,
         * use it even if head is also a macro.  Only macro-expand if
         * no setf expander is found. */
        if (CL_SYMBOL_P(head)) {
            /* Check define-setf-expander table FIRST (takes precedence over macros) */
            {
                CL_Obj expander_fn = CL_NIL;
                CL_Obj exp_entry;
                cl_tables_rdlock();
                exp_entry = setf_expander_table;
                while (!CL_NULL_P(exp_entry)) {
                    CL_Obj pair = cl_car(exp_entry);
                    if (cl_car(pair) == head) {
                        expander_fn = cl_cdr(pair);
                        break;
                    }
                    exp_entry = cl_cdr(exp_entry);
                }
                cl_tables_rwunlock();
                if (!CL_NULL_P(expander_fn)) {
                    CL_Obj call_args[2];
                    CL_Obj expansion;
                    call_args[0] = place;
                    call_args[1] = val_form;
                    CL_GC_PROTECT(place);
                    CL_GC_PROTECT(val_form);
                    expansion = cl_vm_apply(expander_fn, call_args, 2);
                    CL_GC_UNPROTECT(2);
                    compile_expr(c, expansion);
                    c->in_tail = saved_tail;
                    return;
                }
            }
            /* No setf expander — try local macros */
            {
                CL_Obj local_exp = cl_env_lookup_local_macro(c->env, head);
                if (!CL_NULL_P(local_exp)) {
                    /* Local macrolet expander is a 2-arg (form env) wrapper
                     * (see compile_macrolet); see also bi_call_macro_expander. */
                    CL_Obj call_args[2];
                    CL_Obj lex_env = cl_build_lex_env(c->env);
                    call_args[0] = place;
                    call_args[1] = lex_env;
                    CL_GC_PROTECT(lex_env);
                    {
                        CL_Obj expanded;
                        expanded = cl_vm_apply(local_exp, call_args, 2);
                        CL_GC_UNPROTECT(1);
                        compile_setf_place(c, expanded, val_form);
                        c->in_tail = saved_tail;
                        return;
                    }
                }
            }
            /* No setf expander, no local macro — try global macros */
            if (!CL_NULL_P(cl_get_macro(head))) {
                CL_Obj lex_env = cl_build_lex_env(c->env);
                CL_Obj expanded;
                CL_GC_PROTECT(lex_env);
                expanded = cl_macroexpand_1_env(place, lex_env);
                CL_GC_UNPROTECT(1);
                if (expanded != place) {
                    compile_setf_place(c, expanded, val_form);
                    c->in_tail = saved_tail;
                    return;
                }
            }
        }

        /* (setf (values p1 p2 ...) val-form) →
         * (multiple-value-bind (t1 t2 ...) val-form (setf p1 t1) (setf p2 t2) ... t1) */
        if (head == cl_intern_in("VALUES", 6, cl_package_cl)) {
            CL_Obj places = cl_cdr(place);
            CL_Obj tmps = CL_NIL;
            CL_Obj setfs = CL_NIL;
            CL_Obj mvb_form, sym_mvb;
            int n = 0;
            CL_Obj p;

            CL_GC_PROTECT(tmps);
            CL_GC_PROTECT(setfs);

            /* Build list of gensyms and setf forms (in reverse) */
            p = places;
            while (CL_CONS_P(p)) {
                char buf[16];
                CL_Obj tmp, name_str;
                int len;
                len = snprintf(buf, sizeof(buf), "%%MV%d", n);
                name_str = cl_make_string(buf, (uint32_t)len);
                tmp = cl_make_symbol(name_str);
                tmps = cl_cons(tmp, tmps);
                /* Build (setf place-i tmp-i) */
                setfs = cl_cons(
                    cl_cons(cl_intern_in("SETF", 4, cl_package_cl),
                            cl_cons(cl_car(p), cl_cons(tmp, CL_NIL))),
                    setfs);
                n++;
                p = cl_cdr(p);
            }

            /* Reverse tmps and setfs */
            {
                CL_Obj rev_tmps = CL_NIL, rev_setfs = CL_NIL;
                CL_GC_PROTECT(rev_tmps);
                CL_GC_PROTECT(rev_setfs);
                while (!CL_NULL_P(tmps)) {
                    rev_tmps = cl_cons(cl_car(tmps), rev_tmps);
                    tmps = cl_cdr(tmps);
                }
                while (!CL_NULL_P(setfs)) {
                    rev_setfs = cl_cons(cl_car(setfs), rev_setfs);
                    setfs = cl_cdr(setfs);
                }
                tmps = rev_tmps;
                setfs = rev_setfs;
                CL_GC_UNPROTECT(2);
            }

            /* Build: (multiple-value-bind tmps val-form (setf p1 t1) ... (setf pn tn) t1) */
            sym_mvb = cl_intern_in("MULTIPLE-VALUE-BIND", 19, cl_package_cl);
            /* Append first tmp to end of setfs for return value */
            {
                CL_Obj body = cl_cons(cl_car(tmps), CL_NIL); /* (t1) */
                CL_Obj s = setfs;
                /* Reverse setfs to prepend to body */
                CL_Obj rev_s = CL_NIL;
                CL_GC_PROTECT(body);
                CL_GC_PROTECT(rev_s);
                while (!CL_NULL_P(s)) {
                    rev_s = cl_cons(cl_car(s), rev_s);
                    s = cl_cdr(s);
                }
                while (!CL_NULL_P(rev_s)) {
                    body = cl_cons(cl_car(rev_s), body);
                    rev_s = cl_cdr(rev_s);
                }
                mvb_form = cl_cons(sym_mvb, cl_cons(tmps, cl_cons(val_form, body)));
                CL_GC_UNPROTECT(2);
            }

            CL_GC_UNPROTECT(2);
            compile_expr(c, mvb_form);
            c->in_tail = saved_tail;
            return;
        }

        /* (setf (progn form... place) val) → (progn form... (setf place val))
         * Per CLHS 5.1.2.3, PROGN is transparent to setf. */
        if (head == SYM_PROGN) {
            CL_Obj forms = cl_cdr(place);
            CL_Obj inner_place;
            /* Walk to last form (the actual place), compile leading forms */
            while (CL_CONS_P(forms) && CL_CONS_P(cl_cdr(forms))) {
                compile_expr(c, cl_car(forms));
                cl_emit(c, OP_POP);
                forms = cl_cdr(forms);
            }
            /* Last element is the place */
            inner_place = CL_CONS_P(forms) ? cl_car(forms) : CL_NIL;
            compile_setf_place(c, inner_place, val_form);
            c->in_tail = saved_tail;
            return;
        }

        /* (setf (if test then-place else-place) val)
         * → (if test (setf then-place val) (setf else-place val))
         * Extension supported by SBCL/CCL; required by FSet. */
        if (head == SYM_IF) {
            CL_Obj test_form = cl_car(cl_cdr(place));
            CL_Obj then_place = cl_car(cl_cdr(cl_cdr(place)));
            CL_Obj else_place = cl_car(cl_cdr(cl_cdr(cl_cdr(place))));
            CL_Obj setf_then, setf_else, new_if;

            CL_GC_PROTECT(test_form);
            CL_GC_PROTECT(then_place);
            CL_GC_PROTECT(else_place);
            CL_GC_PROTECT(val_form);

            setf_then = cl_cons(SYM_SETF,
                          cl_cons(then_place,
                            cl_cons(val_form, CL_NIL)));
            setf_else = CL_NULL_P(else_place) ? CL_NIL
                        : cl_cons(SYM_SETF,
                            cl_cons(else_place,
                              cl_cons(val_form, CL_NIL)));
            new_if = cl_cons(SYM_IF,
                       cl_cons(test_form,
                         cl_cons(setf_then,
                           CL_NULL_P(else_place) ? CL_NIL
                           : cl_cons(setf_else, CL_NIL))));

            CL_GC_UNPROTECT(4);
            compile_expr(c, new_if);
            c->in_tail = saved_tail;
            return;
        }

        /* (setf (let/let* bindings body... place) val)
         * → (let/let* bindings body... (setf place val))
         * Extension supported by SBCL/CCL; required by FSet. */
        if (head == SYM_LET || head == SYM_LETSTAR) {
            CL_Obj bindings = cl_car(cl_cdr(place));
            CL_Obj body = cl_cdr(cl_cdr(place));
            CL_Obj inner_place, new_body, setf_form, new_let;
            CL_Obj rev = CL_NIL;

            CL_GC_PROTECT(bindings);
            CL_GC_PROTECT(body);
            CL_GC_PROTECT(val_form);
            CL_GC_PROTECT(rev);

            /* Find last body form (the place), collect preceding in reverse */
            while (CL_CONS_P(body) && CL_CONS_P(cl_cdr(body))) {
                rev = cl_cons(cl_car(body), rev);
                body = cl_cdr(body);
            }
            inner_place = CL_CONS_P(body) ? cl_car(body) : CL_NIL;

            /* Build (setf inner-place val) */
            setf_form = cl_cons(SYM_SETF,
                          cl_cons(inner_place,
                            cl_cons(val_form, CL_NIL)));

            /* Build new body: preceding-forms... (setf place val) */
            new_body = cl_cons(setf_form, CL_NIL);
            CL_GC_PROTECT(new_body);
            while (!CL_NULL_P(rev)) {
                new_body = cl_cons(cl_car(rev), new_body);
                rev = cl_cdr(rev);
            }

            /* Build (let/let* bindings new-body...) */
            new_let = cl_cons(head, cl_cons(bindings, new_body));

            CL_GC_UNPROTECT(5);
            compile_expr(c, new_let);
            c->in_tail = saved_tail;
            return;
        }

        /* (setf (the type place) val) → (setf place (the type val))
         * Per CL spec, THE is transparent to setf. */
        if (head == SYM_THE) {
            CL_Obj type_spec = cl_car(cl_cdr(place));
            CL_Obj inner_place = cl_car(cl_cdr(cl_cdr(place)));
            CL_Obj typed_val;
            CL_GC_PROTECT(type_spec);
            CL_GC_PROTECT(inner_place);
            CL_GC_PROTECT(val_form);
            /* Build (the type val_form) */
            typed_val = cl_cons(SYM_THE,
                          cl_cons(type_spec,
                            cl_cons(val_form, CL_NIL)));
            CL_GC_UNPROTECT(3);
            compile_setf_place(c, inner_place, typed_val);
            c->in_tail = saved_tail;
            return;
        }

        if (head == SETF_SYM_CAR || head == SETF_SYM_FIRST) {
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_RPLACA);
        } else if (head == SETF_SYM_CDR || head == SETF_SYM_REST) {
            compile_expr(c, cl_car(cl_cdr(place)));
            compile_expr(c, val_form);
            cl_emit(c, OP_RPLACD);
        } else if (CL_SYMBOL_P(head) && is_nth_accessor(head)) {
            /* SECOND..TENTH: rewrite as (setf (nth N list) val) */
            int nth_idx = get_nth_index(head);
            CL_Obj arg = cl_car(cl_cdr(place));
            CL_Obj nth_place;
            CL_GC_PROTECT(arg);
            nth_place = cl_cons(SETF_SYM_NTH,
                          cl_cons(CL_MAKE_FIXNUM(nth_idx),
                            cl_cons(arg, CL_NIL)));
            CL_GC_UNPROTECT(1);
            compile_setf_place(c, nth_place, val_form);
            c->in_tail = saved_tail;
            return;
        } else if (is_composite_cadr(head)) {
            /* Composite c[ad]+r: decompose (setf (cXY..Zr arg) val)
             * into (setf (cXr (cY..Zr arg)) val) per CL spec */
            CL_Symbol *hsym = (CL_Symbol *)CL_OBJ_TO_PTR(head);
            CL_String *hname = (CL_String *)CL_OBJ_TO_PTR(hsym->name);
            CL_Obj outer_sym = (hname->data[1] == 'A') ? SETF_SYM_CAR : SETF_SYM_CDR;
            CL_Obj arg = cl_car(cl_cdr(place));
            char ibuf[32];
            uint32_t ilen = hname->length - 1, ki;
            CL_Obj isym, iplace, nplace;
            if (ilen >= sizeof(ibuf)) ilen = sizeof(ibuf) - 1;
            ibuf[0] = 'C';
            for (ki = 0; ki < ilen - 2; ki++)
                ibuf[ki + 1] = hname->data[ki + 2];
            ibuf[ilen - 1] = 'R';
            ibuf[ilen] = '\0';
            isym = cl_intern_in(ibuf, ilen, cl_package_cl);
            CL_GC_PROTECT(outer_sym);
            CL_GC_PROTECT(arg);
            iplace = cl_cons(isym, cl_cons(arg, CL_NIL));
            nplace = cl_cons(outer_sym, cl_cons(iplace, CL_NIL));
            CL_GC_UNPROTECT(2);
            compile_setf_place(c, nplace, val_form);
            c->in_tail = saved_tail;
            return;
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
                CL_GC_PROTECT(tmp);
                while (!CL_NULL_P(tmp)) {
                    compile_expr(c, cl_car(tmp));
                    tmp = cl_cdr(tmp);
                }
                CL_GC_UNPROTECT(1);
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
        } else if (head == SETF_SYM_SYMBOL_PLIST) {
            int idx = cl_add_constant(c, SETF_HELPER_SP);
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
            /* (setf (get sym indicator [default]) val) → (%setf-get sym indicator val).
             * Per CLHS §5.1.1.1.1, every subform of the place — including
             * DEFAULT — must be evaluated once, left-to-right, before VAL,
             * even though %setf-get itself doesn't use DEFAULT.  Emit code
             * for DEFAULT into the value stack, then OP_POP it. */
            int idx = cl_add_constant(c, SETF_HELPER_GET);
            CL_Obj sym_form  = cl_car(cl_cdr(place));
            CL_Obj ind_form  = cl_car(cl_cdr(cl_cdr(place)));
            CL_Obj rest_after_ind = cl_cdr(cl_cdr(cl_cdr(place)));
            int has_default = !CL_NULL_P(rest_after_ind);
            cl_emit(c, OP_FLOAD);
            cl_emit_u16(c, (uint16_t)idx);
            compile_expr(c, sym_form);                     /* symbol */
            compile_expr(c, ind_form);                     /* indicator */
            if (has_default) {
                /* Evaluate DEFAULT for side effects, then discard. */
                compile_expr(c, cl_car(rest_after_ind));
                cl_emit(c, OP_POP);
            }
            compile_expr(c, val_form);                     /* value */
            cl_emit(c, OP_CALL);
            cl_emit(c, 3);
        } else if (head == SETF_SYM_GETF) {
            /* (setf (getf PLACE IND [DEFAULT]) VAL).
             *
             * CLHS §5.1.2.4: if IND is missing from PLACE's plist, a new
             * (IND VAL) pair must be PREPENDED to PLACE — so PLACE itself
             * must be reassigned, not just mutated.  CLHS §5.1.1.1.1
             * additionally requires every subform of PLACE — that is, the
             * PLACE form, IND, and DEFAULT — to be evaluated once,
             * left-to-right, before VAL.  Per CLHS, DEFAULT may be
             * evaluated when used in a setf context even though its value
             * is ignored; the ansi-test suite (setf-getf.5) relies on this.
             *
             * We rewrite to
             *   (let* (T-subforms-of-PLACE...
             *          (#:i IND)
             *          (#:d DEFAULT)   ; only if DEFAULT supplied
             *          (#:v VAL))
             *     (setf PLACE-using-T-temps
             *           (%setf-getf PLACE-using-T-temps #:i #:v))
             *     #:v)
             * and recompile, letting nested SETF machinery handle whatever
             * kind of inner place we end up with.
             *
             * For a symbol PLACE there are no subform temps; for a CONS
             * place (e.g. (CAR X)) we bind each argument of the place to a
             * gensym and reuse them in both the SETF target and the helper
             * call so the place is read only once. */
            CL_Obj place_arg  = cl_car(cl_cdr(place));          /* PLACE */
            CL_Obj ind_arg    = cl_car(cl_cdr(cl_cdr(place)));  /* IND   */
            CL_Obj rest3      = cl_cdr(cl_cdr(cl_cdr(place)));  /* (DEFAULT) or () */
            int has_default   = !CL_NULL_P(rest3);
            CL_Obj default_arg = has_default ? cl_car(rest3) : CL_NIL;
            CL_Obj gensym_i  = cl_gensym_with_name("I");
            CL_Obj gensym_d  = has_default ? cl_gensym_with_name("D") : CL_NIL;
            CL_Obj gensym_v  = cl_gensym_with_name("V");
            CL_Obj inner_place;            /* PLACE form rewritten with temps */
            CL_Obj sub_bindings = CL_NIL;  /* temps from PLACE's subforms (if cons) */
            CL_Obj helper_call, setf_place_form, let_bindings, let_form;

            CL_GC_PROTECT(place_arg);
            CL_GC_PROTECT(ind_arg);
            CL_GC_PROTECT(default_arg);
            CL_GC_PROTECT(gensym_i);
            CL_GC_PROTECT(gensym_d);
            CL_GC_PROTECT(gensym_v);
            CL_GC_PROTECT(sub_bindings);

            /* If PLACE is a CONS, decompose its subforms into temp bindings
             * and rebuild PLACE using those temps so it reads only once. */
            if (CL_CONS_P(place_arg)) {
                CL_Obj op = cl_car(place_arg);
                CL_Obj rest = cl_cdr(place_arg);
                CL_Obj temps_head = CL_NIL, temps_tail = CL_NIL;
                CL_Obj bind_head = CL_NIL, bind_tail = CL_NIL;
                CL_GC_PROTECT(temps_head);
                CL_GC_PROTECT(temps_tail);
                CL_GC_PROTECT(bind_head);
                CL_GC_PROTECT(bind_tail);
                while (!CL_NULL_P(rest)) {
                    CL_Obj sub = cl_car(rest);
                    CL_Obj gs = cl_gensym_with_name("T");
                    CL_Obj cell, binding;
                    /* binding: (gs sub) */
                    binding = cl_cons(sub, CL_NIL);
                    binding = cl_cons(gs, binding);
                    cell = cl_cons(binding, CL_NIL);
                    if (CL_NULL_P(bind_head)) bind_head = cell;
                    else ((CL_Cons *)CL_OBJ_TO_PTR(bind_tail))->cdr = cell;
                    bind_tail = cell;
                    /* temps list */
                    cell = cl_cons(gs, CL_NIL);
                    if (CL_NULL_P(temps_head)) temps_head = cell;
                    else ((CL_Cons *)CL_OBJ_TO_PTR(temps_tail))->cdr = cell;
                    temps_tail = cell;
                    rest = cl_cdr(rest);
                }
                inner_place = cl_cons(op, temps_head);
                sub_bindings = bind_head;
                CL_GC_UNPROTECT(4);
            } else {
                inner_place = place_arg;
            }
            CL_GC_PROTECT(inner_place);

            /* (%setf-getf inner_place #:i #:v) */
            helper_call = cl_cons(gensym_v, CL_NIL);
            CL_GC_PROTECT(helper_call);
            helper_call = cl_cons(gensym_i, helper_call);
            helper_call = cl_cons(inner_place, helper_call);
            helper_call = cl_cons(SETF_HELPER_GETF, helper_call);

            /* (setf inner_place helper_call) */
            setf_place_form = cl_cons(helper_call, CL_NIL);
            CL_GC_PROTECT(setf_place_form);
            setf_place_form = cl_cons(inner_place, setf_place_form);
            setf_place_form = cl_cons(SYM_SETF, setf_place_form);

            /* Build let* bindings:
             *   sub_bindings ++ ((#:i IND) [(#:d DEFAULT)] (#:v VAL))
             */
            {
                CL_Obj b_v, b_i, b_d, head_b, tail_b;
                b_v = cl_cons(val_form, CL_NIL);
                b_v = cl_cons(gensym_v, b_v);
                b_v = cl_cons(b_v, CL_NIL);
                head_b = b_v; tail_b = b_v;
                if (has_default) {
                    b_d = cl_cons(default_arg, CL_NIL);
                    b_d = cl_cons(gensym_d, b_d);
                    b_d = cl_cons(b_d, head_b);
                    head_b = b_d;
                }
                b_i = cl_cons(ind_arg, CL_NIL);
                b_i = cl_cons(gensym_i, b_i);
                b_i = cl_cons(b_i, head_b);
                head_b = b_i;
                /* prepend sub_bindings */
                if (!CL_NULL_P(sub_bindings)) {
                    /* find tail of sub_bindings, link to head_b */
                    CL_Obj walk = sub_bindings, prev = CL_NIL;
                    while (!CL_NULL_P(walk)) { prev = walk; walk = cl_cdr(walk); }
                    ((CL_Cons *)CL_OBJ_TO_PTR(prev))->cdr = head_b;
                    let_bindings = sub_bindings;
                } else {
                    let_bindings = head_b;
                }
                CL_UNUSED(tail_b);
            }
            CL_GC_PROTECT(let_bindings);

            /* (let* let_bindings (setf ...) #:v) */
            let_form = cl_cons(gensym_v, CL_NIL);
            CL_GC_PROTECT(let_form);
            let_form = cl_cons(setf_place_form, let_form);
            let_form = cl_cons(let_bindings, let_form);
            let_form = cl_cons(SYM_LETSTAR, let_form);

            compile_expr(c, let_form);
            CL_GC_UNPROTECT(11);
        } else {
            /* Check defsetf table */
            CL_Obj updater = CL_NIL;
            int found = 0;
            {
                CL_Obj entry;
                cl_tables_rdlock();
                entry = setf_table;
                while (!CL_NULL_P(entry)) {
                    CL_Obj pair = cl_car(entry);
                    if (cl_car(pair) == head) {
                        updater = cl_cdr(pair);
                        found = 1;
                        break;
                    }
                    entry = cl_cdr(entry);
                }
                cl_tables_rwunlock();
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
                /* Check define-setf-expander table.
                 * Expander fn takes (place-form value-form) and returns
                 * a single expansion form to compile. */
                {
                    CL_Obj expander_fn = CL_NIL;
                    CL_Obj exp_entry;
                    cl_tables_rdlock();
                    exp_entry = setf_expander_table;
                    while (!CL_NULL_P(exp_entry)) {
                        CL_Obj pair = cl_car(exp_entry);
                        if (cl_car(pair) == head) {
                            expander_fn = cl_cdr(pair);
                            break;
                        }
                        exp_entry = cl_cdr(exp_entry);
                    }
                    cl_tables_rwunlock();
                    if (!CL_NULL_P(expander_fn)) {
                        CL_Obj call_args[2];
                        CL_Obj expansion;
                        call_args[0] = place;
                        call_args[1] = val_form;
                        CL_GC_PROTECT(place);
                        CL_GC_PROTECT(val_form);
                        expansion = cl_vm_apply(expander_fn, call_args, 2);
                        CL_GC_UNPROTECT(2);
                        compile_expr(c, expansion);
                        return;
                    }
                }
                /* Setf function (late-bound): construct %SETF-<name> symbol,
                 * emit FLOAD — resolved at runtime like normal function calls.
                 * Handles both (defun (setf name) ...) and
                 * (defgeneric (setf name) ...) / (defmethod (setf name) ...).
                 * Check setf_fn_table first; if not found, intern %SETF-<name>. */
                CL_Obj setf_fn = CL_NIL;
                {
                    CL_Obj sfe;
                    cl_tables_rdlock();
                    sfe = setf_fn_table;
                    while (!CL_NULL_P(sfe)) {
                        CL_Obj pair = cl_car(sfe);
                        if (cl_car(pair) == head) {
                            setf_fn = cl_cdr(pair);
                            break;
                        }
                        sfe = cl_cdr(sfe);
                    }
                    cl_tables_rwunlock();
                }
                if (CL_NULL_P(setf_fn)) {
                    /* Optimistic late binding: intern %SETF-<name> in the
                     * CLAMIGA package.  Putting these auto-generated
                     * shadow names in CLAMIGA (instead of the accessor's
                     * home package, as we used to) keeps them out of CL's
                     * external symbol set and out of arbitrary user
                     * packages — matching how (defun (setf foo) ...) and
                     * (defmethod (setf foo) ...) need a stable lookup
                     * location across packages.  A previously-registered
                     * setf-fn binding in setf_fn_table still wins; this
                     * fallback only fires for unregistered names. */
                    CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(head);
                    CL_String *sname = (CL_String *)CL_OBJ_TO_PTR(sym->name);
                    char buf[256];
                    int len = snprintf(buf, sizeof(buf), "%%SETF-%.*s",
                                       (int)sname->length, sname->data);
                    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
                    setf_fn = cl_intern_in(buf, (uint32_t)len, cl_package_clamiga);
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

    CL_GC_PROTECT(rest);
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
    CL_GC_UNPROTECT(1);
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

/* Map a CL: builtin call site to a single-opcode inline form, when the
 * VM has an opcode that matches the function's semantics for this arity.
 * Returns the opcode if (name, nargs) is inlinable, or 0 otherwise.
 *
 * Per CLHS 11.1.2.1.2 conformant programs cannot redefine standard
 * COMMON-LISP symbols, so the inliner can ignore the function cell at
 * runtime — the opcode handler in vm.c is the authoritative meaning.
 *
 * Only the binary 2-arg case is inlined for arithmetic / comparison
 * (`+ - * = < > <= >=`).  CL allows variadic forms, but `(+ a b c)` =
 * `(+ (+ a b) c)` only for associative ops on exact reals — for floats
 * and bignums there are subtle ordering differences with bi_add's
 * single-pass loop, so the safe choice is to defer to bi_add for n != 2.
 * Likewise `(- a)` and `(- a b c)` need different bytecode shapes; only
 * the 2-arg case maps to OP_SUB cleanly.
 *
 * NULL and NOT both map to OP_NOT — semantically `cl:null` ≡ `cl:not`
 * (both return T iff arg is NIL, NIL otherwise). */
static uint8_t inline_builtin_opcode(CL_Obj func, int nargs)
{
    CL_Symbol *s;
    CL_String *name;

    if (!CL_SYMBOL_P(func)) return 0;
    s = (CL_Symbol *)CL_OBJ_TO_PTR(func);
    if (s->package != cl_package_cl) return 0;
    name = (CL_String *)CL_OBJ_TO_PTR(s->name);

    switch (name->length) {
    case 1:
        if (nargs == 2) {
            switch (name->data[0]) {
            case '+': return OP_ADD;
            case '-': return OP_SUB;
            case '*': return OP_MUL;
            case '=': return OP_NUMEQ;
            case '<': return OP_LT;
            case '>': return OP_GT;
            }
        }
        break;
    case 2:
        if (nargs == 2) {
            if (name->data[0] == '<' && name->data[1] == '=') return OP_LE;
            if (name->data[0] == '>' && name->data[1] == '=') return OP_GE;
            if (name->data[0] == 'E' && name->data[1] == 'Q') return OP_EQ;
        }
        break;
    case 3:
        if (nargs == 1) {
            if (memcmp(name->data, "CAR", 3) == 0) return OP_CAR;
            if (memcmp(name->data, "CDR", 3) == 0) return OP_CDR;
            if (memcmp(name->data, "NOT", 3) == 0) return OP_NOT;
        }
        break;
    case 4:
        if (nargs == 1 && memcmp(name->data, "NULL", 4) == 0) return OP_NOT;
        if (nargs == 2 && memcmp(name->data, "CONS", 4) == 0) return OP_CONS;
        break;
    }
    return 0;
}

/* Count proper-list length without allocating.  Caller must have
 * already validated that args is a proper list (or NIL). */
static int proper_list_length(CL_Obj list)
{
    int n = 0;
    while (!CL_NULL_P(list)) {
        list = cl_cdr(list);
        n++;
    }
    return n;
}

static void compile_call(CL_Compiler *c, CL_Obj form)
{
    CL_Obj func = cl_car(form);
    CL_Obj args = cl_cdr(form);
    int nargs = 0;
    int saved_tail = c->in_tail;

    /* Validate form structure — CDR must be a proper list */
    if (!CL_NULL_P(args) && !CL_CONS_P(args)) {
        cl_error(CL_ERR_GENERAL, "Compiler: malformed call form (dotted pair in argument list)");
        return;
    }

    c->in_tail = 0;

    /* GC-protect args: compile_expr for each arg may trigger macro expansion + GC */
    CL_GC_PROTECT(args);

    /* Builtin inlining: if `func` is a CL: symbol that matches a
     * VM opcode for this arity AND isn't shadowed by a local
     * function or upvalue, emit the opcode directly.  Saves the
     * FLOAD + CALL (~3 bytes + dispatch through call_builtin) per
     * call site, and removes one symbol from the constants pool. */
    if (CL_SYMBOL_P(func) &&
        cl_env_lookup_local_fun(c->env, func) < 0 &&
        (!c->env || cl_env_resolve_fun_upvalue(c->env, func) < 0))
    {
        int candidate_nargs = proper_list_length(args);
        uint8_t opcode = inline_builtin_opcode(func, candidate_nargs);
        if (opcode != 0) {
            CL_Obj rest = args;
            while (!CL_NULL_P(rest)) {
                compile_expr(c, cl_car(rest));
                rest = cl_cdr(rest);
            }
            cl_emit(c, opcode);
            CL_GC_UNPROTECT(1);
            c->in_tail = saved_tail;
            return;
        }
    }

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
        CL_Obj arg_val = cl_car(args);
        if (CL_HEAP_P(arg_val) && arg_val >= cl_heap.arena_size) {
            fprintf(stderr, "[compile_call] BUG: arg %d = 0x%08x exceeds heap (args=0x%08x nargs=%d code_pos=%d)\n",
                    nargs, (unsigned)arg_val, (unsigned)args, nargs, c->code_pos);
        }
        compile_expr(c, arg_val);
        nargs++;
        args = cl_cdr(args);
    }

    CL_GC_UNPROTECT(1);
    c->in_tail = saved_tail;

    if (c->in_tail) {
        cl_emit(c, OP_TAILCALL);
    } else {
        cl_emit(c, OP_CALL);
    }
    cl_emit(c, (uint8_t)nargs);
}

/* Trampoline-aware analog of compile_body.  Strips leading declarations
 * + docstrings, then returns the tail form (or CL_NIL for empty body). */
CL_Obj compile_body_tail(CL_Compiler *c, CL_Obj forms)
{
    CL_Obj rest = process_body_declarations(c, forms);
    return compile_progn_tail(c, rest);
}

void compile_body(CL_Compiler *c, CL_Obj forms)
{
    /* Mirrors compile_progn: compile_body_tail may push PROGN_ITER frames
     * that compile_expr won't drain (its own tail_base captures them
     * as already-existing).  We drain explicitly afterward. */
    int tail_base = c->tail_count;
    CL_Obj tail = compile_body_tail(c, forms);
    if (CL_NULL_P(tail)) {
        cl_emit(c, OP_NIL);
        return;
    }
    compile_expr(c, tail);
    while (c->tail_count > tail_base) {
        CL_TailFrame frame = c->tail_stack[--c->tail_count];
        CL_Obj cont = emit_tail_postlude(c, &frame);
        if (!CL_NULL_P(cont))
            compile_expr(c, cont);
    }
}

/* Look up a global symbol-macro expansion stored on the symbol's plist
   under the CLAMIGA::%SYMBOL-MACRO-EXPANSION indicator (set by the
   DEFINE-SYMBOL-MACRO macro in boot.lisp).  Returns CL_NIL when none. */
CL_Obj cl_lookup_global_symbol_macro(CL_Obj sym)
{
    CL_Obj out;
    if (cl_lookup_global_symbol_macro_p(sym, &out))
        return out;
    return CL_NIL;
}

int cl_lookup_global_symbol_macro_p(CL_Obj sym, CL_Obj *out)
{
    static CL_Obj indicator = 0;
    CL_Symbol *s;
    CL_Obj plist;

    if (!CL_SYMBOL_P(sym)) return 0;
    if (indicator == 0)
        indicator = cl_intern_in("%SYMBOL-MACRO-EXPANSION", 23, cl_package_clamiga);

    s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    plist = s->plist;
    while (!CL_NULL_P(plist) && !CL_NULL_P(cl_cdr(plist))) {
        if (cl_car(plist) == indicator) {
            *out = cl_car(cl_cdr(plist));
            return 1;
        }
        plist = cl_cdr(cl_cdr(plist));
    }
    return 0;
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

    /* Check symbol macros before variable lookup — lexical first, then global */
    {
        CL_Obj expansion = cl_env_lookup_symbol_macro(c->env, sym);
        if (CL_NULL_P(expansion))
            expansion = cl_lookup_global_symbol_macro(sym);
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

/* --- Main dispatcher ---
 *
 * compile_expr_step processes one form and either:
 *   - finishes by emitting bytecode (returns 0), or
 *   - tail-trampolines by writing a new form to *expr_p (returns 1).
 *
 * The driver compile_expr() runs compile_expr_step() in a loop, then
 * drains any tail frames pushed during the chain.  This keeps the C
 * stack flat across deeply nested forms like (let A (let B (let C ...))).
 */

static int compile_expr_step(CL_Compiler *c, CL_Obj *expr_p)
{
    CL_Obj expr = *expr_p;

    if (CL_NULL_P(expr))    { cl_emit(c, OP_NIL); return 0; }
    if (CL_FIXNUM_P(expr))  { cl_emit_const(c, expr); return 0; }
    if (CL_CHAR_P(expr))    { cl_emit_const(c, expr); return 0; }
    if (CL_ANY_STRING_P(expr)) { cl_emit_const(c, expr); return 0; }
    if (CL_BIGNUM_P(expr))  { cl_emit_const(c, expr); return 0; }
    if (CL_RATIO_P(expr))   { cl_emit_const(c, expr); return 0; }
    if (CL_COMPLEX_P(expr)) { cl_emit_const(c, expr); return 0; }
    if (CL_FLOATP(expr))    { cl_emit_const(c, expr); return 0; }
    if (CL_VECTOR_P(expr))  { cl_emit_const(c, expr); return 0; }
    if (CL_BIT_VECTOR_P(expr)) { cl_emit_const(c, expr); return 0; }
    if (CL_PATHNAME_P(expr))   { cl_emit_const(c, expr); return 0; }
    if (CL_STRUCT_P(expr))     { cl_emit_const(c, expr); return 0; }
    if (CL_HASHTABLE_P(expr))  { cl_emit_const(c, expr); return 0; }

    if (CL_SYMBOL_P(expr)) {
        compile_symbol(c, expr);
        return 0;
    }

    /* Any non-cons heap object that isn't a symbol is self-evaluating per
     * CLHS 3.1.2.1.3.  This catches streams, locks, condition-variables,
     * thread objects, random-state, conditions, instances, etc. — none of
     * them have a meaningful interpretation as a form, so EVAL returns them
     * unchanged.  Without this, e.g. lparallel's
     *   (loop for (nil . form) in specials collect (eval form))
     * blows up with "Cannot compile: unexpected STREAM" the first time a
     * thread is spawned with default special bindings. */
    if (CL_HEAP_P(expr) && !CL_CONS_P(expr)) {
        cl_emit_const(c, expr);
        return 0;
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
                /* Local macrolet expander stored by compile_macrolet is a
                 * 2-arg (form environment) wrapper — same shape as global
                 * macros installed by compile_defmacro. */
                CL_Obj expanded;
                CL_Obj call_args[2];
                CL_Obj lex_env = cl_build_lex_env(c->env);
                call_args[0] = expr;
                call_args[1] = lex_env;
                CL_GC_PROTECT(local_expander);
                CL_GC_PROTECT(lex_env);
                {
                    int _fp0 = cl_vm.fp, _sp0 = cl_vm.sp;
                    expanded = cl_vm_apply(local_expander, call_args, 2);
                    if (cl_vm.fp != _fp0 || cl_vm.sp != _sp0) {
                        fprintf(stderr, "[MXLEAK-LOCAL] local macrolet vm_apply leaked fp:%d→%d sp:%d→%d\n",
                                _fp0, cl_vm.fp, _sp0, cl_vm.sp);
                        fflush(stderr);
                    }
                }
                CL_GC_UNPROTECT(2);
                /* Trampoline into the expansion — outer compile_expr
                 * driver keeps `expr` GC-protected via &expr. */
                *expr_p = expanded;
                return 1;
            }
        }

        /* CLHS 3.1.2.1.2.4: a function form's CAR can be a global function,
         * a local FLET/LABELS function, OR a macro.  Local FLET/LABELS
         * bindings SHADOW global macro definitions of the same symbol
         * (per CLHS 3.1.2.1.2 "kinds of forms").  Without this check, a
         * `(labels ((test (c) ...)) ... (test c) ...)` whose `test`
         * symbol also has a global macro binding (e.g. fiveam:test from
         * a co-loaded library) would expand to the macro's body
         * `(register-test ...)`, breaking the labels invocation.  This
         * surfaced specifically as "Undefined function: REGISTER-TEST"
         * during cold compile of trivia.balland2006/optimizer.lisp's
         * `apply-fusion`, which uses `(labels ((test (c) ...)) ...)`. */
        if (CL_SYMBOL_P(head)) {
            if (cl_env_lookup_local_fun(c->env, head) >= 0)
                goto do_call;
            if (c->env && cl_env_resolve_fun_upvalue(c->env, head) >= 0)
                goto do_call;
        }

        if (CL_SYMBOL_P(head) && cl_macro_p(head)) {
            int _fp0 = cl_vm.fp, _sp0 = cl_vm.sp;
            CL_Obj expanded;
            CL_Obj lex_env = cl_build_lex_env(c->env);
            CL_GC_PROTECT(lex_env);
            expanded = cl_macroexpand_1_env(expr, lex_env);
            CL_GC_UNPROTECT(1);
            if (cl_vm.fp != _fp0 || cl_vm.sp != _sp0) {
                const char *_mname = CL_SYMBOL_P(head) ? cl_symbol_name(head) : "?";
                fprintf(stderr, "[MXLEAK-GLOBAL] macroexpand_1(%s) leaked fp:%d→%d sp:%d→%d\n",
                        _mname, _fp0, cl_vm.fp, _sp0, cl_vm.sp);
                fflush(stderr);
            }
            /* Trampoline into the expansion — outer driver protects expr. */
            *expr_p = expanded;
            return 1;
        }

        if (head == SYM_QUOTE)       { compile_quote(c, expr); return 0; }
        if (head == SYM_IF) {
            /* Always returns the THEN form; ELSE handling deferred to
             * the IF_AFTER_THEN postlude. */
            *expr_p = compile_if(c, expr);
            return 1;
        }
        /* Trampoline PROGN: walk all but last, then continue with last. */
        if (head == SYM_PROGN) {
            CL_Obj forms = cl_cdr(expr);
            if (CL_NULL_P(forms)) { cl_emit(c, OP_NIL); return 0; }
            while (!CL_NULL_P(cl_cdr(forms))) {
                int prev_tail = c->in_tail;
                c->in_tail = 0;
                compile_expr(c, cl_car(forms));
                c->in_tail = prev_tail;
                cl_emit(c, OP_POP);
                forms = cl_cdr(forms);
            }
            *expr_p = cl_car(forms);
            return 1;
        }
        if (head == SYM_LAMBDA)      { compile_lambda(c, expr); return 0; }
        /* Trampoline LET / LET*: prelude pushes a CL_TAIL_LET frame and
         * returns the tail body form (or CL_NIL for empty body). */
        if (head == SYM_LET || head == SYM_LETSTAR) {
            CL_Obj tail = compile_let(c, expr, head == SYM_LETSTAR);
            if (CL_NULL_P(tail)) {
                cl_emit(c, OP_NIL);
                return 0;
            }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_SETQ)        { compile_setq(c, expr); return 0; }
        if (head == SYM_SETF)        { compile_setf(c, expr); return 0; }
        if (head == SYM_FUNCTION)    { compile_function(c, expr); return 0; }
        /* compiler_extra.c */
        if (head == SYM_NAMED_LAMBDA) { compile_named_lambda(c, expr); return 0; }
        if (head == SYM_DEFUN)       { compile_defun(c, expr); return 0; }
        if (head == SYM_DEFVAR)      { compile_defvar(c, expr); return 0; }
        if (head == SYM_DEFPARAMETER) { compile_defparameter(c, expr); return 0; }
        if (head == SYM_DEFCONSTANT)  { compile_defconstant(c, expr); return 0; }
        if (head == SYM_DEFMACRO)    { compile_defmacro(c, expr); return 0; }
        if (head == SYM_AND)         { compile_and(c, expr); return 0; }
        if (head == SYM_OR)          { compile_or(c, expr); return 0; }
        if (head == SYM_COND)        { compile_cond(c, expr); return 0; }
        if (head == SYM_CASE)        { compile_case(c, expr, 0); return 0; }
        if (head == SYM_ECASE)       { compile_case(c, expr, 1); return 0; }
        if (head == SYM_TYPECASE)    { compile_typecase(c, expr, 0); return 0; }
        if (head == SYM_ETYPECASE)   { compile_typecase(c, expr, 1); return 0; }
        if (head == SYM_QUASIQUOTE)  { compile_quasiquote(c, expr); return 0; }
        if (head == SYM_EVAL_WHEN) {
            CL_Obj tail = compile_eval_when(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_LOAD_TIME_VALUE) { compile_load_time_value(c, expr); return 0; }
        if (head == SYM_DEFSETF)     { compile_defsetf(c, expr); return 0; }
        if (head == SYM_DEFTYPE)     { compile_deftype(c, expr); return 0; }
        if (head == SYM_MULTIPLE_VALUE_BIND) {
            CL_Obj tail = compile_multiple_value_bind(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_MULTIPLE_VALUE_CALL)  { compile_multiple_value_call(c, expr); return 0; }
        if (head == SYM_MULTIPLE_VALUE_LIST)  { compile_multiple_value_list(c, expr); return 0; }
        if (head == SYM_MULTIPLE_VALUE_PROG1) { compile_multiple_value_prog1(c, expr); return 0; }
        if (head == SYM_NTH_VALUE)            { compile_nth_value(c, expr); return 0; }
        /* compiler_special.c */
        /* BLOCK / RETURN-FROM / RETURN trampoline.  The prelude emits any
         * setup bytecode and pushes a tail frame; we continue the trampoline
         * with the tail body / value form (or emit OP_NIL for empty body). */
        if (head == SYM_BLOCK) {
            CL_Obj tail = compile_block(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_RETURN_FROM) {
            CL_Obj tail = compile_return_from(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_RETURN) {
            CL_Obj tail = compile_return(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_FLET) {
            CL_Obj tail = compile_flet(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_LABELS) {
            CL_Obj tail = compile_labels(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_DO)          { compile_do(c, expr); return 0; }
        if (head == SYM_DO_STAR)     { compile_do_star(c, expr); return 0; }
        if (head == SYM_DOLIST)      { compile_dolist(c, expr); return 0; }
        if (head == SYM_DOTIMES)     { compile_dotimes(c, expr); return 0; }
        if (head == SYM_TAGBODY)     { compile_tagbody(c, expr); return 0; }
        if (head == SYM_GO)          { compile_go(c, expr); return 0; }
        if (head == SYM_CATCH)       { compile_catch(c, expr); return 0; }
        if (head == SYM_UNWIND_PROTECT) { compile_unwind_protect(c, expr); return 0; }
        if (head == SYM_PROGV) {
            CL_Obj tail = compile_progv(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_DESTRUCTURING_BIND) { compile_destructuring_bind(c, expr); return 0; }
        if (head == SYM_HANDLER_BIND) {
            CL_Obj tail = compile_handler_bind(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_RESTART_CASE) { compile_restart_case(c, expr); return 0; }
        if (head == SYM_DECLARE) {
            /* Tolerate misplaced declares (ignore them with a warning).
             * Some macro expansions place declares outside body position;
             * standard CL implementations just ignore them there. */
            platform_write_string("\033[33mWARNING: DECLARE is not allowed here -- ignoring\033[0m\n");
            cl_emit(c, OP_NIL);
            return 0;
        }
        if (head == SYM_DECLAIM)     { compile_declaim(c, expr); return 0; }
        if (head == SYM_LOCALLY) {
            CL_Obj tail = compile_locally(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_TRACE)       { compile_trace(c, expr); return 0; }
        if (head == SYM_UNTRACE)     { compile_untrace(c, expr); return 0; }
        if (head == SYM_TIME)        { compile_time(c, expr); return 0; }
        if (head == SYM_IN_PACKAGE)  { compile_in_package(c, expr); return 0; }
        if (head == SYM_MACROLET) {
            CL_Obj tail = compile_macrolet(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_SYMBOL_MACROLET) {
            CL_Obj tail = compile_symbol_macrolet(c, expr);
            if (CL_NULL_P(tail)) { cl_emit(c, OP_NIL); return 0; }
            *expr_p = tail;
            return 1;
        }
        if (head == SYM_THE)             { compile_the(c, expr); return 0; }

        /* (funcall fn arg1 arg2 ...) → compile fn as value, args, OP_CALL
         * Avoids C stack nesting by keeping the call in the VM dispatch loop. */
        if (head == SYM_FUNCALL) {
            CL_Obj fargs = cl_cdr(expr);
            int nargs = 0;
            int saved_tail;
            if (CL_NULL_P(fargs))
                cl_error(CL_ERR_ARGS, "FUNCALL requires at least one argument");
            saved_tail = c->in_tail;
            c->in_tail = 0;
            /* GC-protect fargs: compile_expr may trigger macro expansion + GC */
            CL_GC_PROTECT(fargs);
            /* Compile the function expression */
            compile_expr(c, cl_car(fargs));
            /* Compile remaining arguments */
            fargs = cl_cdr(fargs);
            while (!CL_NULL_P(fargs)) {
                compile_expr(c, cl_car(fargs));
                nargs++;
                fargs = cl_cdr(fargs);
            }
            CL_GC_UNPROTECT(1);
            c->in_tail = saved_tail;
            if (c->in_tail)
                cl_emit(c, OP_TAILCALL);
            else
                cl_emit(c, OP_CALL);
            cl_emit(c, (uint8_t)nargs);
            return 0;
        }

        /* (apply fn arg1 ... arglist) → build full arglist via CONS, OP_APPLY
         * Avoids C stack nesting by using the VM's inline OP_APPLY handler. */
        if (head == SYM_APPLY) {
            CL_Obj fargs = cl_cdr(expr);
            int napply_args = 0;
            int saved_tail, i;
            if (CL_NULL_P(fargs) || CL_NULL_P(cl_cdr(fargs)))
                cl_error(CL_ERR_ARGS, "APPLY requires at least two arguments");
            saved_tail = c->in_tail;
            c->in_tail = 0;
            /* GC-protect fargs: compile_expr may trigger macro expansion + GC */
            CL_GC_PROTECT(fargs);
            /* Compile the function expression */
            compile_expr(c, cl_car(fargs));
            /* Compile all remaining args (leading args + final arglist) */
            fargs = cl_cdr(fargs);
            while (!CL_NULL_P(fargs)) {
                compile_expr(c, cl_car(fargs));
                napply_args++;
                fargs = cl_cdr(fargs);
            }
            CL_GC_UNPROTECT(1);
            /* CONS leading args onto the final arglist to build full arglist.
             * Stack has: fn a1 a2 ... arglist
             * We need (n-1) CONS ops to produce: fn (a1 a2 ... . arglist) */
            for (i = 0; i < napply_args - 1; i++)
                cl_emit(c, OP_CONS);
            c->in_tail = saved_tail;
            cl_emit(c, OP_APPLY);
            return 0;
        }

    do_call:
        compile_call(c, expr);
        return 0;
    }

    cl_error(CL_ERR_GENERAL, "Cannot compile: unexpected %s in expression position",
             cl_type_name(expr));
    return 0;  /* unreachable */
}

/* Driver: trampoline-loop over compile_expr_step and drain pending tail
 * frames.  Each step either finishes (return 0) or sets a new form for
 * the trampoline (return 1).  The drain emits postludes (env restore,
 * OP_DYNUNBIND, etc.) for any frames the chain pushed.
 *
 * A postlude can return a continuation form (non-CL_NIL) — that form
 * becomes the next trampoline target, after which the drain resumes.
 * IF_AFTER_THEN uses this to dispatch the ELSE branch once THEN has
 * compiled, keeping both branches off the C stack. */
void compile_expr(CL_Compiler *c, CL_Obj expr)
{
    int tail_base = c->tail_count;

    /* expr is updated in place via compile_expr_step's *expr_p; protect &expr
     * for the entire chain so macroexpand/let-prelude allocations don't
     * dangling our current form. */
    CL_GC_PROTECT(expr);

    while (compile_expr_step(c, &expr)) { /* trampoline */ }

    while (c->tail_count > tail_base) {
        /* Copy by value: emit_tail_postlude may push new frames which
         * could realloc tail_stack and invalidate any pointer into it. */
        CL_TailFrame frame = c->tail_stack[--c->tail_count];
        CL_Obj cont = emit_tail_postlude(c, &frame);
        if (!CL_NULL_P(cont)) {
            /* expr is GC-protected via &expr; reassigning carries the
             * continuation form into protected territory. */
            expr = cont;
            while (compile_expr_step(c, &expr)) { /* trampoline */ }
        }
    }

    CL_GC_UNPROTECT(1);
}

/* --- Macro expansion (runtime, via VM) --- */

CL_Obj cl_macroexpand_1(CL_Obj form)
{
    return cl_macroexpand_1_env(form, CL_NIL);
}

/* Walk a lex-env alist (built by cl_build_lex_env) for a MACROLET entry
 * whose name matches sym.  Returns the expander or CL_NIL. */
static CL_Obj lex_env_local_macro(CL_Obj lex_env, CL_Obj sym)
{
    while (CL_CONS_P(lex_env)) {
        CL_Obj pair = cl_car(lex_env);
        if (CL_CONS_P(pair) && cl_car(pair) == SYM_LEX_LOCAL_MACRO) {
            CL_Obj inner = cl_cdr(pair);
            if (CL_CONS_P(inner) && cl_car(inner) == sym)
                return cl_cdr(inner);
        }
        lex_env = cl_cdr(lex_env);
    }
    return CL_NIL;
}

CL_Obj cl_macroexpand_1_env(CL_Obj form, CL_Obj lex_env)
{
    CL_Obj head = cl_car(form);
    CL_Obj expander;
    CL_Obj expanded;
    CL_Obj call_args[2];

    /* Local (macrolet) bindings shadow global macros — check the
     * captured lex-env first so &environment-aware expanders that
     * MACROEXPAND a sub-form see macrolet-bound macros (CLHS 5.1.4). */
    if (CL_SYMBOL_P(head) && CL_CONS_P(lex_env)) {
        expander = lex_env_local_macro(lex_env, head);
        if (!CL_NULL_P(expander)) {
            call_args[0] = form;
            call_args[1] = lex_env;
            CL_GC_PROTECT(form);
            CL_GC_PROTECT(expander);
            CL_GC_PROTECT(lex_env);
            expanded = cl_vm_apply(expander, call_args, 2);
            CL_GC_UNPROTECT(3);
            return expanded;
        }
    }

    expander = cl_get_macro(head);
    if (CL_NULL_P(expander)) return form;

    /* Expander is a 2-arg (form environment) wrapper produced by
     * compile_defmacro (or a user-supplied function via SETF MACRO-FUNCTION).
     * The wrapper itself sets cl_current_lex_env for any &environment
     * binding inside the inner expander body. */
    call_args[0] = form;
    call_args[1] = lex_env;

    CL_GC_PROTECT(form);
    CL_GC_PROTECT(expander);
    CL_GC_PROTECT(lex_env);

    expanded = cl_vm_apply(expander, call_args, 2);

    CL_GC_UNPROTECT(3);

    return expanded;
}


/* --- Macro table --- */

void cl_register_macro(CL_Obj name, CL_Obj expander)
{
    CL_Obj pair;
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(expander);
    pair = cl_cons(name, expander);
    cl_tables_wrlock();
    macro_table = cl_cons(pair, macro_table);
    cl_tables_rwunlock();
    CL_GC_UNPROTECT(2);
}

/* Snapshot-and-release iteration:
 *   macro_table is only ever PREPENDED-to (cl_register_macro builds a
 *   new head whose cdr is the prior head).  Old cells stay reachable
 *   from the new head's cdr chain, so once we capture the head pointer
 *   under the lock we can release the lock and walk the snapshot
 *   without races.  Holding the rdlock across cl_car (which can
 *   cl_error → longjmp on a corrupt cell) was leaking readers and
 *   blocking later writers — see the cl_tables_rwlock deadlock fix. */
int cl_macro_p(CL_Obj name)
{
    CL_Obj list;
    cl_tables_rdlock();
    list = macro_table;
    cl_tables_rwunlock();
    while (!CL_NULL_P(list)) {
        if (cl_car(cl_car(list)) == name)
            return 1;
        list = cl_cdr(list);
    }
    return 0;
}

CL_Obj cl_get_macro(CL_Obj name)
{
    CL_Obj list;
    cl_tables_rdlock();
    list = macro_table;
    cl_tables_rwunlock();
    while (!CL_NULL_P(list)) {
        CL_Obj pair = cl_car(list);
        if (cl_car(pair) == name)
            return cl_cdr(pair);
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
    cl_tables_wrlock();
    type_table = cl_cons(pair, type_table);
    cl_tables_rwunlock();
    CL_GC_UNPROTECT(2);
}

CL_Obj cl_get_type_expander(CL_Obj name)
{
    CL_Obj list;
    cl_tables_rdlock();
    list = type_table;
    cl_tables_rwunlock();
    while (!CL_NULL_P(list)) {
        CL_Obj pair = cl_car(list);
        if (cl_car(pair) == name)
            return cl_cdr(pair);
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
    cl_tables_wrlock();
    setf_fn_table = cl_cons(pair, setf_fn_table);
    cl_tables_rwunlock();
    CL_GC_UNPROTECT(2);
}

/* --- Public API --- */

/* CL_Compiler is ~155KB — too large for the AmigaOS stack and too costly
 * to alloc/free per call.  AllocVec/FreeVec of 155KB blocks fragments the
 * AmigaOS system pool: after roughly 44 cycles loading lib/clos.lisp the
 * next AllocVec(155KB) returns NULL even though plenty of free RAM exists.
 * To avoid that we keep a process-wide free-list — popped on entry, pushed
 * on exit — so a successfully allocated CL_Compiler is never returned to
 * AmigaOS.  Nested compiles (parent chain) are handled correctly: each
 * call still gets a distinct struct.  The chain is short (<= depth of
 * compile-time macro expansion) so memory growth is bounded. */
static CL_Compiler *cl_compiler_pool_head = NULL;
static void *cl_compiler_pool_lock = NULL;

static CL_Compiler *cl_compiler_pool_acquire(void)
{
    CL_Compiler *c = NULL;
    if (cl_compiler_pool_lock) platform_mutex_lock(cl_compiler_pool_lock);
    if (cl_compiler_pool_head) {
        c = cl_compiler_pool_head;
        cl_compiler_pool_head = (CL_Compiler *)c->parent; /* free-list link */
    }
    if (cl_compiler_pool_lock) platform_mutex_unlock(cl_compiler_pool_lock);
    if (!c) {
        c = (CL_Compiler *)platform_alloc(sizeof(CL_Compiler));
    }
    return c;
}

static void cl_compiler_pool_release(CL_Compiler *c)
{
    if (!c) return;
    /* Drop owned external buffers — only the 155KB struct itself is pooled. */
    if (c->tail_stack) { platform_free(c->tail_stack); c->tail_stack = NULL; }
    if (cl_compiler_pool_lock) platform_mutex_lock(cl_compiler_pool_lock);
    c->parent = cl_compiler_pool_head; /* reuse parent slot as free-list link */
    cl_compiler_pool_head = c;
    if (cl_compiler_pool_lock) platform_mutex_unlock(cl_compiler_pool_lock);
}

void cl_compiler_pool_init(void)
{
    int i;
    if (!cl_compiler_pool_lock)
        platform_mutex_init(&cl_compiler_pool_lock);
    /* Pre-warm: AmigaOS AllocVec of 155 KB succeeds reliably at startup
     * (system pool unfragmented) but starts to fail after a workload like
     * source-loading lib/clos.lisp churns memory.  Reserve enough blocks
     * up front to cover the worst-case nested-compile chain depth we have
     * seen (1 outer cl_compile + several compile_lambda for inner lambdas
     * in CLOS-heavy methods).  8 is comfortably above the high-water mark
     * observed and only costs ~1.2 MB on a 64 MB Amiga. */
    for (i = 0; i < 8; i++) {
        CL_Compiler *c = (CL_Compiler *)platform_alloc(sizeof(CL_Compiler));
        if (!c) break;
        c->tail_stack = NULL;
        c->parent = cl_compiler_pool_head;
        cl_compiler_pool_head = c;
    }
}

CL_Obj cl_compile(CL_Obj expr)
{
    CL_Compiler *comp;
    CL_CompEnv *env;
    CL_Bytecode *bc;

    comp = cl_compiler_pool_acquire();
    if (!comp) {
        platform_write_string("[compile] platform_alloc(CL_Compiler) FAILED\n");
        return CL_NIL;
    }
    memset(comp, 0, sizeof(*comp));
    env = cl_env_create(NULL);
    comp->env = env;
    comp->in_tail = 0;

    /* Register compiler for GC root marking */
    comp->parent = cl_active_compiler;
    cl_active_compiler = comp;

    compile_expr(comp, expr);

    cl_emit(comp, OP_HALT);

    bc = (CL_Bytecode *)cl_alloc(TYPE_BYTECODE, sizeof(CL_Bytecode));
    if (!bc) {
        platform_write_string("[compile] cl_alloc(TYPE_BYTECODE) FAILED\n");
        cl_env_destroy(env);
        cl_compiler_pool_release(comp);
        return CL_NIL;
    }

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

    /* Unregister compiler from GC root chain */
    cl_active_compiler = comp->parent;

    cl_env_destroy(env);

    cl_compiler_pool_release(comp);

    return CL_PTR_TO_OBJ(bc);
}

CL_Obj cl_compile_defun(CL_Obj name, CL_Obj lambda_list, CL_Obj body)
{
    CL_Obj form = cl_cons(SYM_DEFUN,
                          cl_cons(name,
                                  cl_cons(lambda_list, body)));
    return cl_compile(form);
}

/* Mark compiler constants for a specific thread's active compiler chain.
 * Used by multi-thread GC (Phase 2+). */
void cl_compiler_gc_mark_thread(CL_Thread *t)
{
    extern void gc_mark_obj(CL_Obj obj);
    CL_Compiler *c = t->active_compiler;
    while (c) {
        int i;
        for (i = 0; i < c->const_count; i++)
            gc_mark_obj(c->constants[i]);
        /* Also mark block/tagbody tags and outer block/tag references */
        for (i = 0; i < c->block_count; i++)
            gc_mark_obj(c->blocks[i].tag);
        for (i = 0; i < c->tagbody_count; i++) {
            int j;
            gc_mark_obj(c->tagbodies[i].id);
            for (j = 0; j < c->tagbodies[i].n_tags; j++)
                gc_mark_obj(c->tagbodies[i].tags[j].tag);
        }
        for (i = 0; i < c->outer_block_count; i++)
            gc_mark_obj(c->outer_blocks[i]);
        for (i = 0; i < c->outer_tag_count; i++) {
            gc_mark_obj(c->outer_tags[i].tag);
            gc_mark_obj(c->outer_tags[i].tagbody_id);
        }
        /* Pending tail frames hold continuation forms (e.g. the ELSE
         * branch of an IF whose THEN is still compiling) that must
         * survive GC during the body's compilation. */
        for (i = 0; i < c->tail_count; i++)
            gc_mark_obj(c->tail_stack[i].cont_form);
        /* Mark compile-time environment (platform_alloc'd, holds CL_Obj refs) */
        if (c->env) {
            CL_CompEnv *env = c->env;
            while (env) {
                for (i = 0; i < env->local_count; i++)
                    gc_mark_obj(env->locals[i]);
                for (i = 0; i < env->local_fun_count; i++)
                    gc_mark_obj(env->local_funs[i].name);
                for (i = 0; i < env->local_macro_count; i++) {
                    gc_mark_obj(env->local_macros[i].name);
                    gc_mark_obj(env->local_macros[i].expander);
                }
                for (i = 0; i < env->symbol_macro_count; i++) {
                    gc_mark_obj(env->symbol_macros[i].name);
                    gc_mark_obj(env->symbol_macros[i].expansion);
                }
                env = env->parent;
            }
        }
        c = c->parent;
    }
}

/* Legacy wrapper — marks current thread's compiler chain */
void cl_compiler_gc_mark(void)
{
    cl_compiler_gc_mark_thread(cl_get_current_thread());
}

/* Update compiler roots during compaction (mirrors gc_mark_thread but rewrites) */
void cl_compiler_gc_update_thread(CL_Thread *t, void (*update)(CL_Obj *))
{
    CL_Compiler *c = t->active_compiler;
    while (c) {
        int i;
        for (i = 0; i < c->const_count; i++)
            update(&c->constants[i]);
        for (i = 0; i < c->block_count; i++)
            update(&c->blocks[i].tag);
        for (i = 0; i < c->tagbody_count; i++) {
            int j;
            update(&c->tagbodies[i].id);
            for (j = 0; j < c->tagbodies[i].n_tags; j++)
                update(&c->tagbodies[i].tags[j].tag);
        }
        for (i = 0; i < c->outer_block_count; i++)
            update(&c->outer_blocks[i]);
        for (i = 0; i < c->outer_tag_count; i++) {
            update(&c->outer_tags[i].tag);
            update(&c->outer_tags[i].tagbody_id);
        }
        /* Pending tail-frame continuation forms — see mark side. */
        for (i = 0; i < c->tail_count; i++)
            update(&c->tail_stack[i].cont_form);
        if (c->env) {
            CL_CompEnv *env = c->env;
            while (env) {
                for (i = 0; i < env->local_count; i++)
                    update(&env->locals[i]);
                for (i = 0; i < env->local_fun_count; i++)
                    update(&env->local_funs[i].name);
                for (i = 0; i < env->local_macro_count; i++) {
                    update(&env->local_macros[i].name);
                    update(&env->local_macros[i].expander);
                }
                for (i = 0; i < env->symbol_macro_count; i++) {
                    update(&env->symbol_macros[i].name);
                    update(&env->symbol_macros[i].expansion);
                }
                env = env->parent;
            }
        }
        c = c->parent;
    }
}

void cl_compiler_init(void)
{
    macro_table = CL_NIL;
    setf_table = CL_NIL;
    type_table = CL_NIL;

    if (!cl_tables_rwlock)
        platform_rwlock_init(&cl_tables_rwlock);

    cl_compiler_pool_init();

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
    SETF_SYM_SYMBOL_PLIST    = cl_intern_in("SYMBOL-PLIST", 12, cl_package_cl);
    SETF_SYM_FDEFINITION     = cl_intern_in("FDEFINITION", 11, cl_package_cl);
    SETF_HELPER_NTH          = cl_intern_in("%SETF-NTH", 9, cl_package_clamiga);
    SETF_HELPER_SV           = cl_intern_in("%SET-SYMBOL-VALUE", 17, cl_package_clamiga);
    SETF_HELPER_SF           = cl_intern_in("%SET-SYMBOL-FUNCTION", 20, cl_package_clamiga);
    SETF_HELPER_SP           = cl_intern_in("%SET-SYMBOL-PLIST", 17, cl_package_clamiga);
    SETF_SYM_GETHASH         = cl_intern_in("GETHASH", 7, cl_package_cl);
    SETF_HELPER_GETHASH      = cl_intern_in("%SETF-GETHASH", 13, cl_package_clamiga);
    SETF_HELPER_AREF         = cl_intern_in("%SETF-AREF", 10, cl_package_clamiga);
    SETF_SYM_ROW_MAJOR_AREF = cl_intern_in("ROW-MAJOR-AREF", 14, cl_package_cl);
    SETF_HELPER_ROW_MAJOR_AREF = cl_intern_in("%SETF-ROW-MAJOR-AREF", 20, cl_package_clamiga);
    SETF_SYM_FILL_POINTER    = cl_intern_in("FILL-POINTER", 12, cl_package_cl);
    SETF_HELPER_FILL_POINTER = cl_intern_in("%SETF-FILL-POINTER", 18, cl_package_clamiga);
    SETF_SYM_BIT             = cl_intern_in("BIT", 3, cl_package_cl);
    SETF_HELPER_BIT          = cl_intern_in("%SETF-BIT", 9, cl_package_clamiga);
    SETF_SYM_SBIT            = cl_intern_in("SBIT", 4, cl_package_cl);
    SETF_HELPER_SBIT         = cl_intern_in("%SETF-SBIT", 10, cl_package_clamiga);
    SETF_SYM_GET             = cl_intern_in("GET", 3, cl_package_cl);
    SETF_HELPER_GET          = cl_intern_in("%SETF-GET", 9, cl_package_clamiga);
    SETF_SYM_GETF            = cl_intern_in("GETF", 4, cl_package_cl);
    SETF_HELPER_GETF         = cl_intern_in("%SETF-GETF", 10, cl_package_clamiga);
    SYM_LEX_LOCAL_MACRO      = cl_intern_in("%LEX-LOCAL-MACRO", 16, cl_package_clamiga);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&SETF_SYM_CAR);
    cl_gc_register_root(&SETF_SYM_CDR);
    cl_gc_register_root(&SETF_SYM_FIRST);
    cl_gc_register_root(&SETF_SYM_REST);
    cl_gc_register_root(&SETF_SYM_NTH);
    cl_gc_register_root(&SETF_SYM_AREF);
    cl_gc_register_root(&SETF_SYM_SVREF);
    cl_gc_register_root(&SETF_SYM_CHAR);
    cl_gc_register_root(&SETF_SYM_SCHAR);
    cl_gc_register_root(&SETF_SYM_SYMBOL_VALUE);
    cl_gc_register_root(&SETF_SYM_SYMBOL_FUNCTION);
    cl_gc_register_root(&SETF_SYM_SYMBOL_PLIST);
    cl_gc_register_root(&SETF_SYM_FDEFINITION);
    cl_gc_register_root(&SETF_HELPER_NTH);
    cl_gc_register_root(&SETF_HELPER_SV);
    cl_gc_register_root(&SETF_HELPER_SF);
    cl_gc_register_root(&SETF_HELPER_SP);
    cl_gc_register_root(&SETF_SYM_GETHASH);
    cl_gc_register_root(&SETF_HELPER_GETHASH);
    cl_gc_register_root(&SETF_HELPER_AREF);
    cl_gc_register_root(&SETF_SYM_ROW_MAJOR_AREF);
    cl_gc_register_root(&SETF_HELPER_ROW_MAJOR_AREF);
    cl_gc_register_root(&SETF_SYM_FILL_POINTER);
    cl_gc_register_root(&SETF_HELPER_FILL_POINTER);
    cl_gc_register_root(&SETF_SYM_BIT);
    cl_gc_register_root(&SETF_HELPER_BIT);
    cl_gc_register_root(&SETF_SYM_SBIT);
    cl_gc_register_root(&SETF_HELPER_SBIT);
    cl_gc_register_root(&SETF_SYM_GET);
    cl_gc_register_root(&SETF_HELPER_GET);
    cl_gc_register_root(&SETF_SYM_GETF);
    cl_gc_register_root(&SETF_HELPER_GETF);
    cl_gc_register_root(&SYM_LEX_LOCAL_MACRO);
}

/* --- Compiler chain save/restore for NLX --- */

void *cl_compiler_mark(void)
{
    return (void *)cl_active_compiler;
}

void cl_compiler_restore_to(void *saved)
{
    CL_Compiler *target = (CL_Compiler *)saved;
    while (cl_active_compiler != target && cl_active_compiler != NULL) {
        CL_Compiler *c = cl_active_compiler;
        if (c->protect) {
            /* Compiler is still in use by C code (e.g., compile_lambda's
             * determine_boxed_vars which re-enters the VM for macroexpansion).
             * Don't free it — it will be freed when compilation completes. */
            return;
        }
        cl_active_compiler = c->parent;
        if (c->env) cl_env_destroy(c->env);
        cl_compiler_pool_release(c);
    }
}
