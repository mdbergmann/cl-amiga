/* asm_m68k.c — m68k instruction encoders.
 *
 * Pure byte emission — no native execution here.  Verification path
 * is FS-UAE: the cross-built binary runs the emitted code, and the
 * Amiga test suite exercises whichever functions JIT-compile.
 */

#ifdef JIT_M68K

#include <stddef.h>      /* NULL */

#include "jit/asm_m68k.h"

/* NOP: opcode 0100 1110 0111 0001 (0x4E71).
 * M68000 PRM §4-145: "Performs no operation". */
void m68k_emit_nop(CodeBuf *cb)
{
    cb_emit_u16(cb, 0x4E71);
}

/* RTS: opcode 0100 1110 0111 0101 (0x4E75).
 * M68000 PRM §4-169: pops a longword return address from the stack
 * into the PC. */
void m68k_emit_rts(CodeBuf *cb)
{
    cb_emit_u16(cb, 0x4E75);
}

/* MOVEQ: opcode 0111 nnn 0 IIIIIIII (16-bit, no operand words).
 * Sign-extends an 8-bit immediate to longword in Dn.
 * Caller is responsible for passing a data register (D0..D7); the
 * register field is masked to 3 bits, so an A-register slips through
 * as the matching D-register here — keep encoders thin and let
 * codegen layer enforce shape. */
void m68k_emit_moveq(CodeBuf *cb, int8_t imm, M68kReg dn)
{
    uint16_t enc = (uint16_t)(0x7000 | ((dn & 7) << 9) | (uint8_t)imm);
    cb_emit_u16(cb, enc);
}

/* MOVE.L #imm32,Dn: opcode 0010 nnn 000 111100 followed by a 4-byte
 * big-endian immediate.  Source EA = 111/100 (immediate addressing
 * mode), dest EA = 000/nnn (data register direct).  6 bytes total. */
void m68k_emit_move_l_imm32(CodeBuf *cb, uint32_t imm, M68kReg dn)
{
    uint16_t enc = (uint16_t)(0x2000 | ((dn & 7) << 9) | 0x003C);
    cb_emit_u16(cb, enc);
    cb_emit_u32(cb, imm);
}

/* MOVE.L (d16,An),Dn: opcode 0010 nnn 000 101 aaa followed by a 2-byte
 * big-endian signed displacement.  Source EA = 101/aaa (mode 5: address
 * register indirect with 16-bit displacement), dest EA = 000/nnn (data
 * register direct).  4 bytes total. */
void m68k_emit_move_l_disp_an_to_dn(CodeBuf *cb, int16_t disp,
                                    M68kReg an, M68kReg dn)
{
    uint16_t enc = (uint16_t)(0x2000 | ((dn & 7) << 9) | 0x0028 | (an & 7));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

/* LINK An,#disp16: opcode 0100 1110 0101 0nnn = 0x4E50 | n, followed
 * by 16-bit signed displacement.  Pushes An onto the stack, copies SP
 * to An, then adds disp to SP (negative for frame allocation). */
void m68k_emit_link_an_disp16(CodeBuf *cb, M68kReg an, int16_t disp)
{
    uint16_t enc = (uint16_t)(0x4E50 | (an & 7));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

/* UNLK An: opcode 0100 1110 0101 1nnn = 0x4E58 | n.  Reverses LINK. */
void m68k_emit_unlk_an(CodeBuf *cb, M68kReg an)
{
    uint16_t enc = (uint16_t)(0x4E58 | (an & 7));
    cb_emit_u16(cb, enc);
}

/* CLR.L -(An): opcode 0100 0010 10 100 nnn = 0x42A0 | n.  Pre-decrements
 * An by 4, then writes 0 to (An). */
void m68k_emit_clr_l_predec(CodeBuf *cb, M68kReg an)
{
    cb_emit_u16(cb, (uint16_t)(0x42A0 | (an & 7)));
}

/* MOVE.L #imm32,-(An): src EA = 111/100 (immediate), dst EA = 100/nnn
 * (-(An)).  Bits: 0010 nnn 100 111 100 + 32-bit immediate. */
void m68k_emit_move_l_imm32_predec(CodeBuf *cb, uint32_t imm, M68kReg an)
{
    uint16_t enc = (uint16_t)(0x2000 | ((an & 7) << 9) |
                              (4 << 6) | (7 << 3) | 4);
    cb_emit_u16(cb, enc);
    cb_emit_u32(cb, imm);
}

/* MOVE.L (An),(d16,Am): src EA = 010/an ((An)), dst EA = 101/am
 * ((d16,Am)).  Bits: 0010 am 101 010 an + 16-bit displacement.  Note
 * dst-reg occupies bits 11-9, src-reg occupies bits 2-0, dst-mode is
 * in bits 8-6, src-mode in bits 5-3. */
void m68k_emit_move_l_an_to_disp_am(CodeBuf *cb, M68kReg an,
                                    int16_t disp, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x2000 | ((am & 7) << 9) |
                              (5 << 6) | (2 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

/* MOVE.L (d16,An),-(Am): src EA = 101/an ((d16,An)), dst EA = 100/am
 * (-(Am)).  4 bytes: opcode + 16-bit displacement. */
void m68k_emit_move_l_disp_an_predec_am(CodeBuf *cb, int16_t disp,
                                        M68kReg an, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x2000 | ((am & 7) << 9) |
                              (4 << 6) | (5 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

/* MOVE.L (An)+,Dn: src EA = 011/an ((An)+), dst EA = 000/dn (Dn).
 * Bits: 0010 dn 000 011 an.  2 bytes. */
void m68k_emit_move_l_postinc_an_to_dn(CodeBuf *cb, M68kReg an, M68kReg dn)
{
    uint16_t enc = (uint16_t)(0x2000 | ((dn & 7) << 9) |
                              (0 << 6) | (3 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
}

/* ADDQ.L #imm,An: opcode 0101 ddd 0 10 001 nnn where ddd is the 3-bit
 * data field (1..7 encoded as 1..7, 8 encoded as 0), size=10 (long),
 * EA mode=001 (An direct).  2 bytes. */
void m68k_emit_addq_l_an(CodeBuf *cb, uint8_t imm, M68kReg an)
{
    uint8_t data = (imm == 8) ? 0 : (uint8_t)(imm & 7);
    uint16_t enc = (uint16_t)(0x5000 | ((uint16_t)data << 9) |
                              (2 << 6) | (1 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
}

/* MOVE.L (An),-(Am): src EA = 010/an, dst EA = 100/am.  Bits:
 * 0010 am 100 010 an.  2 bytes — no extension words.  m68k semantics
 * read the source operand fully before applying the destination's
 * predecrement, so `move.l (a7),-(a7)` duplicates TOS rather than
 * overwriting it. */
void m68k_emit_move_l_an_to_predec_am(CodeBuf *cb, M68kReg an, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x2000 | ((am & 7) << 9) |
                              (4 << 6) | (2 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
}

/* Bcc.W: opcode word 0110 cccc 0000 0000 then 16-bit signed
 * displacement.  cccc selects the condition: 0 = BRA, 6 = BNE,
 * 7 = BEQ, 9 = BVS, 12 = BGE, 13 = BLT, 14 = BGT, 15 = BLE.
 * See M68000 PRM §4-25. */
void m68k_emit_bcc_w(CodeBuf *cb, uint8_t cond, int16_t disp)
{
    uint16_t enc = (uint16_t)(0x6000 | ((uint16_t)(cond & 0xF) << 8));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

void m68k_emit_bra_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb,  0, disp); }
void m68k_emit_beq_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb,  7, disp); }
void m68k_emit_bne_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb,  6, disp); }
void m68k_emit_bvs_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb,  9, disp); }
void m68k_emit_blt_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 13, disp); }
void m68k_emit_bge_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 12, disp); }
void m68k_emit_bgt_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 14, disp); }
void m68k_emit_ble_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 15, disp); }

void m68k_patch_disp16(uint8_t *code, uint32_t code_len,
                       uint32_t patch_off, int16_t disp)
{
    if (code == NULL) return;
    if (patch_off + 2 > code_len) return;
    code[patch_off]     = (uint8_t)(((uint16_t)disp >> 8) & 0xFF);
    code[patch_off + 1] = (uint8_t)( (uint16_t)disp       & 0xFF);
}

/* MOVE.L Dn,Dm: src EA = 000/dn, dst EA = 000/dm.
 * Word: 0010 dm 000 000 dn = 0x2000 | (dm<<9) | dn.  2 bytes. */
void m68k_emit_move_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm)
{
    uint16_t enc = (uint16_t)(0x2000 | ((dm & 7) << 9) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* MOVE.L Dn,-(An): src EA = 000/dn, dst EA = 100/an.
 * Word: 0010 an 100 000 dn = 0x2100 | (an<<9) | dn.  2 bytes. */
void m68k_emit_move_l_dn_predec_an(CodeBuf *cb, M68kReg dn, M68kReg an)
{
    uint16_t enc = (uint16_t)(0x2000 | ((an & 7) << 9) |
                              (4 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* AND.L Dn,Dm: <ea> AND Dn → Dn form, with EA = source data register.
 * Bits: 1100 dm 010 000 dn = 0xC080 | (dm<<9) | dn.  2 bytes. */
void m68k_emit_and_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm)
{
    uint16_t enc = (uint16_t)(0xC000 | ((dm & 7) << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* BTST #imm,Dn: 0000 100 000 000 dn first word, then 16-bit word with
 * the bit number in the low byte (imm is masked mod 32 by the CPU
 * for Dn destinations).  4 bytes. */
void m68k_emit_btst_imm_dn(CodeBuf *cb, uint8_t imm, M68kReg dn)
{
    cb_emit_u16(cb, (uint16_t)(0x0800 | (dn & 7)));
    cb_emit_u16(cb, (uint16_t)(imm & 0x1F));
}

/* ADD.L Dn,Dm: <ea> ADD Dn → Dn form, EA = source data register.
 * Bits: 1101 dm 010 000 dn = 0xD080 | (dm<<9) | dn.  2 bytes. */
void m68k_emit_add_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm)
{
    uint16_t enc = (uint16_t)(0xD000 | ((dm & 7) << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* SUB.L Dn,Dm: <ea> SUB Dn → Dn form.  Bits: 1001 dm 010 000 dn =
 * 0x9080 | (dm<<9) | dn.  2 bytes. */
void m68k_emit_sub_l_dn_to_dm(CodeBuf *cb, M68kReg dn, M68kReg dm)
{
    uint16_t enc = (uint16_t)(0x9000 | ((dm & 7) << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* SUBQ.L #imm,Dn: opcode 0101 ddd 1 10 000 nnn where ddd is the 3-bit
 * data field (1..7 encoded as 1..7, 8 encoded as 0), bit 8 = 1 marks
 * SUBQ (vs ADDQ), size=10 (long), mode=000 (Dn).  2 bytes. */
void m68k_emit_subq_l_dn(CodeBuf *cb, uint8_t imm, M68kReg dn)
{
    uint8_t data = (imm == 8) ? 0 : (uint8_t)(imm & 7);
    uint16_t enc = (uint16_t)(0x5100 | ((uint16_t)data << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* ADDQ.L #imm,Dn: bit 8 = 0 (ADDQ).  2 bytes. */
void m68k_emit_addq_l_dn(CodeBuf *cb, uint8_t imm, M68kReg dn)
{
    uint8_t data = (imm == 8) ? 0 : (uint8_t)(imm & 7);
    uint16_t enc = (uint16_t)(0x5000 | ((uint16_t)data << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* CMP.L Dn,Dm: CMP <ea>,Dn form with EA = source data register.  The
 * `Dn` field of the instruction is the *destination* (the register
 * whose value is subtracted from), the EA reg field is the source.
 * Flags reflect (Dn_field - EA_field) = Dm - Dn.
 * Bits: 1011 dm 010 000 dn = 0xB080 | (dm<<9) | dn.  2 bytes. */
void m68k_emit_cmp_l_dn_dm(CodeBuf *cb, M68kReg dn, M68kReg dm)
{
    uint16_t enc = (uint16_t)(0xB000 | ((dm & 7) << 9) |
                              (2 << 6) | (dn & 7));
    cb_emit_u16(cb, enc);
}

/* JSR (xxx).L: opcode 0x4EB9, then 32-bit big-endian absolute address.
 * 6 bytes.  See M68000 PRM §4-119. */
void m68k_emit_jsr_abs_l(CodeBuf *cb, uint32_t addr)
{
    cb_emit_u16(cb, 0x4EB9);
    cb_emit_u32(cb, addr);
}

/* MOVE.L An,Am: dst mode = 001 (An), src mode = 001 (An).
 * Word: 0010 am 001 001 an = 0x2040 | (am<<9) | 0x008 | an.  2 bytes.
 * Equivalent to MOVEA.L An,Am (same encoding; m68k aliases the two
 * for register-to-register address moves). */
void m68k_emit_move_l_an_to_am(CodeBuf *cb, M68kReg an, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x2000 | ((am & 7) << 9) |
                              (1 << 6) | (1 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
}

/* MOVE.L An,-(Am): src mode = 001 (An direct, push the address-
 * register *value*), dst mode = 100 (predec).
 * Word: 0010 am 100 001 an.  2 bytes.  Note this is distinct from
 * `move_l_an_to_predec_am` (src mode 010 = indirect) — that one
 * pushes what An points at; this one pushes An itself. */
void m68k_emit_move_l_an_predec_am(CodeBuf *cb, M68kReg an, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x2000 | ((am & 7) << 9) |
                              (4 << 6) | (1 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
}

/* LEA (d16,An),Am: opcode 0100 am 111 101 an + 16-bit signed disp.
 * Bits 8-6 = 111 distinguishes LEA from a regular MOVE; source EA
 * mode = 101 (address-register indirect with 16-bit displacement).
 * 4 bytes.  See M68000 PRM §4-120. */
void m68k_emit_lea_disp_an_to_am(CodeBuf *cb, int16_t disp,
                                 M68kReg an, M68kReg am)
{
    uint16_t enc = (uint16_t)(0x4000 | ((am & 7) << 9) |
                              (7 << 6) | (5 << 3) | (an & 7));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

#endif /* JIT_M68K */
