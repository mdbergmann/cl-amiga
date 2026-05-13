/* jit.h — public API of the m68k native-code backend.
 *
 * This is the integration surface used by the rest of the runtime. Two
 * functions are called from the bytecode compiler / boot, and the rest
 * is implementation detail.
 *
 * On non-m68k builds (no JIT_M68K) every entry point becomes an inline
 * no-op so call sites stay identical and Lisp code is unaware of the
 * JIT's presence.
 *
 * See specs/native-backend.md for the full design.
 */

#ifndef CL_JIT_H
#define CL_JIT_H

#include "core/types.h"

#ifdef JIT_M68K

/* One-time init at boot, after cl_compiler_init. */
void   cl_jit_init(void);

/* Optionally translate this bytecode to native m68k. May leave
 * bc->native_code == NULL if the function is ineligible or the JIT is
 * disabled — callers must always be ready to fall back to the bytecode
 * interpreter. */
void   cl_jit_compile(CL_Bytecode *bc);

/* Enter native code with the same calling convention the bytecode VM
 * uses (args already pushed on cl_vm.stack). Caller checks
 * bc->native_code != NULL first. Not wired into vm.c in the skeleton. */
CL_Obj cl_jit_invoke(CL_Bytecode *bc, int nargs);

/* Runtime introspection: is the JIT compiled in and active? */
int    cl_jit_enabled(void);

/* Diagnostic helper: emit a fixed NOP+RTS stub into bc->native_code.
 * Used by the `%JIT-COMPILE-STUB` builtin to exercise the encoder →
 * CodeBuf → CL_Bytecode pipeline before there's any actual codegen
 * (or any caller that enters native_code).  Returns 1 on success,
 * 0 if allocation failed.  Existing native_code is replaced. */
int    cl_jit_emit_stub(CL_Bytecode *bc);

/* Diagnostic counter: number of cl_jit_invoke entries since boot.
 * Bumped on every native-code dispatch from OP_CALL.  Exposed to Lisp
 * via the `%JIT-INVOKE-COUNT` builtin so end-to-end tests can prove
 * that a call actually went through the native path rather than just
 * being interpreted (which would happen to return the same value). */
uint32_t cl_jit_invoke_count_get(void);

#else  /* !JIT_M68K — host / non-m68k targets get no-op stubs */

static inline void   cl_jit_init(void)                       { }
static inline void   cl_jit_compile(CL_Bytecode *bc)         { (void)bc; }
static inline CL_Obj cl_jit_invoke(CL_Bytecode *bc, int n)   { (void)bc; (void)n; return CL_NIL; }
static inline int    cl_jit_enabled(void)                    { return 0; }
static inline int    cl_jit_emit_stub(CL_Bytecode *bc)       { (void)bc; return 0; }
static inline uint32_t cl_jit_invoke_count_get(void)         { return 0; }

#endif /* JIT_M68K */

#endif /* CL_JIT_H */
