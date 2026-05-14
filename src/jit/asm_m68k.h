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

/* MOVE.L (d16,An),Dn — load a longword from memory at address (An+d16)
 * into Dn.  Source EA = 101/An (address register indirect with 16-bit
 * displacement), dest EA = 000/Dn.  Emits 4 bytes: 2-byte opcode +
 * 2-byte signed big-endian displacement.  Used to read C-ABI stack args
 * off A7 (e.g. `move.l 4(a7),d0` for the first arg).  See M68000 PRM
 * §4-128 (MOVE) and §2-2 (mode 5 addressing).  an must be A0..A7, dn
 * must be D0..D7. */
void m68k_emit_move_l_disp_an_to_dn(CodeBuf *cb, int16_t disp,
                                    M68kReg an, M68kReg dn);

/* --- Walker-only encoders (per-opcode template emitter).
 *
 * These build the stack-frame + operand-stack discipline the walker
 * uses: LINK/UNLK for the frame, push/pop via predecrement/postincrement
 * for the operand stack on A7, and load/store through a frame pointer
 * for locals. */

/* LINK An,#disp16 — push An, set An=A7, then A7 += disp16.  Negative
 * disp allocates frame space below the saved An.  Emits 4 bytes:
 * opcode word + 16-bit signed displacement.  See M68000 PRM §4-122. */
void m68k_emit_link_an_disp16(CodeBuf *cb, M68kReg an, int16_t disp);

/* UNLK An — A7=An; pop into An.  Reverses LINK.  2 bytes.  See
 * M68000 PRM §4-198. */
void m68k_emit_unlk_an(CodeBuf *cb, M68kReg an);

/* CLR.L -(An) — write 0 into the longword at predecremented An.
 * Used to push CL_NIL onto the operand stack in 2 bytes.  Opcode
 * 0100 0010 10 100 nnn.  See M68000 PRM §4-49. */
void m68k_emit_clr_l_predec(CodeBuf *cb, M68kReg an);

/* MOVE.L #imm32,-(An) — push a 32-bit immediate onto the stack at
 * -An.  6 bytes: opcode + 32-bit big-endian immediate.  Source EA =
 * 111/100 (immediate), dest EA = 100/nnn (pre-decrement An). */
void m68k_emit_move_l_imm32_predec(CodeBuf *cb, uint32_t imm, M68kReg an);

/* MOVE.L (An),(d16,Am) — copy longword at (An) to memory at (Am+d16).
 * Used by OP_STORE to write TOS to a local without popping (the
 * matching OP_POP pops separately).  4 bytes. */
void m68k_emit_move_l_an_to_disp_am(CodeBuf *cb, M68kReg an,
                                    int16_t disp, M68kReg am);

/* MOVE.L (d16,An),-(Am) — push longword at (An+d16) onto -Am.  Used
 * by OP_LOAD to push a local onto the operand stack.  4 bytes. */
void m68k_emit_move_l_disp_an_predec_am(CodeBuf *cb, int16_t disp,
                                        M68kReg an, M68kReg am);

/* MOVE.L (An)+,Dn — pop longword from (An)+ into Dn.  Used by OP_RET
 * to pop the operand-stack TOS into the return-value register D0.
 * 2 bytes. */
void m68k_emit_move_l_postinc_an_to_dn(CodeBuf *cb, M68kReg an, M68kReg dn);

/* ADDQ.L #imm,An — add a small immediate (1..8) to address register
 * An.  Used by OP_POP as `addq.l #4,a7` to discard a longword.
 * 2 bytes.  Encoded with imm=8 stored as data field 0. */
void m68k_emit_addq_l_an(CodeBuf *cb, uint8_t imm, M68kReg an);

#endif /* JIT_M68K */

#endif /* CL_JIT_ASM_M68K_H */
