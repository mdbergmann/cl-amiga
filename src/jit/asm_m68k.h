/* asm_m68k.h — m68k instruction encoders.
 *
 * Each function appends one m68k instruction's bytes to a CodeBuf.
 * The codegen layer (codegen_m68k.c) composes these into per-opcode
 * templates.
 *
 * Encoding reference: Motorola M68000 Family Programmer's Reference
 * Manual.  Each encoder cites the relevant section in its body.
 *
 * m68k-only: verification happens via FS-UAE running the cross-built
 * binary; the host build doesn't include this file.
 */

#ifndef CL_JIT_ASM_M68K_H
#define CL_JIT_ASM_M68K_H

#ifdef JIT_M68K

#include "jit/codebuf.h"

/* m68k register identifiers.  Matches the convention in
 * specs/native-backend.md §"Calling convention for emitted code". */
typedef enum {
    REG_D0, REG_D1, REG_D2, REG_D3, REG_D4, REG_D5, REG_D6, REG_D7,
    REG_A0, REG_A1, REG_A2, REG_A3, REG_A4, REG_A5, REG_A6, REG_A7
} M68kReg;

/* Zero-operand instructions (single 16-bit opcode word).
 * See M68000 PRM §4-NOP, §4-RTS. */
void m68k_emit_nop(CodeBuf *cb);
void m68k_emit_rts(CodeBuf *cb);

/* MOVEQ #imm8,Dn — sign-extend an 8-bit immediate into a data register.
 * dn must be a data register (REG_D0..REG_D7).  See M68000 PRM §4-134. */
void m68k_emit_moveq(CodeBuf *cb, int8_t imm, M68kReg dn);

/* MOVE.L #imm32,Dn — load a 32-bit immediate into a data register.
 * Emits 6 bytes: 2-byte opcode + 4-byte big-endian immediate.
 * dn must be a data register (REG_D0..REG_D7).  See M68000 PRM §4-128. */
void m68k_emit_move_l_imm32(CodeBuf *cb, uint32_t imm, M68kReg dn);

#endif /* JIT_M68K */

#endif /* CL_JIT_ASM_M68K_H */
