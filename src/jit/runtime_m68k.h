/* runtime_m68k.h -- the m68k walker's own runtime helpers (runtime_m68k.c).
 * The CPU-neutral ones are in runtime.h. */

#ifndef CL_JIT_RUNTIME_M68K_H
#define CL_JIT_RUNTIME_M68K_H

#ifdef JIT_M68K

#include "core/types.h"

void   cl_jit_runtime_init(void);

/* A direct-call site's cell (specs/jit-direct-calls.md §2): 12 bytes the
 * walker appends after the function's code, one per OP_CALL / OP_TAILCALL /
 * OP_CALL_GLOBAL / OP_TAILCALL_GLOBAL.  Native code only reads it; the
 * _site helpers below fill it on a miss.  Valid only while gen equals
 * cl_call_gen, so `func` needs neither GC rooting nor relocation.  The
 * offsets are baked into the emitted hit path: gen 0, func 4, entry 8. */
typedef struct {
    volatile uint32_t gen;     /* cl_call_gen at fill time; 0 = never filled */
    volatile CL_Obj   func;    /* the callee (bytecode or closure)          */
    void * volatile   entry;   /* its bc->native_code                       */
} CL_JitCallSite;

/* The miss path of a call site.  Builtins, FFI stubs and native callees are
 * dispatched directly (arguments copied onto the rooted VM stack, then the
 * C function or cl_jit_invoke); anything else goes through cl_vm_apply (see
 * jit_dispatch in runtime_m68k.c) -- after a safepoint poll and an attempt to
 * fill SITE, so the next call through it goes native-to-native.  The
 * _global form resolves the callee from SYM (no function slot under the
 * arguments). */
CL_Obj cl_jit_runtime_call_site(CL_Obj *operand_top, uint32_t nargs,
                                CL_JitCallSite *site);
CL_Obj cl_jit_runtime_call_global_site(CL_Obj *operand_top, uint32_t nargs,
                                       CL_Obj sym, CL_JitCallSite *site);

/* OP_BLOCK_PUSH / OP_BLOCK_POP / OP_BLOCK_RETURN.  The walker emits
 * `JSR setjmp` inline between alloc and commit so the captured frame
 * belongs to the JIT'd function itself (necessary for longjmp to
 * rewind back here).  See runtime_m68k.c for the full protocol. */
void  *cl_jit_runtime_block_alloc(CL_Obj tag);
void   cl_jit_runtime_block_commit(void);
void   cl_jit_runtime_block_pop(void);
CL_Obj cl_jit_runtime_block_post_longjmp(void);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void   cl_jit_runtime_block_return(CL_Obj tag, CL_Obj value);

/* OP_CATCH / OP_UNCATCH.  Same JIT-inline-setjmp protocol as
 * BLOCK_PUSH; the only differences are
 *   - the tag comes from the operand stack (not constants), so the
 *     walker passes it through D0 / (a7);
 *   - the matching pop is OP_UNCATCH (search-backward for CL_NLX_CATCH).
 *
 * The longjmp arrival path is byte-identical to block_post_longjmp:
 * restore dyn/handler/restart/gc/compiler marks and mv_count/mv_values
 * from the NLX frame, then return the throw result so the walker can
 * push it onto the operand stack and BRA to the landing IP.  We keep a
 * dedicated catch_post_longjmp anyway so future divergence (e.g.
 * preserving condition state) doesn't have to refactor a shared site.
 *
 * Cross-implementation interop is automatic: bi_throw (the THROW
 * builtin) longjmps via the buf field of whichever CATCH frame matched
 * — VM-owned or JIT-owned — so throws from either side reach catches
 * on either side.  Same property block_return relies on. */
void  *cl_jit_runtime_catch_alloc(CL_Obj tag);
void   cl_jit_runtime_catch_commit(void);
void   cl_jit_runtime_catch_pop(void);
CL_Obj cl_jit_runtime_catch_post_longjmp(void);

/* OP_UWPROT / OP_UWPOP / OP_UWRETHROW.  Same JSR-setjmp-inline shape
 * as BLOCK_PUSH; alloc reserves the NLX slot without committing it,
 * the walker emits JSR setjmp, then commit bumps cl_nlx_top.  On the
 * longjmp arrival the post_longjmp helper restores all the marks the
 * VM's OP_UWPROT longjmp arm would restore.
 *
 *   uwprot_alloc       — fill cl_nlx_stack[cl_nlx_top] (type=UWPROT,
 *                         marks, vm_sp/vm_fp); return &nlx->buf.
 *   uwprot_commit      — cl_nlx_top++.
 *   uwprot_post_longjmp — restore marks/mv_count/mv_values from frame.
 *   uwprot_pop          — normal-exit pop (search-backward for UWPROT
 *                         frame, clear cl_pending_throw).
 *   uwprot_rethrow      — cl_pending_throw==1 → find catch/block and
 *                         longjmp; ==2 → cl_error with saved state;
 *                         ==0 → nop.  May not return. */
void  *cl_jit_runtime_uwprot_alloc(void);
void   cl_jit_runtime_uwprot_commit(void);
void   cl_jit_runtime_uwprot_pop(void);
void   cl_jit_runtime_uwprot_post_longjmp(void);
void   cl_jit_runtime_uwprot_rethrow(void);

/* OP_HANDLER_CASE_PUSH / OP_HANDLER_CASE_POP.  Same JSR-setjmp-inline
 * shape as BLOCK_PUSH; the frame is CL_NLX_HANDLER_CASE, its tag the
 * clause TYPE list, and commit also pushes one clause binding per type
 * (fixnum clause index, nlx_index = the frame) so cl_signal_condition's
 * cl_handler_case_transfer lands in our buf.  post_longjmp returns the
 * condition; clause() the matched clause index the walker's landing arm
 * dispatches on (clause k resumes at the k-th OP_JMP of the bytecode
 * landing table).  See runtime_m68k.c for the full protocol. */
void    *cl_jit_runtime_handler_case_alloc(CL_Obj types);
void     cl_jit_runtime_handler_case_commit(void);
void     cl_jit_runtime_handler_case_pop(void);
CL_Obj   cl_jit_runtime_handler_case_post_longjmp(void);
uint32_t cl_jit_runtime_handler_case_clause(void);

/* OP_MV_TO_LIST helper: build a list from cl_mv_values, returning it.
 * Matches the VM's quirk where cl_mv_count==0 with non-NIL primary is
 * treated as a single-value list. */
CL_Obj cl_jit_runtime_mv_to_list(CL_Obj primary);
/* OP_MV_SAVE / OP_MV_RESTORE (unwind-protect value passing, vm.h). */
void   cl_jit_runtime_mv_save(CL_Obj primary);
void   cl_jit_runtime_mv_save_empty(void);
CL_Obj cl_jit_runtime_mv_restore(void);

/* Kw prologue for JIT'd functions whose lambda-list carries &key.
 * Mirrors the matching code in vm.c::OP_CALL normal path:
 * NIL-initializes the frame's slot area, copies positional args into
 * the matching slots, then performs keyword matching right-to-left so
 * the leftmost duplicate keyword wins (CLHS 3.4.1.4.1).  Signals
 * CL_ERR_ARGS on odd argument count or unknown keyword unless
 * :allow-other-keys is enabled.
 *
 *   bc     - the callee's bytecode (read-only metadata).
 *   nargs  - actual number of caller-supplied arguments.
 *   args   - pointer to the raw arg vector (`&cl_vm.stack[sp-nargs]`).
 *   frame  - pointer to the JIT frame's locals area; the walker LEAs
 *            `-(4*n_locals)(a6)` into this pointer so frame[i]
 *            corresponds to JIT slot i (forward layout — frame[0] is
 *            the lowest-addressed slot).
 *
 * Non-allocating, so passing `bc` as a raw pointer is safe — there is
 * no GC opportunity that would relocate the bytecode header.  May
 * call cl_error which longjmps out of the JIT frame; the unwind path
 * keeps GC depth tracking consistent via the CL_ErrorFrame snapshot,
 * so no manual cleanup is required.  See the walker gate for the
 * shape restrictions (&key only, no &rest / &optional / upvalues). */
void cl_jit_runtime_kw_prologue(CL_Bytecode *bc, uint32_t nargs,
                                CL_Obj *args, CL_Obj *frame);

/* Backing for OP_AMIGA_CALL — resolves the library-base symbol to a
 * foreign-pointer address (errors like the VM's OP_AMIGA_CALL on
 * unbound/wrong-type), reverse-copies n_args from the JIT's m68k
 * operand stack into a stack-local buffer, then calls
 * cl_amiga_ffi_call_dispatch.  Allocates only if the dispatch result
 * exceeds CL_FIXNUM_MAX (bignum box) and is therefore reached by the
 * conservative scan via the caller's flushed operand-stack values. */
CL_Obj cl_jit_runtime_amiga_call(CL_Obj base_sym, int32_t offset,
                                 uint32_t regspec, uint32_t n_args,
                                 CL_Obj *operand_top);

/* OP_HANDLER_PUSH / OP_HANDLER_POP / OP_RESTART_PUSH / OP_RESTART_POP.
 * Pure push/pop on the per-thread handler/restart binding stacks; no
 * setjmp involved (handlers run as ordinary calls dispatched by
 * cl_signal_condition).  Overflow goes through cl_error so the walker
 * cache-flushes before the JSR, same as OP_DYNBIND. */
void cl_jit_runtime_handler_push(CL_Obj type_sym, CL_Obj handler);
void cl_jit_runtime_handler_pop(uint32_t count);
void cl_jit_runtime_restart_push(CL_Obj name_sym, CL_Obj handler, CL_Obj report,
                                 CL_Obj interactive, CL_Obj test, CL_Obj tag);
void cl_jit_runtime_restart_pop(uint32_t count);

/* OP_TAGBODY_PUSH / OP_TAGBODY_POP / OP_TAGBODY_GO.  Same JIT-inline-
 * setjmp protocol as BLOCK_PUSH with two twists:
 *   - the longjmp arrival re-arms the frame (cl_nlx_top++) so the
 *     tagbody stays usable for repeated GO until OP_TAGBODY_POP;
 *   - the longjmp arrival returns the tag-index fixnum that the
 *     dispatch shim emitted right after PUSH consumes via JTRUE. */
void  *cl_jit_runtime_tagbody_alloc(CL_Obj tagbody_id);
void   cl_jit_runtime_tagbody_commit(void);
void   cl_jit_runtime_tagbody_pop(void);
CL_Obj cl_jit_runtime_tagbody_post_longjmp(void);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void   cl_jit_runtime_tagbody_go(CL_Obj tagbody_id, CL_Obj tag_index);

/* Address of libc setjmp, captured at init time and baked into the
 * BLOCK_PUSH emit as a JSR.abs.l immediate. */
extern uint32_t cl_jit_setjmp_addr;

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_M68K_H */
