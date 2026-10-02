/* runtime_vmstack.h -- helpers of a walker whose frame lives in cl_vm.stack
 * (runtime_vmstack.c).  The CPU-neutral ones are in runtime.h. */

#ifndef CL_JIT_RUNTIME_VMSTACK_H
#define CL_JIT_RUNTIME_VMSTACK_H

#ifdef JIT_A64

#include "core/types.h"

/* Its operand stack is cl_vm.stack, so these take pointers into it.
 *   stack_overflow - the prologue's frame does not fit: "VM stack overflow".
 *   call           - OP_CALL/OP_TAILCALL, the callee under the NARGS
 *                    arguments below TOP.
 *   call_global    - the _GLOBAL calls, the callee the function of *SYMREF
 *                    (a word of bc->constants).
 *   is_self        - the self tail call's guard (same bytecode, nothing traced).
 *   tail           - any other tail call (constant space between natives).
 *   cons/list/push_local - the interpreter's cl_cons_rooted forms. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void   cl_jit_vmstack_stack_overflow(void);
struct CL_Thread_s;
CL_Obj cl_jit_vmstack_call(struct CL_Thread_s *thr, CL_Obj *top,
                               uint32_t nargs);
CL_Obj cl_jit_vmstack_call_global(struct CL_Thread_s *thr, CL_Obj *top,
                                      uint32_t nargs, const CL_Obj *symref);
int    cl_jit_vmstack_is_self(CL_Obj func, CL_Obj entered);
/* A non-self tail call: to a native callee, a frame-reusing handoff to
 * cl_jit_invoke (CL_Thread.jit_tail_pending); to anything else, a call. */
CL_Obj cl_jit_vmstack_tail(struct CL_Thread_s *thr, CL_Obj *top,
                               uint32_t nargs, CL_Obj *bp, const CL_Obj *symref);
/* FUNC's bytecode when native code may enter it with NARGS, else NULL. */
CL_Bytecode *cl_jit_vmstack_native_callee(CL_Obj func, uint32_t nargs);
CL_Obj cl_jit_vmstack_cons(CL_Obj *pair);
CL_Obj cl_jit_vmstack_list(CL_Obj *base, uint32_t n);
CL_Obj cl_jit_vmstack_push_local(CL_Obj *item, CL_Obj *slot);
#endif /* JIT_A64 */

#endif /* CL_JIT_RUNTIME_VMSTACK_H */
