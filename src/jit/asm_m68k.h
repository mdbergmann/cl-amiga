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

/* MOVE.L (An),-(Am) — push the longword at (An) onto -Am.  Used by
 * OP_DUP as `move.l (a7),-(a7)` to duplicate the operand-stack TOS.
 * 2 bytes: src EA = 010/an, dst EA = 100/am.  No extension words.
 *
 * Note: the m68k hardware enforces that `MOVE.L (A7),-(A7)` reads the
 * source *before* the predecrement updates A7, so the old TOS gets
 * copied to the new TOS — exactly the OP_DUP semantics. */
void m68k_emit_move_l_an_to_predec_am(CodeBuf *cb, M68kReg an, M68kReg am);

/* Bcc.W — m68k branches with 16-bit signed displacement.
 *
 * Encoding: opcode word 0110 cccc 0000 0000, then a 16-bit signed
 * displacement word.  Displacement is relative to PC = (instr_start + 2),
 * i.e. the byte right after the opcode word — which is also where the
 * displacement word itself lives.  Range: -32768..+32767.
 *
 * For forward branches the caller emits with disp=0 and patches the
 * displacement later (see m68k_patch_disp16); for backward branches the
 * target is already known and the caller computes disp inline.
 *
 * Condition codes (passed to the generic m68k_emit_bcc_w): 0=BRA,
 * 6=BNE, 7=BEQ, 9=BVS, 12=BGE, 13=BLT, 14=BGT, 15=BLE.  Named
 * wrappers below for the ones the walker currently emits. */
void m68k_emit_bcc_w(CodeBuf *cb, uint8_t cond, int16_t disp);
void m68k_emit_bra_w(CodeBuf *cb, int16_t disp);   /* unconditional */
void m68k_emit_beq_w(CodeBuf *cb, int16_t disp);   /* branch if Z=1   */
void m68k_emit_bne_w(CodeBuf *cb, int16_t disp);   /* branch if Z=0   */
void m68k_emit_bvs_w(CodeBuf *cb, int16_t disp);   /* branch if V=1   */
void m68k_emit_blt_w(CodeBuf *cb, int16_t disp);   /* branch if signed < */
void m68k_emit_bge_w(CodeBuf *cb, int16_t disp);   /* branch if signed >= */
void m68k_emit_bgt_w(CodeBuf *cb, int16_t disp);   /* branch if signed > */
void m68k_emit_ble_w(CodeBuf *cb, int16_t disp);   /* branch if signed <= */

/* MOVE.L Dn,Dm — copy a data register to another data register.
 * 2 bytes.  Both modes are register direct (mode 000). */
void m68k_emit_move_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm);

/* MOVE.L Dn,-(An) — push a data-register longword onto -An.  2 bytes. */
void m68k_emit_move_l_dn_predec_an(CodeBuf *cb, M68kReg dn, M68kReg an);

/* AND.L Dn,Dm — Dm = Dm AND Dn.  2 bytes.  Used for tag-pair check:
 * a fixnum's tag bit (bit 0) is 1, so AND'ing two tagged objects
 * yields bit 0 = 1 iff both are fixnums. */
void m68k_emit_and_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm);

/* BTST #imm,Dn — set Z = ~bit imm of Dn.  imm is masked mod 32.
 * 4 bytes (opcode word + immediate word).  Used to test tag bit 0
 * after the AND. */
void m68k_emit_btst_imm_dn(CodeBuf *cb, uint8_t imm, M68kReg dn);

/* ADD.L Dn,Dm — Dm = Dm + Dn.  Sets N/Z/V/C flags.  2 bytes.  The V
 * flag captures signed overflow — the JIT uses BVS to detect fixnum
 * range overflow. */
void m68k_emit_add_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm);

/* SUB.L Dn,Dm — Dm = Dm - Dn.  Sets N/Z/V/C.  2 bytes.  Used by the
 * OP_ADD overflow recovery: after a fixnum+fixnum overflow, d0 holds
 * the wrapped sum and d1 holds b; subtracting d1 from d0 (modulo 2^32)
 * reconstructs the original a so the slow-path JSR sees the right
 * arguments. */
void m68k_emit_sub_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm);

/* SUBQ.L #imm,Dn — small immediate subtract from Dn.  imm in 1..8;
 * 8 encodes as data=0.  2 bytes.  Used by the OP_ADD fast path to
 * strip the surplus tag bit after fixnum + fixnum. */
void m68k_emit_subq_l_dn(CodeBuf *cb, uint8_t imm, M68kReg dn);

/* ADDQ.L #imm,Dn — symmetric to SUBQ.L.  2 bytes. */
void m68k_emit_addq_l_dn(CodeBuf *cb, uint8_t imm, M68kReg dn);

/* CMP.L Dn,Dm — sets flags from Dm - Dn (no result stored).  2 bytes.
 * BLT/BGE etc. then branch on signed compare result.  Following the
 * m68k convention, the *first* operand is the subtrahend: CMP.L Dn,Dm
 * tests "Dm vs Dn" — flags reflect Dm - Dn. */
void m68k_emit_cmp_l_dn_dm(CodeBuf *cb, M68kReg dn, M68kReg dm);

/* JSR (xxx).L — call absolute long address.  6 bytes: 2-byte opcode
 * (0x4EB9) + 4-byte big-endian address.  Used to invoke runtime
 * helpers from JIT-emitted code; the m68k C ABI's standard stack-
 * passed-args / D0-result convention is preserved. */
void m68k_emit_jsr_abs_l(CodeBuf *cb, uint32_t addr);

/* MOVE.L An,Am — copy a 32-bit address register to another address
 * register.  2 bytes.  Both modes are address-register direct
 * (mode 001).  Used by OP_CALL to snapshot A7 (operand-stack TOS)
 * into a scratch A-register before the helper's args are pushed —
 * the saved address points at the call's argN-1 and acts as the
 * helper's operand_top parameter. */
void m68k_emit_move_l_an_to_am(CodeBuf *cb, M68kReg an, M68kReg am);

/* MOVE.L An,-(Am) — push the longword *value* of address register
 * An onto -Am.  Distinct from m68k_emit_move_l_an_to_predec_am
 * (which pushes the longword at the *address* in An).  Used by
 * OP_CALL to push the saved operand_top pointer as a C-ABI argument
 * — what we want is the pointer itself, not what it points at.
 * 2 bytes. */
void m68k_emit_move_l_an_predec_am(CodeBuf *cb, M68kReg an, M68kReg am);

/* LEA (d16,An),Am — Am = An + sign-extended d16.  Address-only
 * arithmetic; doesn't disturb the condition codes.  4 bytes: opcode
 * word + 16-bit signed displacement.  Used by OP_CALL to drop the
 * call's func + N arg slots off the operand stack in one
 * instruction regardless of arity (LEA (4*(nargs+1))(a7),a7). */
void m68k_emit_lea_disp_an_to_am(CodeBuf *cb, int16_t disp,
                                 M68kReg an, M68kReg am);

/* MOVE.L Dn,(d16,Am) — write a data register to memory at (Am+d16).
 * Used by OP_STORE / cache spills when the value to write lives in a
 * data register (e.g. the stack-cache TOS in D7).  4 bytes.  Source
 * EA = 000/dn (data-register direct), dest EA = 101/am (address-
 * register indirect with 16-bit displacement). */
void m68k_emit_move_l_dn_to_disp_am(CodeBuf *cb, M68kReg dn,
                                    int16_t disp, M68kReg am);

/* TST.L Dn — set N/Z flags from Dn (compare Dn to 0).  2 bytes.
 * Used by cache-aware branch templates (JNIL / JTRUE / NOT) to test
 * a CL_Obj in a data register without first popping it to the m68k
 * stack — the immediate predecessor of the test had to be a value-
 * producing instruction either way, so this just lets the test read
 * the register direct.  Opcode 0100 1010 1000 0nnn. */
void m68k_emit_tst_l_dn(CodeBuf *cb, M68kReg dn);

/* Overwrite a 16-bit big-endian field already written to `code` at byte
 * offset `patch_off`.  Used to fill in forward-branch displacements
 * once the target's native offset is known.  No-op if patch_off+2
 * exceeds code_len — keeps the patch loop simple, the caller has
 * already bailed if a branch's range overflowed. */
void m68k_patch_disp16(uint8_t *code, uint32_t code_len,
                       uint32_t patch_off, int16_t disp);

#endif /* JIT_M68K */

#endif /* CL_JIT_ASM_M68K_H */
