/* codegen_m68k.h — per-opcode emitters ("templates").
 *
 * Each public function appends m68k machine code for one bytecode
 * opcode to a CodeBuf, using the register conventions documented in
 * specs/native-backend.md §"Calling convention for emitted code".
 *
 * Skeleton: only the init hook exists.  emit_op_* functions land
 * incrementally per opcode (starting with the staging-1 set:
 * CONST, LOAD, STORE, POP, RET, ADD, LT, JNIL, JMP, HALT).
 */

#ifndef CL_JIT_CODEGEN_M68K_H
#define CL_JIT_CODEGEN_M68K_H

#ifdef JIT_M68K

void cl_jit_codegen_init(void);

#endif /* JIT_M68K */

#endif /* CL_JIT_CODEGEN_M68K_H */
