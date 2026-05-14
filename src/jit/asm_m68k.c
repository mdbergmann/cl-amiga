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
 * displacement.  cccc selects the condition: 0 = BRA (unconditional),
 * 6 = BNE (Z=0), 7 = BEQ (Z=1).  See M68000 PRM §4-25. */
static void m68k_emit_bcc_w(CodeBuf *cb, uint8_t cond, int16_t disp)
{
    uint16_t enc = (uint16_t)(0x6000 | ((uint16_t)(cond & 0xF) << 8));
    cb_emit_u16(cb, enc);
    cb_emit_u16(cb, (uint16_t)disp);
}

void m68k_emit_bra_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 0, disp); }
void m68k_emit_beq_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 7, disp); }
void m68k_emit_bne_w(CodeBuf *cb, int16_t disp) { m68k_emit_bcc_w(cb, 6, disp); }

void m68k_patch_disp16(uint8_t *code, uint32_t code_len,
                       uint32_t patch_off, int16_t disp)
{
    if (code == NULL) return;
    if (patch_off + 2 > code_len) return;
    code[patch_off]     = (uint8_t)(((uint16_t)disp >> 8) & 0xFF);
    code[patch_off + 1] = (uint8_t)( (uint16_t)disp       & 0xFF);
}

#endif /* JIT_M68K */
