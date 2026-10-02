/* runtime_vmstack.h -- helpers of a walker whose frame lives in cl_vm.stack
 * (runtime_vmstack.c).  The CPU-neutral ones are in runtime.h. */

#ifndef CL_JIT_RUNTIME_VMSTACK_H
#define CL_JIT_RUNTIME_VMSTACK_H

#ifdef JIT_A64

#include "core/types.h"

/* Its operand stack is cl_vm.stack, so these take pointers into it.
 *   stack_overflow - the prologue's frame does not fit: "VM stack overflow".
 *   call           - OP_CALL, the callee under the NARGS arguments below
 *                    TOP.
 *   call_global    - the _GLOBAL calls, the callee the function of *SYMREF
 *                    (a word of bc->constants).
 *                    Both take the call site's cell (the miss path of its
 *                    direct-call hit path; runtime_vmstack.c), or NULL.
 *   is_self        - the self tail call's guard (same bytecode, nothing traced).
 *   tail           - any other tail call (constant space between natives).
 *   cons/list/push_local - the interpreter's cl_cons_rooted forms. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void   cl_jit_vmstack_stack_overflow(void);
struct CL_Thread_s;
CL_Obj cl_jit_vmstack_call(struct CL_Thread_s *thr, CL_Obj *top,
                           uint32_t nargs, uint64_t *site);
CL_Obj cl_jit_vmstack_call_global(struct CL_Thread_s *thr, CL_Obj *top,
                                  uint32_t nargs, const CL_Obj *symref,
                                  uint64_t *site);
int    cl_jit_vmstack_is_self(CL_Obj func, CL_Obj entered);
/* A non-self tail call: to a native callee, a frame-reusing handoff to
 * cl_jit_invoke (CL_Thread.jit_tail_pending); to anything else, a call. */
CL_Obj cl_jit_vmstack_tail(struct CL_Thread_s *thr, CL_Obj *top,
                               uint32_t nargs, CL_Obj *bp, const CL_Obj *symref);
/* FUNC's bytecode when native code may enter it with NARGS, else NULL. */
CL_Bytecode *cl_jit_vmstack_native_callee(CL_Obj func, uint32_t nargs);
/* Does a call with NARGS arguments fit BC's lambda list?  The interpreter's
 * OP_CALL bounds (vm.c): at least the required ones, at most the required
 * and optional ones -- or 255 with &rest or &key.  A call that does not fit
 * takes the interpreter, which signals the arity error. */
static inline int cl_jit_vmstack_fits(const CL_Bytecode *bc, uint32_t nargs)
{
    uint32_t arity = bc->arity & 0x7FFFu;
    uint32_t max = ((bc->arity & 0x8000u) || (bc->flags & 1u))
                   ? 255u : arity + bc->n_optional;
    return nargs >= arity && nargs <= max;
}
CL_Obj cl_jit_vmstack_cons(CL_Obj *pair);
CL_Obj cl_jit_vmstack_list(CL_Obj *base, uint32_t n);
CL_Obj cl_jit_vmstack_push_local(CL_Obj *item, CL_Obj *slot);
/* The &rest / &key prologue (the arguments at BP, the function value FUNC,
 * before the frame is set up); OP_CLOSURE (the template a word of
 * bc->constants, the captures on the VM stack); OP_RESTART_PUSH (the name a
 * word of bc->constants, the five operands at OPS). */
void   cl_jit_vmstack_ll_prologue(uint32_t nargs, CL_Obj *bp, CL_Obj func);
CL_Obj cl_jit_vmstack_make_closure(const CL_Obj *tmpl_ref, uint32_t n,
                                   CL_Obj *values);
void   cl_jit_vmstack_restart_push(const CL_Obj *name_ref, CL_Obj *ops);
#endif /* JIT_A64 */

#endif /* CL_JIT_RUNTIME_VMSTACK_H */
