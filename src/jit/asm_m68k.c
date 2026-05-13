/* asm_m68k.c — m68k instruction encoders.
 *
 * Pure byte emission — no native execution here.  Verification path
 * is FS-UAE: the cross-built binary runs the emitted code, and the
 * Amiga test suite exercises whichever functions JIT-compile.
 */

#ifdef JIT_M68K

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

#endif /* JIT_M68K */
