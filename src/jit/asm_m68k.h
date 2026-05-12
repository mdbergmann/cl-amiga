/* asm_m68k.h — low-level m68k instruction encoders.
 *
 * One function per instruction form used by codegen_m68k.c.  These
 * write raw bytes into a CodeBuf; they know about m68k effective
 * addressing modes and operand sizes but nothing about Lisp semantics.
 *
 * Skeleton: no encoders yet.  Will be filled in alongside the first
 * batch of opcode templates.
 */

#ifndef CL_JIT_ASM_M68K_H
#define CL_JIT_ASM_M68K_H

#ifdef JIT_M68K

#include <stdint.h>

/* m68k register identifiers.  Matches the convention in
 * specs/native-backend.md §"Calling convention for emitted code". */
typedef enum {
    REG_D0, REG_D1, REG_D2, REG_D3, REG_D4, REG_D5, REG_D6, REG_D7,
    REG_A0, REG_A1, REG_A2, REG_A3, REG_A4, REG_A5, REG_A6, REG_A7
} M68kReg;

#endif /* JIT_M68K */

#endif /* CL_JIT_ASM_M68K_H */
