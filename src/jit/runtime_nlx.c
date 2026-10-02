/* runtime_nlx.c -- the NLX frames (BLOCK, CATCH, TAGBODY, UNWIND-PROTECT,
 * HANDLER-CASE), the UNWIND-PROTECT value-save stack and the handler/restart
 * bindings, shared by both native walkers (runtime_nlx.h has the protocol).
 *
 * Moved out of runtime_m68k.c unchanged when the AArch64 walker gained these
 * opcodes (specs/native-backend-a64.md, phase 3): nothing here depends on
 * where a native frame keeps its operand stack.  Some comments still speak
 * of the m68k walker's sequences (JSR setjmp, the conservative scan); the
 * AArch64 walker emits the same calls with `blr`, and its values are on the
 * VM stack below cl_vm.sp, rooted, at every one of them.
 */

#if defined(JIT_M68K) || defined(JIT_A64)

#include "jit/runtime.h"
#include "jit/runtime_nlx.h"
#include "core/types.h"
#include "core/error.h"
#include "core/symbol.h"
#include "core/printer.h"    /* cl_printer_state_save / _restore */
#include "core/thread.h"
#include "core/vm.h"         /* cl_dynbind_restore_to, CL_NLXFrame, cl_nlx_jump */
#include "core/mem.h"
#include "core/compiler.h"   /* cl_compiler_mark / cl_compiler_unwind_to */
#include "core/fasl.h"       /* cl_fasl_{reader,writer}_unwind_to */
#include <string.h>          /* strncpy (the parked error message) */

extern CL_Obj cl_car(CL_Obj obj);
extern CL_Obj cl_cdr(CL_Obj obj);
extern CL_Obj cl_cons(CL_Obj car, CL_Obj cdr);

/* --- OP_BLOCK_PUSH / OP_BLOCK_POP / OP_BLOCK_RETURN ----------------------
 *
 * Block / return-from NLX is implemented by emitting `setjmp` *inline*
 * from the JIT'd function's own frame.  The four helpers below split
 * the work the bytecode VM does in one switch-arm so the JIT can
 * sandwich its own JSR setjmp in the middle:
 *
 *   1. `block_alloc`  — fill in cl_nlx_stack[cl_nlx_top]'s metadata
 *      (tag, marks, vm_sp/vm_fp snapshot, mv_count baseline) and return
 *      a pointer to its `buf` field.  Does NOT bump cl_nlx_top: the
 *      slot is "reserved but not yet live", so an unrelated cl_error
 *      between alloc and setjmp can't unwind through a half-initialised
 *      frame.
 *   2. The JIT then emits `JSR setjmp` with that buf pointer.  setjmp
 *      saves the JIT frame's SP/PC/callee-saved regs into the buf.
 *   3. `block_commit` — bump cl_nlx_top once setjmp returns 0 (normal
 *      path).  A single MOVE-style increment; the helper is here so
 *      the per-thread `cl_nlx_top` macro stays the single point of
 *      truth for the indirection through CT.
 *   4. `block_pop`  — search-backward decrement of cl_nlx_top mirroring
 *      VM's OP_BLOCK_POP (handles the case where intervening
 *      TAGBODY/UWPROT frames leaked past a tail-call boundary).
 *   5. `block_post_longjmp` — after setjmp returns non-zero, restore
 *      marks (dyn / handler / restart / gc-root / compiler), restore
 *      mv_count and mv_values, and return the block's stored result
 *      so the JIT can push it on the operand stack.
 *   6. `block_return` — find the matching block frame on the NLX stack,
 *      stash result + mv_state in it, and longjmp.  If an UWPROT frame
 *      is interposed, divert the longjmp to that frame's buf and stash
 *      the pending throw in cl_pending_* (same protocol the VM uses,
 *      so UWPROT cleanup runs and then the rethrow chains back to the
 *      matching block).  CL_NORETURN — never returns to the caller.
 *
 * GC: helpers don't allocate Lisp objects.  cl_error on a malformed
 * RETURN-FROM (no matching block) and the overflow check in
 * block_alloc do allocate condition objects but those paths divert
 * via the existing CL_CATCH chain — same shape as cl_jit_runtime_call's
 * error paths.  The conservative scan reaches operand-stack values the
 * JIT spilled before its JSR; cache_flush at the BLOCK_PUSH branch
 * boundary keeps that invariant. */

/* Mirror of vm.c's static nlx_frame_is_stale: an NLX frame whose
 * target VM frame has been reused (typically by a tail call that
 * landed past it) is "stale" — its longjmp target no longer
 * corresponds to an active stack frame, so the UWPROT-interposition
 * loop must skip it.  Pure JIT'd code never reuses vm_fp during its
 * execution, so this only matters when JIT'd code calls bytecode that
 * tail-calls within the same vm_fp slot. */
static int jit_nlx_frame_is_stale(CL_NLXFrame *nlx)
{
    CL_Frame *target;
    if (nlx->vm_fp <= 0) return 0;
    target = &cl_vm.frames[nlx->vm_fp - 1];
    return target->code != nlx->code;
}

/* Shared NLX-frame allocator for BLOCK/CATCH/TAGBODY and the common
 * portion of UWPROT.  Reserves cl_nlx_stack[cl_nlx_top] without
 * committing it (the matching *_commit bumps cl_nlx_top).  The VM
 * longjmp channel (catch_ip/offset/code/constants/bytecode) is dead for
 * JIT-owned frames — our JSR setjmp lands the longjmp in native code —
 * so those are zero-filled here; UWPROT overwrites code/constants/
 * bytecode with the current VM frame's values after this returns.
 * Returns the reserved frame so the caller can hand &nlx->buf to the
 * emitted setjmp. */
static CL_NLXFrame *nlx_alloc_common(int type, CL_Obj tag)
{
    CL_NLXFrame *nlx;
    if (cl_nlx_top >= cl_nlx_max)
        cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");
    nlx = &cl_nlx_stack[cl_nlx_top];
    nlx->type           = (uint8_t)type;
    nlx->vm_sp          = cl_vm.sp;
    nlx->vm_fp          = cl_vm.fp;
    nlx->tag            = tag;
    nlx->result         = CL_NIL;
    nlx->catch_ip       = 0;
    nlx->offset         = 0;
    nlx->code           = NULL;
    nlx->constants      = NULL;
    nlx->bytecode       = CL_NIL;
    nlx->base_fp        = 0;
    nlx->dyn_mark            = cl_dyn_top;
    nlx->handler_mark        = cl_handler_top;
    nlx->handler_active_mask = cl_handler_active_mask;
    nlx->restart_mark        = cl_restart_top;
    nlx->gc_root_mark        = gc_root_count;
    nlx->compiler_mark       = cl_compiler_mark();
    nlx->printer_mark        = cl_printer_state_save();
    nlx->saved_jit_depth     = CT->jit_depth;
    nlx->saved_jit_c_floor   = CT->jit_c_floor;
    nlx->saved_pending_mark  = cl_saved_pending_top;
    /* The C-level error-frame (CL_CATCH) depth, as the VM records it for
     * every NLX frame (vm.c NLX_PUSH_COMMON).  It used to be captured for
     * HANDLER-CASE frames only, so a THROW into a JIT'd CATCH -- every
     * restart invoked into natively compiled code, e.g. a RESTART-CASE's
     * ABORT chosen from a handler that ran on top of a native frame --
     * left cl_error_frame_top at the depth of C frames the longjmp had
     * abandoned; the next cl_error then longjmp'd into that dead stack
     * (seen as a handler "declining" and the outer form ending, clamacs's
     * debugger, 2026-09-11; tests/amiga/dev-repl-tests.lisp). */
    nlx->error_mark          = cl_error_frame_top;
    nlx->mv_save_mark        = CT->mv_save_top;
    nlx->mv_count            = 1;
    nlx->landing             = NULL;   /* JIT frame: setjmps inline into buf */
    return nlx;
}

/* Search-backward pop shared by BLOCK/CATCH/TAGBODY/UWPROT: a tail call
 * inside the body may have leaked an intervening NLX frame, so a blind
 * --top would unwind to the wrong slot (mirrors the VM's OP_*_POP). */
static void nlx_pop_type(int type)
{
    int i;
    for (i = cl_nlx_top - 1; i >= 0; i--) {
        if (cl_nlx_stack[i].type == type) {
            cl_nlx_top = i;
            return;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
}

/* Restore the SP/FP + dyn/handler/restart/gc-root/jit-depth/compiler/
 * printer marks captured at frame alloc — shared by all four
 * *_post_longjmp helpers.  SP/FP must be rewound here: the longjmp may
 * have fired from arbitrarily deep VM execution (e.g. an inner lambda
 * invoked via cl_jit_runtime_call → cl_vm_apply that did a return-from
 * across the closure boundary).  cl_vm_apply's normal-exit restore is
 * skipped on longjmp, so SP/FP are left at the inner callee's last
 * position; subsequent OP_CALL dispatches would then operate on a stale
 * stack and silently overwrite live operands a few frames up.  The VM's
 * matching paths do the same (see vm.c OP_BLOCK_RETURN).
 *
 * LANDING_ANCHOR is CL_CAPTURE_SP() taken by the caller — the
 * cl_jit_runtime_*_post_longjmp entry point the native JIT code JSRs to
 * directly on the longjmp arm — not by this shared helper itself, so the
 * anchor matches the true landing frame instead of being one C frame
 * deeper (mirrors vm.c, which calls cl_compiler_unwind_to with
 * CL_CAPTURE_SP() taken inline at the landing point). */
static void nlx_restore_core(CL_NLXFrame *nlx, void *landing_anchor)
{
    cl_vm.sp = nlx->vm_sp;
    cl_vm.fp = nlx->vm_fp;
    cl_dynbind_restore_to(nlx->dyn_mark);
    cl_handler_top          = nlx->handler_mark;
    cl_handler_active_mask  = nlx->handler_active_mask;
    cl_restart_top          = nlx->restart_mark;
    /* Error frames pushed by C code deeper than this frame are gone with
     * the C stack the longjmp unwound: drop them, as the VM's landing does
     * (vm.c), or a later cl_error longjmps into dead stack.  The pending
     * mark likewise (both were HANDLER-CASE-only before; see
     * nlx_alloc_common). */
    cl_error_frame_top      = nlx->error_mark;
    cl_saved_pending_top    = nlx->saved_pending_mark;
    gc_root_count           = nlx->gc_root_mark;
    cl_jit_restore_depth(nlx->saved_jit_depth, nlx->saved_jit_c_floor);
    cl_compiler_unwind_to(nlx->compiler_mark, landing_anchor);
    cl_fasl_reader_unwind_to(landing_anchor);
    cl_fasl_writer_unwind_to(landing_anchor);
    cl_printer_state_restore(nlx->printer_mark);
    CT->mv_save_top = nlx->mv_save_mark;
}

/* BLOCK/CATCH longjmp arrival: core restore + full multiple-value set +
 * return the stashed result.  (TAGBODY and UWPROT diverge on the MV
 * handling and are written out longhand.)  LANDING_ANCHOR is forwarded
 * to nlx_restore_core unchanged — see its comment.
 *
 * This landing IS the target of a non-local transfer, so that transfer is
 * complete: drop any pending NLX state, as the VM's CATCH/BLOCK landing
 * does.  The pending state can be a FOREIGN one — an error unwind
 * (cl_pending_throw == 2) that landed in an UNWIND-PROTECT cleanup, where
 * the cleanup then threw out to this catch.  CLHS 5.2 says the original
 * transfer is abandoned when a cleanup initiates its own; leaving the flag
 * set let the next enclosing uwprot_rethrow resurrect that abandoned
 * error.  Seen 2026-09-14 as the ARexx port's EVAL of an undefined
 * function: the command layer's %CALL-GUARDED (a CATCH whose cleanup
 * THROWs) caught the unwind, and the WITH-OUTPUT-TO-STRING epilogue
 * around it re-raised the error into the outer guard (rc 20) -- and in
 * the port's own cleanup chain, killed the handler thread before it
 * could reply (tests/amiga/dev-repl-tests.lisp "guarded escape",
 * tests/amiga/arexx-tests.lisp "eval of an undefined function"). */
static CL_Obj nlx_restore_common(void *landing_anchor)
{
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    int mi;
    nlx_restore_core(nlx, landing_anchor);
    cl_pending_throw = 0;
    cl_mv_count = nlx->mv_count;
    for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
        cl_mv_values[mi] = nlx->mv_values[mi];
    return nlx->result;
}

void *cl_jit_runtime_block_alloc(CL_Obj tag)
{
    return &nlx_alloc_common(CL_NLX_BLOCK, tag)->buf;
}

void cl_jit_runtime_block_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_block_pop(void)
{
    nlx_pop_type(CL_NLX_BLOCK);
}

CL_Obj cl_jit_runtime_block_post_longjmp(void)
{
    return nlx_restore_common(CL_CAPTURE_SP());
}

void cl_jit_runtime_block_return(CL_Obj tag, CL_Obj value)
{
    int i, j, mi;
    /* cl_nlx_floor: no target below a foreign-callback boundary (thread.h) */
    for (i = cl_nlx_top - 1; i >= cl_nlx_floor; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_BLOCK &&
            cl_nlx_stack[i].tag == tag) {
            /* Check for an interposing UWPROT frame.  If present, the
             * spec requires its cleanup to run first; we longjmp to
             * the UWPROT and leave a pending-throw record that the
             * UWPROT epilogue uses to rethrow once cleanup is done.
             * The full multiple-value set must travel with the pending
             * record (mirrors vm.c OP_BLOCK_RETURN) — saving only the
             * primary silently drops secondary values across the
             * unwind-protect, e.g. (block b (uwp (return-from b
             * (values nil t)))) would return only NIL. */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                    !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                    cl_pending_throw = 1;
                    cl_pending_tag = tag;
                    cl_pending_value = value;
                    cl_pending_mv_count = cl_mv_count;
                    for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                        cl_pending_mv_values[mi] = cl_mv_values[mi];
                    cl_nlx_top = j;
                    cl_nlx_jump(&cl_nlx_stack[j]);
                }
            }
            cl_nlx_stack[i].result   = value;
            cl_nlx_stack[i].mv_count = cl_mv_count;
            for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                cl_nlx_stack[i].mv_values[mi] = cl_mv_values[mi];
            cl_nlx_top = i;
            cl_nlx_jump(&cl_nlx_stack[i]);
        }
    }
    cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s%s",
             CL_NULL_P(tag) ? "NIL" : cl_symbol_name(tag),
             cl_nlx_boundary_hint(tag, CL_NLX_BLOCK));
}

/* --- OP_CATCH / OP_UNCATCH ----------------------------------------------
 *
 * Mirrors OP_BLOCK_PUSH's JIT-inline-setjmp protocol (alloc → JSR
 * setjmp → branch on D0; commit on the zero arm, post_longjmp + push
 * result + BRA-to-landing on the non-zero arm).  Two surface
 * differences from BLOCK:
 *
 *   - The tag is a runtime value (popped from the operand stack), not
 *     a const-pool index.  catch_alloc therefore takes the tag as its
 *     sole argument, same shape as block_alloc.
 *
 *   - The pop helper looks for CL_NLX_CATCH (not CL_NLX_BLOCK), with
 *     the same search-backward leakage tolerance as block_pop.
 *
 * The longjmp arrival site is byte-identical to block_post_longjmp:
 * restore SP/FP and the dyn/handler/restart/gc/compiler marks plus
 * mv_count/mv_values from the NLX frame.  catch frames live on the
 * same NLX stack as block/tagbody/uwprot, so a throw from any
 * intervening JIT'd or VM'd code finds and longjmps to the matching
 * buf.  No special THROW opcode is needed — `throw` is a regular CL
 * builtin (bi_throw in builtins_io.c) that calls longjmp on the
 * matching buf, and the longjmp returns into whichever side captured
 * the setjmp. */

void *cl_jit_runtime_catch_alloc(CL_Obj tag)
{
    return &nlx_alloc_common(CL_NLX_CATCH, tag)->buf;
}

void cl_jit_runtime_catch_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_catch_pop(void)
{
    nlx_pop_type(CL_NLX_CATCH);
}

CL_Obj cl_jit_runtime_catch_post_longjmp(void)
{
    return nlx_restore_common(CL_CAPTURE_SP());
}

/* --- OP_UWPROT / OP_UWPOP / OP_UWRETHROW --------------------------------
 *
 * Same JIT-inline-setjmp protocol as BLOCK_PUSH.  Five helpers split the
 * work the VM does in one switch arm:
 *
 *   1. `uwprot_alloc` — reserve cl_nlx_stack[cl_nlx_top] without
 *      committing it.  Captures vm_sp/vm_fp and the dyn/handler/restart/
 *      gc-root/compiler marks so the longjmp epilogue can rewind them.
 *      code/constants/bytecode/catch_ip/offset are zero-filled: VM's
 *      OP_UWPROT longjmp arm consumes those, but our longjmp lands in
 *      JIT'd code (via the JSR setjmp the walker emits), so they stay
 *      dead for JIT-owned frames.
 *   2. JIT then emits `JSR setjmp` so the captured frame is the JIT'd
 *      function's own stack frame.
 *   3. `uwprot_commit` — bump cl_nlx_top (mirror of block_commit).
 *   4. `uwprot_pop` — normal-exit pop: search-backward to the matching
 *      UWPROT frame (tail-call leakage compatibility, same as VM's
 *      OP_UWPOP), then clear cl_pending_throw.
 *   5. `uwprot_post_longjmp` — restore the marks captured at alloc.
 *      Unlike block_post_longjmp this does NOT touch cl_mv_count or
 *      cl_mv_values: the protected form's MVs are explicitly captured
 *      by OP_MV_TO_LIST inserted by compile_unwind_protect, and the
 *      throw site may already have set up MV state we must not clobber.
 *   6. `uwprot_rethrow` — implementation of OP_UWRETHROW.  Three
 *      branches based on cl_pending_throw:
 *        0: no pending throw — nop.
 *        1: pending THROW/RETURN-FROM — find matching catch/block/
 *           tagbody.  If an interposing UWPROT is still on the stack,
 *           longjmp to it instead (its cleanup runs first); else
 *           longjmp directly to the target.  No matching target →
 *           cl_error.
 *        2: pending error (cl_error caught by us) — find next
 *           interposing UWPROT and longjmp; if none, restore
 *           cl_error_code/cl_error_msg from the pending slots and
 *           longjmp to the outermost cl_error_frames entry (matches
 *           vm.c's OP_UWRETHROW pending==2 branch, minus the
 *           cl_vm.fp/sp restore that the JIT doesn't track).
 *      Cases 1 and 2 do not return; case 0 returns to the JIT caller. */
void *cl_jit_runtime_uwprot_alloc(void)
{
    /* Snapshot the current VM frame's code/constants/bytecode so the
     * staleness check (target->code == nlx->code) used by every site
     * that scans for interposing UWPROT frames — cl_error_unwind,
     * block_return, uwprot_rethrow — treats this frame as live.  The
     * VM's OP_UWPROT does the same; without it, JIT-owned UWPROT
     * frames appear stale, throws bypass cleanup, and the
     * walker-uwp-throw test fails (cleanup count stays 0).  Everything
     * else is the shared frame setup; nlx_alloc_common zero-fills
     * code/constants/bytecode, which we overwrite here. */
    CL_Frame    *cur = (cl_vm.fp > 0) ? &cl_vm.frames[cl_vm.fp - 1] : NULL;
    CL_NLXFrame *nlx = nlx_alloc_common(CL_NLX_UWPROT, CL_NIL);
    nlx->code           = cur ? cur->code      : NULL;
    nlx->constants      = cur ? cur->constants : NULL;
    nlx->bytecode       = cur ? cur->bytecode  : CL_NIL;

    /* Park the pending-throw state and clear it, exactly as the VM's
     * OP_UWPROT arming does (vm.c), so an UNWIND-PROTECT nested inside a
     * cleanup body cannot clobber the transfer that is unwinding through
     * the enclosing one.  This was missing: the JIT parked nothing, its
     * normal-exit pop cleared cl_pending_throw outright, and a THROW (a
     * restart invoked from a handler) into a JIT'd CATCH was lost the
     * moment an interposed cleanup ran a WITH-LOCK-HELD -- the transfer
     * simply stopped, and the condition being handled escaped instead
     * (clamacs's debugger, 2026-09-11; tests/amiga/dev-repl-tests.lisp
     * "cleanup with-lock-held").  Mirrors the VM including the GC-safety
     * rule there: with no throw in flight the tag/value globals are stale
     * offsets, so NILs are parked, not copies. */
    if (cl_saved_pending_top >= cl_saved_pending_max)
        cl_error(CL_ERR_OVERFLOW, "saved-pending stack overflow");
    {
        CL_SavedPending *sp = &cl_saved_pending_stack[cl_saved_pending_top++];
        int pt = cl_pending_throw;
        sp->pending_throw = pt;
        if (pt) {
            int mi;
            sp->pending_tag      = cl_pending_tag;
            sp->pending_value    = cl_pending_value;
            sp->pending_mv_count = cl_pending_mv_count;
            for (mi = 0; mi < cl_pending_mv_count && mi < CL_MAX_MV; mi++)
                sp->pending_mv_values[mi] = cl_pending_mv_values[mi];
            sp->pending_error_code = cl_pending_error_code;
            if (pt == 2) {
                strncpy(sp->pending_error_msg, cl_pending_error_msg,
                        sizeof(sp->pending_error_msg) - 1);
                sp->pending_error_msg[sizeof(sp->pending_error_msg) - 1] = '\0';
            } else {
                sp->pending_error_msg[0] = '\0';
            }
            cl_pending_throw      = 0;
            cl_pending_tag        = CL_NIL;
            cl_pending_value      = CL_NIL;
            cl_pending_mv_count   = 0;
            cl_pending_error_code = 0;
            cl_pending_error_msg[0] = '\0';
        } else {
            sp->pending_tag      = CL_NIL;
            sp->pending_value    = CL_NIL;
            sp->pending_mv_count = 0;
            sp->pending_error_code = 0;
            sp->pending_error_msg[0] = '\0';
        }
        sp->entered_via_longjmp = 0;   /* the landing sets it */
    }
    /* The record is part of this frame's dynamic extent: a transfer to
     * this frame must find it, not drop it (nlx_alloc_common took the mark
     * before the push). */
    nlx->saved_pending_mark = cl_saved_pending_top;
    return &nlx->buf;
}

void cl_jit_runtime_uwprot_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_uwprot_pop(void)
{
    /* Normal-exit pop of the NLX frame only.  cl_pending_throw is NOT
     * cleared here: the arming parked and cleared it, and uwprot_rethrow
     * restores it after the cleanup -- clearing it here is what lost an
     * enclosing transfer whenever a nested UNWIND-PROTECT in a cleanup
     * body exited normally (mirrors the VM's OP_UWPOP). */
    nlx_pop_type(CL_NLX_UWPROT);
}

void cl_jit_runtime_uwprot_post_longjmp(void)
{
    /* cl_nlx_top has been set to this frame's index by whatever throw
     * site triggered the longjmp (cl_error, block_return, throw, …).
     * Read the frame's saved marks and restore. */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    nlx_restore_core(nlx, CL_CAPTURE_SP());
    /* Pass-through, not a target: the transfer stays in flight, so
     * cl_pending_throw stays set.  Record it in the slot the arming parked
     * (nlx_restore_core put cl_saved_pending_top back to just past it),
     * so uwprot_rethrow can re-initiate it after the cleanup even when the
     * cleanup body's own NLX activity cleared the globals (mirrors the VM's
     * UWPROT landing). */
    if (cl_saved_pending_top > 0) {
        CL_SavedPending *sp = &cl_saved_pending_stack[cl_saved_pending_top - 1];
        int mi;
        sp->pending_throw    = cl_pending_throw;
        sp->pending_tag      = cl_pending_tag;
        sp->pending_value    = cl_pending_value;
        sp->pending_mv_count = cl_pending_mv_count;
        for (mi = 0; mi < cl_pending_mv_count && mi < CL_MAX_MV; mi++)
            sp->pending_mv_values[mi] = cl_pending_mv_values[mi];
        sp->pending_error_code = cl_pending_error_code;
        strncpy(sp->pending_error_msg, cl_pending_error_msg,
                sizeof(sp->pending_error_msg) - 1);
        sp->pending_error_msg[sizeof(sp->pending_error_msg) - 1] = '\0';
        sp->entered_via_longjmp = 1;
    }
    /* Intentionally don't touch cl_mv_count / cl_mv_values: the throw
     * site may have arranged its own MV state that the cleanup forms must
     * observe.  The protected form's values were never produced on this
     * path, so park an EMPTY record for the OP_MV_RESTORE after the
     * cleanup (mirrors the VM's UWPROT landing). */
    cl_jit_runtime_mv_save_empty();
}

/* Backing for OP_MV_SAVE (vm.h CL_MV_SAVE_SIZE): park PRIMARY plus the rest
 * of the MV buffer as one record on the per-thread save stack.  Mirrors the
 * VM opcode exactly.  Non-allocating. */
void cl_jit_runtime_mv_save(CL_Obj primary)
{
    CL_Thread *t = CT;
    int n = t->mv_count;
    int i;
    if (n < 0) n = 0;
    if (n > CL_MAX_MV) n = CL_MAX_MV;
    if (t->mv_save_top + n + 1 > CL_MV_SAVE_SIZE)
        cl_error(CL_ERR_OVERFLOW, "UNWIND-PROTECT value-save stack overflow");
    if (n > 0) {
        t->mv_save_buf[t->mv_save_top++] = primary;
        for (i = 1; i < n; i++)
            t->mv_save_buf[t->mv_save_top++] = t->mv_values[i];
    }
    t->mv_save_buf[t->mv_save_top++] = CL_MAKE_FIXNUM(n);
}

/* The UWPROT longjmp arm's record: zero values, so the OP_MV_RESTORE after
 * the cleanup finds something to pop (it only runs when the pending transfer
 * was parked for an enclosing cleanup rather than re-thrown). */
void cl_jit_runtime_mv_save_empty(void)
{
    CL_Thread *t = CT;
    if (t->mv_save_top + 1 > CL_MV_SAVE_SIZE)
        cl_error(CL_ERR_OVERFLOW, "UNWIND-PROTECT value-save stack overflow");
    t->mv_save_buf[t->mv_save_top++] = CL_MAKE_FIXNUM(0);
}

/* Backing for OP_MV_RESTORE: pop the record back into the MV buffer and
 * return the primary (NIL for zero values).  Non-allocating. */
CL_Obj cl_jit_runtime_mv_restore(void)
{
    CL_Thread *t = CT;
    int n, i;
    if (t->mv_save_top < 1)
        cl_error(CL_ERR_GENERAL, "OP_MV_RESTORE: value-save stack underflow");
    n = (int)CL_FIXNUM_VAL(t->mv_save_buf[--t->mv_save_top]);
    if (n < 0 || n > CL_MAX_MV || t->mv_save_top < n)
        cl_error(CL_ERR_GENERAL, "OP_MV_RESTORE: corrupt value-save record");
    t->mv_save_top -= n;
    for (i = 0; i < n; i++)
        t->mv_values[i] = t->mv_save_buf[t->mv_save_top + i];
    t->mv_count = n;
    return n > 0 ? t->mv_values[0] : CL_NIL;
}

void cl_jit_runtime_uwprot_rethrow(void)
{
    int p;
    /* Pop the record the arming parked and decide whether to re-initiate,
     * with the VM's OP_UWRETHROW rules: a transfer started inside the
     * cleanup body itself always goes on; otherwise the parked one is put
     * back into the globals, and goes on only if this frame's landing had
     * fired for it -- a frame entered normally leaves it parked for the
     * enclosing cleanup to find. */
    {
        int rethrow_active = (cl_pending_throw != 0);
        int should_rethrow;
        CL_SavedPending saved;
        if (cl_saved_pending_top > 0) {
            saved = cl_saved_pending_stack[--cl_saved_pending_top];
        } else {
            saved.pending_throw = 0;
            saved.pending_tag = CL_NIL;
            saved.pending_value = CL_NIL;
            saved.pending_mv_count = 0;
            saved.pending_error_code = 0;
            saved.pending_error_msg[0] = '\0';
            saved.entered_via_longjmp = 0;
        }
        if (cl_pending_throw == 0) {
            int mi;
            cl_pending_throw      = saved.pending_throw;
            cl_pending_tag        = saved.pending_tag;
            cl_pending_value      = saved.pending_value;
            cl_pending_mv_count   = saved.pending_mv_count;
            for (mi = 0; mi < saved.pending_mv_count && mi < CL_MAX_MV; mi++)
                cl_pending_mv_values[mi] = saved.pending_mv_values[mi];
            cl_pending_error_code = saved.pending_error_code;
            strncpy(cl_pending_error_msg, saved.pending_error_msg,
                    sizeof(cl_pending_error_msg) - 1);
            cl_pending_error_msg[sizeof(cl_pending_error_msg) - 1] = '\0';
        }
        should_rethrow = rethrow_active ||
                         (saved.entered_via_longjmp && cl_pending_throw != 0);
        if (!should_rethrow) return;
    }
    p = cl_pending_throw;
    if (p == 0) return;

    if (p == 1) {
        CL_Obj ptag = cl_pending_tag;
        CL_Obj pval = cl_pending_value;
        int i, j, mi;
        for (i = cl_nlx_top - 1; i >= cl_nlx_floor; i--) {
            if ((cl_nlx_stack[i].type == CL_NLX_CATCH ||
                 cl_nlx_stack[i].type == CL_NLX_BLOCK ||
                 cl_nlx_stack[i].type == CL_NLX_TAGBODY) &&
                cl_nlx_stack[i].tag == ptag) {
                for (j = cl_nlx_top - 1; j > i; j--) {
                    if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                        !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                        cl_nlx_top = j;
                        cl_nlx_jump(&cl_nlx_stack[j]);
                    }
                }
                cl_pending_throw = 0;
                cl_nlx_stack[i].result = pval;
                /* Carry the full multiple-value set saved at the throw
                 * site into the target frame so block_post_longjmp /
                 * catch_post_longjmp restore all of them (mirrors vm.c
                 * OP_UWRETHROW); without this the frame keeps its
                 * mv_count=1 baseline and secondary values are lost. */
                cl_nlx_stack[i].mv_count = cl_pending_mv_count;
                for (mi = 0; mi < cl_pending_mv_count && mi < CL_MAX_MV; mi++)
                    cl_nlx_stack[i].mv_values[mi] = cl_pending_mv_values[mi];
                cl_nlx_top = i;
                cl_nlx_jump(&cl_nlx_stack[i]);
            }
        }
        cl_pending_throw = 0;
        cl_error(CL_ERR_GENERAL, "No catch for tag during re-throw");
    } else if (p == 3) {
        /* Pending HANDLER-CASE clause transfer (cl_handler_case_transfer):
         * target frame index in cl_pending_tag, condition in
         * cl_pending_value.  Mirrors vm.c OP_UWRETHROW. */
        int i = (int)CL_FIXNUM_VAL(cl_pending_tag);
        int j;
        if (i < cl_nlx_floor || i >= cl_nlx_top ||
            cl_nlx_stack[i].type != CL_NLX_HANDLER_CASE) {
            cl_pending_throw = 0;
            cl_error(CL_ERR_GENERAL,
                     "HANDLER-CASE frame vanished during re-throw");
        }
        for (j = cl_nlx_top - 1; j > i; j--) {
            if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                cl_nlx_top = j;
                cl_nlx_jump(&cl_nlx_stack[j]);
            }
        }
        cl_pending_throw = 0;
        cl_nlx_stack[i].result = cl_pending_value;
        cl_nlx_stack[i].mv_count = 1;
        cl_nlx_stack[i].mv_values[0] = cl_pending_value;
        cl_nlx_top = i;
        cl_nlx_jump(&cl_nlx_stack[i]);
    } else {
        /* p == 2: pending error.  Search for next UWPROT, else replay
         * the original error through cl_error_frames.  This skips the
         * cl_vm.fp/sp reset the VM's OP_UWRETHROW does — the JIT
         * doesn't keep per-helper base_fp, and the outermost
         * cl_error_frames longjmp target restores VM state itself. */
        int i;
        for (i = cl_nlx_top - 1; i >= cl_nlx_floor; i--) {
            if (cl_nlx_stack[i].type == CL_NLX_UWPROT &&
                !jit_nlx_frame_is_stale(&cl_nlx_stack[i])) {
                cl_nlx_top = i;
                cl_nlx_jump(&cl_nlx_stack[i]);
            }
        }
        {
            /* Propagate through cl_error_frame_longjmp so the top error
             * frame's snapshots (gc roots, jit depth, FASL readers,
             * compiler chain, handler/restart tops) are restored — the
             * bare longjmp here used to skip all of them, leaving
             * gc_root_count dangling into unwound C stack frames. */
            int err_code = cl_pending_error_code;
            cl_pending_throw = 0;
            cl_error_code = err_code;
            strncpy(cl_error_msg, cl_pending_error_msg,
                    sizeof(cl_error_msg) - 1);
            cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';
            cl_error_frame_longjmp(err_code);
        }
    }
}

/* Backing for OP_MV_TO_LIST.  Mirrors vm.c exactly (including the
 * cl_mv_count == 0 with non-NIL primary quirk that preserves the
 * primary as a single-element list through the unwind-protect MV
 * round-trip).  Allocates one or more conses; the conservative scan
 * roots the JIT'd caller's cached operand-stack values across the
 * call.  Resets cl_mv_count to 1 like the VM op so subsequent ops see
 * single-value semantics. */
CL_Obj cl_jit_runtime_mv_to_list(CL_Obj primary)
{
    CL_Obj list = CL_NIL;
    if (cl_mv_count == 0) {
        if (!CL_NULL_P(primary))
            list = cl_cons(primary, CL_NIL);
    } else if (cl_mv_count == 1) {
        list = cl_cons(primary, CL_NIL);
    } else {
        int i;
        for (i = cl_mv_count - 1; i >= 0; i--)
            list = cl_cons(cl_mv_values[i], list);
    }
    cl_mv_count = 1;
    return list;
}

/* --- OP_HANDLER_PUSH / OP_HANDLER_POP / OP_RESTART_PUSH / OP_RESTART_POP ---
 *
 * Pure push/pop on the per-thread handler / restart binding stacks.
 * Unlike OP_BLOCK_PUSH / OP_UWPROT these don't capture a setjmp frame
 * — they just register a binding that cl_signal_condition (handler)
 * or find-restart (restart) will walk later.  No JIT-side longjmp
 * choreography needed; the helpers are byte-for-byte mirrors of the
 * VM cases in core/vm.c.
 *
 * Allocation: the overflow guard calls cl_error which allocates a
 * condition.  The walker cache-flushes before the JSR (same as
 * OP_DYNBIND) so cached operand-stack values stay rooted on the
 * conservatively-scanned m68k stack across the (rare) error path. */

void cl_jit_runtime_handler_push(CL_Obj type_sym, CL_Obj handler)
{
    if (cl_handler_top >= CL_MAX_HANDLER_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");
    cl_handler_stack[cl_handler_top].type_name = type_sym;
    cl_handler_stack[cl_handler_top].handler = handler;
    cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
    /* Mirror the VM's OP_HANDLER_PUSH (core/vm.c): a freshly pushed handler
     * must be ACTIVE, else cl_signal_condition's band gate (added when CLHS
     * 9.1.4 handler-disabling landed) skips it and the condition escapes
     * uncaught.  A reused stack slot can carry a stale disabled bit from a
     * prior band-disable, so setting the bit here is mandatory. */
    cl_handler_active_mask |= ((uint64_t)1 << cl_handler_top);
    cl_handler_top++;
}

void cl_jit_runtime_handler_pop(uint32_t count)
{
    int old_top = cl_handler_top;
    cl_handler_top -= (int)count;
    if (cl_handler_top < 0) cl_handler_top = 0;
    cl_handler_active_mask &= ~CL_HANDLER_BAND_MASK(cl_handler_top, old_top);
}

/* --- OP_HANDLER_CASE_PUSH / OP_HANDLER_CASE_POP --------------------------
 *
 * Same JIT-inline-setjmp protocol as BLOCK_PUSH (alloc → JSR setjmp →
 * commit on the zero arm).  The frame is a CL_NLX_HANDLER_CASE whose tag
 * is the clause TYPE list (a constant of the bytecode); commit pushes the
 * frame AND one handler binding per clause whose "handler" is the clause
 * index as a fixnum, pointing back at the frame — exactly what the VM's
 * OP_HANDLER_CASE_PUSH does.  When a clause matches, cl_signal_condition
 * → cl_handler_case_transfer stores the condition as the frame's result,
 * sets hc_clause and lands in our buf through cl_nlx_jump (landing ==
 * NULL, so the frame's own setjmp is the target; interposing cleanups run
 * first as pending kind 3, re-initiated by uwprot_rethrow).  The walker's
 * longjmp arm pushes the condition and branches to the matched clause's
 * OP_JMP in the bytecode landing table (clause k at landing + 5 * k).
 *
 *   handler_case_alloc(types)   — reserve the frame, check both stacks.
 *   handler_case_commit()       — cl_nlx_top++, push the clause bindings.
 *   handler_case_pop()          — normal exit: drop bindings and frame.
 *   handler_case_post_longjmp() — restore the marks, return the condition.
 *   handler_case_clause()       — the matched clause index (after the above).
 */

static int handler_case_clause_count(CL_Obj types)
{
    int n = 0;
    while (CL_CONS_P(types)) { n++; types = cl_cdr(types); }
    return n;
}

void *cl_jit_runtime_handler_case_alloc(CL_Obj types)
{
    CL_NLXFrame *nlx;
    /* Both capacity checks BEFORE the frame is committed, so an overflow
     * error leaves nothing half-pushed (mirrors the VM opcode). */
    if (cl_handler_top + handler_case_clause_count(types) > CL_MAX_HANDLER_BINDINGS)
        cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");
    nlx = nlx_alloc_common(CL_NLX_HANDLER_CASE, types);
    nlx->hc_clause  = 0;
    /* error_mark and saved_pending_mark come from nlx_alloc_common now, for
     * every JIT frame kind. */
    return &nlx->buf;
}

void cl_jit_runtime_handler_case_commit(void)
{
    int ni = cl_nlx_top;
    CL_Obj types = cl_nlx_stack[ni].tag;
    int n = handler_case_clause_count(types);
    int k;
    cl_nlx_top++;
    /* Clause 0 is pushed LAST so it is the innermost binding:
     * cl_signal_condition walks the stack top-down and CLHS 9.1.4 wants
     * the textually first matching clause (vm.c OP_HANDLER_CASE_PUSH). */
    for (k = n - 1; k >= 0; k--) {
        CL_Obj t = types;
        int j;
        for (j = 0; j < k; j++) t = cl_cdr(t);
        cl_handler_stack[cl_handler_top].type_name    = cl_car(t);
        cl_handler_stack[cl_handler_top].handler      = CL_MAKE_FIXNUM(k);
        cl_handler_stack[cl_handler_top].nlx_index    = ni;
        cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
        cl_handler_active_mask |= ((uint64_t)1 << cl_handler_top);
        cl_handler_top++;
    }
}

void cl_jit_runtime_handler_case_pop(void)
{
    /* Normal exit: search backward for the frame (a tail call inside the
     * form may have leaked an intervening NLX frame — same tolerance as
     * nlx_pop_type), drop its clause bindings, then the frame.  The MV
     * state is the form's and is left alone. */
    int hi;
    for (hi = cl_nlx_top - 1; hi >= 0; hi--) {
        if (cl_nlx_stack[hi].type == CL_NLX_HANDLER_CASE) {
            int old_top = cl_handler_top;
            cl_handler_top = cl_nlx_stack[hi].handler_mark;
            if (cl_handler_top < old_top)
                cl_handler_active_mask &=
                    ~CL_HANDLER_BAND_MASK(cl_handler_top, old_top);
            cl_nlx_top = hi;
            return;
        }
    }
    if (cl_nlx_top > 0) cl_nlx_top--;
}

CL_Obj cl_jit_runtime_handler_case_post_longjmp(void)
{
    /* cl_nlx_top is this frame's index (set by cl_handler_case_transfer
     * or uwprot_rethrow's kind-3 arm).  The clause bindings are gone once
     * nlx_restore_core has restored handler_top to the frame's mark. */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    CL_Obj cond;
    nlx_restore_core(nlx, CL_CAPTURE_SP());   /* error_mark, pending mark included */
    cond = nlx->result;
    cl_pending_throw = 0;
    cl_mv_count = 1;
    cl_mv_values[0] = cond;
    return cond;
}

uint32_t cl_jit_runtime_handler_case_clause(void)
{
    return (uint32_t)cl_nlx_stack[cl_nlx_top].hc_clause;
}

void cl_jit_runtime_restart_pop(uint32_t count)
{
    cl_restart_top -= (int)count;
    if (cl_restart_top < 0) cl_restart_top = 0;
}

/* --- OP_TAGBODY_PUSH / OP_TAGBODY_POP / OP_TAGBODY_GO ----------------------
 *
 * Mirrors OP_BLOCK_PUSH's JIT-inline-setjmp protocol with two twists:
 *
 *   1. The longjmp arrival path *re-arms* the NLX frame (bumps
 *      cl_nlx_top after restoring marks) so the same tagbody stays
 *      live for repeated GO from inner closures.  See vm.c OP_TAGBODY_
 *      PUSH's `else` arm — cl_nlx_top++ runs both in the setjmp==0
 *      path and the setjmp!=0 path.
 *
 *   2. The longjmp arrival path returns the *tag index* (a small
 *      fixnum picked by the compiler at TAGBODY_PUSH time) so the
 *      dispatch shim the compiler emits right after PUSH can route
 *      to the right tag body via JTRUE.  The walker emits a
 *      MOVE.L D0,-(A7) right after the post_longjmp helper to land
 *      that index on the operand stack.
 *
 * GO can cross into a tagbody set up by VM code (the parent
 * function's body executed by the interpreter) or by JIT code (the
 * parent was itself walker-compiled).  Both are reachable because
 * the longjmp restores the captured setjmp/stack frame regardless
 * of who emitted it.  Helpers below are byte-for-byte mirrors of
 * vm.c::OP_TAGBODY_* so the behavior matches across VM/JIT mixes. */

void *cl_jit_runtime_tagbody_alloc(CL_Obj tagbody_id)
{
    return &nlx_alloc_common(CL_NLX_TAGBODY, tagbody_id)->buf;
}

void cl_jit_runtime_tagbody_commit(void)
{
    cl_nlx_top++;
}

void cl_jit_runtime_tagbody_pop(void)
{
    nlx_pop_type(CL_NLX_TAGBODY);
}

CL_Obj cl_jit_runtime_tagbody_post_longjmp(void)
{
    /* cl_nlx_top was set to this frame's index by GO before the
     * longjmp; read the saved marks and restore. */
    CL_NLXFrame *nlx = &cl_nlx_stack[cl_nlx_top];
    CL_Obj tag_index;

    nlx_restore_core(nlx, CL_CAPTURE_SP());
    /* Target of a completed transfer: nothing stays pending (the VM's
     * TAGBODY landing does the same; see nlx_restore_common). */
    cl_pending_throw = 0;
    cl_mv_count = 1;

    tag_index = nlx->result;
    /* Re-arm: a tagbody stays usable for repeated GO until the
     * matching OP_TAGBODY_POP runs.  Bumping cl_nlx_top here mirrors
     * vm.c OP_TAGBODY_PUSH's setjmp!=0 arm. */
    cl_nlx_top++;
    return tag_index;
}

void cl_jit_runtime_tagbody_go(CL_Obj tagbody_id, CL_Obj tag_index)
{
    int i, j, mi;
    for (i = cl_nlx_top - 1; i >= cl_nlx_floor; i--) {
        if (cl_nlx_stack[i].type == CL_NLX_TAGBODY &&
            cl_nlx_stack[i].tag == tagbody_id) {
            /* UWPROT interposition: if a non-stale UWPROT frame
             * sits between top and the target, divert there and
             * record the pending throw so cleanup runs before the
             * actual transfer.  GO carries no user values, but the
             * pending-mv snapshot must still be set (mirrors vm.c
             * OP_GO) so the rethrow consumer doesn't propagate a
             * stale mv_count from an earlier RETURN-FROM. */
            for (j = cl_nlx_top - 1; j > i; j--) {
                if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                    !jit_nlx_frame_is_stale(&cl_nlx_stack[j])) {
                    cl_pending_throw = 1;
                    cl_pending_tag = tagbody_id;
                    cl_pending_value = tag_index;
                    cl_pending_mv_count = cl_mv_count;
                    for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                        cl_pending_mv_values[mi] = cl_mv_values[mi];
                    cl_nlx_top = j;
                    cl_nlx_jump(&cl_nlx_stack[j]);
                }
            }
            cl_nlx_stack[i].result = tag_index;
            cl_nlx_top = i;
            cl_nlx_jump(&cl_nlx_stack[i]);
        }
    }
    cl_error(CL_ERR_GENERAL, "GO: tagbody frame not found%s",
             cl_nlx_boundary_hint(tagbody_id, CL_NLX_TAGBODY));
}

#endif /* JIT_M68K || JIT_A64 */
