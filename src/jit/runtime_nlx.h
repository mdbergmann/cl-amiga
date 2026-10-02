/* runtime_nlx.h -- the runtime helpers both walkers share for the NLX
 * frames (BLOCK, CATCH, TAGBODY, UNWIND-PROTECT, HANDLER-CASE), the
 * UNWIND-PROTECT value-save stack and the handler/restart bindings
 * (runtime_nlx.c).
 *
 * The NLX protocol: the walker calls *_alloc, which reserves the frame and
 * returns &nlx->buf, then calls setjmp on that buffer FROM THE NATIVE
 * FUNCTION ITSELF (a helper that called setjmp would return, and its frame
 * would be gone before a longjmp rewound to it), then *_commit on the zero
 * return.  A non-zero return is a transfer to this frame: *_post_longjmp
 * restores the marks, and the walker resumes at the landing.  The m68k
 * walker calls them with the C-ABI arguments on the m68k stack, the
 * AArch64 one in x0-x2; nothing in them depends on where the native frame
 * keeps its operand stack. */

#ifndef CL_JIT_RUNTIME_NLX_H
#define CL_JIT_RUNTIME_NLX_H

#if defined(JIT_M68K) || defined(JIT_A64)

#include "core/types.h"

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


/* OP_HANDLER_PUSH / OP_HANDLER_POP / OP_RESTART_PUSH / OP_RESTART_POP.
 * Pure push/pop on the per-thread handler/restart binding stacks; no
 * setjmp involved (handlers run as ordinary calls dispatched by
 * cl_signal_condition).  Overflow goes through cl_error so the walker
 * cache-flushes before the JSR, same as OP_DYNBIND. */
void cl_jit_runtime_handler_push(CL_Obj type_sym, CL_Obj handler);
void cl_jit_runtime_handler_pop(uint32_t count);
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


#endif /* JIT_M68K || JIT_A64 */

#endif /* CL_JIT_RUNTIME_NLX_H */
