/* runtime_m68k.h -- the m68k walker's own runtime helpers (runtime_m68k.c).
 * The CPU-neutral ones are in runtime.h. */

#ifndef CL_JIT_RUNTIME_M68K_H
#define CL_JIT_RUNTIME_M68K_H

#ifdef JIT_M68K

#include "core/types.h"

void   cl_jit_runtime_init(void);

/* A direct-call site's cell (specs/jit-direct-calls.md §2): 16 bytes the
 * walker appends after the function's code, one per OP_CALL / OP_TAILCALL /
 * OP_CALL_GLOBAL / OP_TAILCALL_GLOBAL.  Native code only reads it; the
 * _site helpers below fill it on a miss.  Valid only while gen equals
 * cl_call_gen, so `func` needs neither GC rooting nor relocation.  The
 * offsets are baked into the emitted hit path: gen 0, func 4, entry 8,
 * code 12.  `code` goes into the CL_Frame the hit path pushes; it is read
 * after gen is compared, so a fill racing that read can pair it with
 * another func -- harmless: nothing dereferences a native frame's code,
 * the UWPROT staleness checks only compare it with itself (runtime_nlx.c),
 * and it is never the frame's own stub_code. */
typedef struct {
    volatile uint32_t gen;     /* cl_call_gen at fill time; 0 = never filled */
    volatile CL_Obj   func;    /* the callee (bytecode or closure)          */
    void * volatile   entry;   /* its bc->native_code                       */
    uint8_t * volatile code;   /* its bc->code, for the frame               */
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

/* Kw prologue for JIT'd functions whose lambda-list carries &key
 * (with or without &optional / &rest).  Mirrors the frame setup in
 * vm.c::OP_CALL normal path: NIL-initializes the frame's slot area,
 * copies positional args into the matching slots, conses the &rest
 * list, then performs keyword matching right-to-left so the leftmost
 * duplicate keyword wins (CLHS 3.4.1.4.1).  Signals CL_ERR_ARGS on odd
 * argument count or unknown keyword unless :allow-other-keys is enabled.
 *
 *   func   - the function value at 8(a6) (closure or bytecode); the
 *            bytecode is derived from it, again after the &rest consing.
 *   nargs  - actual number of caller-supplied arguments (D1 at entry).
 *   last   - the last argument (12(a6)); argument i at last[nargs-1-i],
 *            the positional ABI's operand-stack order.
 *   frame  - pointer to the JIT frame's locals area; the walker LEAs
 *            `-(4*(n_locals+1))(a6)` into this pointer so frame[i]
 *            corresponds to JIT slot i (forward layout — frame[0] is
 *            the lowest-addressed slot).
 *
 * May call cl_error which longjmps out of the JIT frame; the unwind path
 * keeps GC depth tracking consistent via the CL_ErrorFrame snapshot,
 * so no manual cleanup is required. */
void cl_jit_runtime_kw_prologue(CL_Obj func, uint32_t nargs,
                                CL_Obj *last, CL_Obj *frame);

/* Prologue of a positional-ABI function with &rest (no &key): FUNC the
 * function value at 8(a6), NARGS the count the entry passed in D1, LAST
 * the last argument (12(a6); argument i at LAST[nargs-1-i]) and FRAME
 * slot 0 of the LINK frame.  NILs every slot, copies the positional
 * arguments and stores the list of the others in the slot after them.
 * Allocates (the list). */
void cl_jit_runtime_rest_prologue(CL_Obj func, uint32_t nargs,
                                  CL_Obj *last, CL_Obj *frame);

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

/* OP_RESTART_PUSH: the five operands by value (the conservative m68k-stack
 * scan reaches them); the other binding-stack opcodes are in runtime_nlx.h. */
void cl_jit_runtime_restart_push(CL_Obj name_sym, CL_Obj handler, CL_Obj report,
                                 CL_Obj interactive, CL_Obj test, CL_Obj tag);

/* Address of libc setjmp, captured at init time and baked into the
 * BLOCK_PUSH emit as a JSR.abs.l immediate. */
extern uint32_t cl_jit_setjmp_addr;

#endif /* JIT_M68K */

#endif /* CL_JIT_RUNTIME_M68K_H */
