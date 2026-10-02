/* runtime_vmstack.c -- runtime helpers of a walker whose native frame lives
 * in cl_vm.stack (jit_a64.c today; specs/native-backend-a64.md).  Nothing
 * here is CPU-specific: a PPC or x86-64 walker built on the same frame
 * design calls the same entry points.
 */

#ifdef JIT_A64

#include "jit/runtime.h"
#include "jit/runtime_vmstack.h"
#include "core/types.h"
#include "core/error.h"
#include "core/thread.h"
#include "core/vm.h"
#include "core/mem.h"
#include "core/builtins.h"   /* cl_ffi_stub_call */
#include "jit/jit.h"         /* cl_jit_invoke */

extern CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs);

/* --- The AArch64 walker's own entry points (specs/native-backend-a64.md) --
 *
 * AArch64 native code keeps its locals and operand stack in cl_vm.stack,
 * the arguments of a call in call order right under the operand-stack top,
 * and writes cl_vm.sp before every helper call ("Rule 1": every live value
 * is below sp, a GC root the collectors forward in place).  So these
 * helpers take pointers into that stack, never a CL_Obj that a collection
 * inside them could leave stale, and the arguments of a call need no copy:
 * they already sit where cl_jit_invoke, the builtins and the stub frame
 * expect them. */

/* The prologue's overflow check failed: the frame (locals, the function
 * slot, the deepest operand stack) does not fit the VM stack. */
void cl_jit_vmstack_stack_overflow(void)
{
    cl_error(CL_ERR_OVERFLOW, "VM stack overflow");
}

/* Dispatch FUNC with the NARGS arguments at ARGS (on the VM stack, below
 * thr->vm.sp, which the caller set to ARGS + NARGS).  The arms of
 * jit_dispatch: a builtin or an FFI stub is called directly, a native
 * callee the call fits enters through cl_jit_invoke (one C-stack probe:
 * native frames nest on the C stack), any other bytecode/closure takes the
 * stub frame (whose OP_CALL counts it hot, and signals an arity mismatch
 * with OP_CALL's own diagnostic), and everything else -- a generic
 * function, a symbol, a traced callee -- cl_vm_apply. */
static CL_Obj vmstack_dispatch(CL_Thread *thr, CL_Obj func, CL_Obj *args,
                           uint32_t nargs)
{
    uint32_t ftype = 0xFFu;

    if (CL_HEAP_P(func) && func < cl_heap.arena_size)
        ftype = CL_HDR_TYPE(CL_OBJ_TO_PTR(func));
    if (thr->trace_count == 0) {
        if (ftype == TYPE_FUNCTION)
            return cl_vm_call_builtin(thr, (CL_Function *)CL_OBJ_TO_PTR(func),
                                      args, (int)nargs);
        if (ftype == TYPE_FFI_STUB) {
            CL_Obj result = cl_ffi_stub_call(func, args, (int)nargs);
            thr->mv_count = 1;
            thr->mv_values[0] = result;
            return result;
        }
        if (ftype == TYPE_BYTECODE || ftype == TYPE_CLOSURE) {
            CL_Bytecode *bc = cl_jit_bytecode_of(func, ftype);
            if (bc != NULL && bc->native_code != NULL) {
                uint32_t arity = bc->arity & 0x7FFF;
                if ((bc->arity & 0x8000) == 0 && bc->n_optional == 0 &&
                    ((bc->flags & 1) ? nargs >= arity : nargs == arity)) {
                    cl_check_c_stack("a native call");
                    return cl_jit_invoke(func, bc, (int)nargs);
                }
            }
            if (bc != NULL)
                return cl_vm_call_bytecode(thr, func, args, (int)nargs, 0);
        }
    }
    return cl_vm_apply(func, args, (int)nargs);
}

/* Poll first, as the interpreter's OP_CALL does (VM_SAFEPOINT): the
 * collection it may run moves objects, so nothing is read before it. */
static void vmstack_call_poll(CL_Thread *thr)
{
    if (thr->gc_requested) cl_gc_safepoint();
    if (thr->interrupt_pending) cl_thread_handle_interrupt(thr);
}

/* OP_CALL / OP_TAILCALL: TOP is the operand-stack top, the function value
 * at TOP[-NARGS-1] under the arguments. */
CL_Obj cl_jit_vmstack_call(CL_Thread *thr, CL_Obj *top, uint32_t nargs)
{
    vmstack_call_poll(thr);
    return vmstack_dispatch(thr, top[-(int32_t)nargs - 1], top - nargs, nargs);
}

/* OP_CALL_GLOBAL and the fused heads: the callee is the function of the
 * symbol in *SYMREF -- a word of bc->constants, read after the poll. */
CL_Obj cl_jit_vmstack_call_global(CL_Thread *thr, CL_Obj *top,
                                      uint32_t nargs, const CL_Obj *symref)
{
    CL_Obj func;
    vmstack_call_poll(thr);
    func = cl_jit_runtime_fload(*symref);   /* UNDEFINED-FUNCTION as the VM */
    return vmstack_dispatch(thr, func, top - nargs, nargs);
}

/* The self tail call's guard: does FUNC run the same bytecode as ENTERED,
 * the function value this frame was entered with?  Then the walker copies
 * the arguments over its own and branches back to the body, storing FUNC
 * as the frame's function (a different closure over the same code brings
 * its own upvalues).  Never while anything is traced: the call must reach
 * the trace output. */
int cl_jit_vmstack_is_self(CL_Obj func, CL_Obj entered)
{
    uint32_t ft, et;
    CL_Thread *thr = CT;
    if (thr->trace_count != 0 || cl_traced_function_count != 0) return 0;
    if (!CL_HEAP_P(func) || func >= cl_heap.arena_size) return 0;
    if (!CL_HEAP_P(entered) || entered >= cl_heap.arena_size) return 0;
    ft = CL_HDR_TYPE(CL_OBJ_TO_PTR(func));
    et = CL_HDR_TYPE(CL_OBJ_TO_PTR(entered));
    if ((ft != TYPE_BYTECODE && ft != TYPE_CLOSURE) ||
        (et != TYPE_BYTECODE && et != TYPE_CLOSURE))
        return 0;
    return cl_jit_bytecode_of(func, ft) == cl_jit_bytecode_of(entered, et);
}

/* FUNC's bytecode when native code may enter it directly with NARGS
 * arguments -- it carries native code, the call fits its lambda list,
 * nothing is traced -- else NULL. */
CL_Bytecode *cl_jit_vmstack_native_callee(CL_Obj func, uint32_t nargs)
{
    uint32_t ftype, arity;
    CL_Bytecode *bc;
    CL_Thread *thr = CT;
    if (thr->trace_count != 0 || cl_traced_function_count != 0) return NULL;
    if (!CL_HEAP_P(func) || func >= cl_heap.arena_size) return NULL;
    ftype = CL_HDR_TYPE(CL_OBJ_TO_PTR(func));
    if (ftype != TYPE_BYTECODE && ftype != TYPE_CLOSURE) return NULL;
    bc = cl_jit_bytecode_of(func, ftype);
    if (bc == NULL || bc->native_code == NULL) return NULL;
    arity = bc->arity & 0x7FFF;
    if ((bc->arity & 0x8000) || bc->n_optional != 0 ||
        !((bc->flags & 1) ? nargs >= arity : nargs == arity))
        return NULL;
    return bc;
}

/* A tail call that is not a self call (OP_TAILCALL with SYMREF NULL, the
 * callee under the arguments; OP_TAILCALL_GLOBAL, the callee the function
 * of *SYMREF).  To a native callee it does not call: it moves the NARGS
 * arguments down to BP (the frame is dead), puts the callee above them,
 * sets jit_tail_pending and returns -- the native code returns at once and
 * cl_jit_invoke enters the callee from the same base, so a chain of tail
 * calls between native functions runs in constant space.  Any other
 * callee is called, and its value is the native function's. */
CL_Obj cl_jit_vmstack_tail(CL_Thread *thr, CL_Obj *top, uint32_t nargs,
                               CL_Obj *bp, const CL_Obj *symref)
{
    CL_Obj func;
    CL_Obj *args = top - nargs;
    uint32_t i;
    vmstack_call_poll(thr);
    func = symref ? cl_jit_runtime_fload(*symref) : top[-(int32_t)nargs - 1];
    if (cl_jit_vmstack_native_callee(func, nargs) == NULL)
        return vmstack_dispatch(thr, func, args, nargs);
    for (i = 0; i < nargs; i++)          /* args lie above bp: ascending */
        bp[i] = args[i];
    bp[nargs] = func;
    thr->vm.sp = (int)(bp - thr->vm.stack) + (int)nargs + 1;
    thr->jit_tail_pending = nargs + 1;
    return CL_NIL;
}

/* OP_CONS: PAIR[0] the car, PAIR[1] the cdr, both still on the VM stack
 * (the interpreter's cl_cons_rooted, which reads them after allocating). */
CL_Obj cl_jit_vmstack_cons(CL_Obj *pair)
{
    return cl_cons_rooted(&pair[0], &pair[1]);
}

/* OP_LIST: the N elements at BASE[0..N-1], first element lowest, the
 * interpreter's loop. */
CL_Obj cl_jit_vmstack_list(CL_Obj *base, uint32_t n)
{
    CL_Obj list = CL_NIL;
    int32_t i;
    CL_GC_PROTECT(list);
    for (i = (int32_t)n - 1; i >= 0; i--)
        list = cl_cons_rooted(&base[i], &list);
    CL_GC_UNPROTECT(1);
    return list;
}

/* OP_PUSH_LOCAL: (push *ITEM *SLOT), both words of the VM stack. */
CL_Obj cl_jit_vmstack_push_local(CL_Obj *item, CL_Obj *slot)
{
    CL_Obj cell = cl_cons_rooted(item, slot);
    *slot = cell;
    return cell;
}
#endif /* JIT_A64 */
