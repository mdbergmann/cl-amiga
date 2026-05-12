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

#endif /* JIT_M68K */
