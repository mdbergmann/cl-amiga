/* asm_a64.h — AArch64 instruction encoders and a label assembler.
 *
 * Every encoder is a pure function that returns the 32-bit instruction word,
 * or A64_BAD when an operand does not fit its field (an immediate out of
 * range, a misaligned offset).  A64_BAD is the caller's cue to synthesise a
 * longer form -- a64_mov_imm builds any 64-bit constant from MOVZ/MOVK --
 * or to decline the function.  Encodings follow the Arm ARM (DDI 0487,
 * C4.1 "A64 instruction set encoding"); tests/test_asm_a64.c pins every one
 * against the word clang assembles for it.
 *
 * Register numbers are 0..30; 31 is the zero register (WZR/XZR) in data
 * processing and the stack pointer in loads, stores and ADD/SUB immediate,
 * exactly as the architecture defines it.  SF selects the width: 0 for the
 * 32-bit W form (all CL_Obj values), 1 for the 64-bit X form (pointers).
 *
 * Portable C99: compiled into every host build so the encoder tests run on
 * x86-64 too; only the JIT_A64 build executes what it produces.
 * See specs/native-backend-a64.md.
 */

#ifndef CL_JIT_ASM_A64_H
#define CL_JIT_ASM_A64_H

#include <stdint.h>
#include "jit/codebuf.h"

#define A64_BAD 0xFFFFFFFFu   /* "does not encode"; an unallocated word */

#define A64_ZR  31            /* WZR / XZR */
#define A64_SP  31            /* SP, where the instruction means it */
#define A64_FP  29
#define A64_LR  30

typedef enum {
    A64_EQ = 0, A64_NE = 1, A64_HS = 2, A64_LO = 3, A64_MI = 4, A64_PL = 5,
    A64_VS = 6, A64_VC = 7, A64_HI = 8, A64_LS = 9, A64_GE = 10, A64_LT = 11,
    A64_GT = 12, A64_LE = 13, A64_AL = 14
} A64Cond;

/* The opposite condition (EQ <-> NE, GE <-> LT, ...).  Not for AL. */
#define A64_INVERT(c) ((A64Cond)((c) ^ 1))

/* --- Moves ------------------------------------------------------------- */
uint32_t a64_movz(int sf, int rd, uint32_t imm16, int shift);   /* shift 0/16/32/48 */
uint32_t a64_movk(int sf, int rd, uint32_t imm16, int shift);
uint32_t a64_movn(int sf, int rd, uint32_t imm16, int shift);
uint32_t a64_mov_reg(int sf, int rd, int rm);       /* ORR rd, zr, rm */
uint32_t a64_mov_sp(int rd, int rn);                /* ADD rd, rn, #0 (to/from SP) */

/* --- Arithmetic -------------------------------------------------------- */
/* ADD/SUB(S) immediate: IMM in 0..4095, or a multiple of 4096 up to
 * 4095 << 12 (the LSL #12 form). */
uint32_t a64_add_imm (int sf, int rd, int rn, uint32_t imm);
uint32_t a64_sub_imm (int sf, int rd, int rn, uint32_t imm);
uint32_t a64_adds_imm(int sf, int rd, int rn, uint32_t imm);
uint32_t a64_subs_imm(int sf, int rd, int rn, uint32_t imm);
uint32_t a64_cmp_imm (int sf, int rn, uint32_t imm);     /* SUBS zr, rn, #imm */
/* Register forms, no shift. */
uint32_t a64_add_reg (int sf, int rd, int rn, int rm);
uint32_t a64_sub_reg (int sf, int rd, int rn, int rm);
uint32_t a64_adds_reg(int sf, int rd, int rn, int rm);
uint32_t a64_subs_reg(int sf, int rd, int rn, int rm);
uint32_t a64_cmp_reg (int sf, int rn, int rm);           /* SUBS zr, rn, rm */
/* ADD xd, xn, wm, UXTW #shift (shift 0..4): a 32-bit offset off a base. */
uint32_t a64_add_uxtw(int rd, int rn, int rm, int shift);
uint32_t a64_and_reg (int sf, int rd, int rn, int rm);
uint32_t a64_orr_reg (int sf, int rd, int rn, int rm);
uint32_t a64_eor_reg (int sf, int rd, int rn, int rm);
uint32_t a64_tst_reg (int sf, int rn, int rm);           /* ANDS zr, rn, rm */
uint32_t a64_asr_imm (int sf, int rd, int rn, int shift);
uint32_t a64_lsl_imm (int sf, int rd, int rn, int shift);
uint32_t a64_lsr_imm (int sf, int rd, int rn, int shift);
uint32_t a64_smull   (int rd, int rn, int rm);           /* xd = wn * wm, signed */
uint32_t a64_cmp_sxtw(int rn, int rm);                   /* CMP xn, wm, SXTW */
uint32_t a64_csel    (int sf, int rd, int rn, int rm, A64Cond cond);
uint32_t a64_cset    (int sf, int rd, A64Cond cond);

/* --- Loads and stores --------------------------------------------------
 * SIZE is the access size in bytes: 1, 2, 4 or 8.  The unsigned-offset
 * form wants OFF to be a non-negative multiple of SIZE below 4096*SIZE. */
uint32_t a64_ldr_uoff(int size, int rt, int rn, uint32_t off);
uint32_t a64_str_uoff(int size, int rt, int rn, uint32_t off);
/* LDUR/STUR: unscaled, OFF in -256..255. */
uint32_t a64_ldur(int size, int rt, int rn, int32_t off);
uint32_t a64_stur(int size, int rt, int rn, int32_t off);
/* Pre- and post-index, 4 or 8 bytes, OFF in -256..255: the push and pop
 * of the operand stack (str w, [x20], #4 / ldr w, [x20, #-4]!). */
uint32_t a64_ldr_post(int size, int rt, int rn, int32_t off);
uint32_t a64_str_post(int size, int rt, int rn, int32_t off);
uint32_t a64_ldr_pre (int size, int rt, int rn, int32_t off);
uint32_t a64_str_pre (int size, int rt, int rn, int32_t off);
/* LDR wt, [xn, wm, UXTW]: a CL_Obj word at arena base + offset. */
uint32_t a64_ldr_w_uxtw(int rt, int rn, int rm);
uint32_t a64_str_w_uxtw(int rt, int rn, int rm);
/* STP/LDP of X registers; OFF a multiple of 8 in -512..504. */
uint32_t a64_stp_pre (int rt, int rt2, int rn, int32_t off);
uint32_t a64_ldp_post(int rt, int rt2, int rn, int32_t off);
uint32_t a64_stp_off (int rt, int rt2, int rn, int32_t off);
uint32_t a64_ldp_off (int rt, int rt2, int rn, int32_t off);

/* --- Control ----------------------------------------------------------- */
uint32_t a64_ret(void);                  /* RET x30 */
uint32_t a64_br (int rn);
uint32_t a64_blr(int rn);
uint32_t a64_nop(void);
uint32_t a64_dmb_ish(void);              /* DMB ISH: a full inner-shareable barrier */
uint32_t a64_brk(uint32_t imm16);
/* PC-relative forms with the displacement in BYTES from this instruction
 * (a multiple of 4).  The label assembler below fills these in; they are
 * public for the tests and for fixed short skips. */
uint32_t a64_b_rel    (int32_t disp);                    /* +-128 MB */
uint32_t a64_bl_rel   (int32_t disp);
uint32_t a64_bcond_rel(A64Cond cond, int32_t disp);      /* +-1 MB */
uint32_t a64_cbz_rel  (int sf, int rt, int32_t disp);
uint32_t a64_cbnz_rel (int sf, int rt, int32_t disp);
uint32_t a64_tbz_rel  (int rt, int bit, int32_t disp);   /* +-32 KB */
uint32_t a64_tbnz_rel (int rt, int bit, int32_t disp);
uint32_t a64_ldr_lit_rel(int size, int rt, int32_t disp); /* LDR (literal), 4 or 8 */

/* --- Label assembler ---------------------------------------------------
 * Branches name a label that may be bound before or after them; every
 * displacement is filled in and range-checked once, by a64_finish.
 * Any failure (an encoder's A64_BAD, an allocation, a label never bound,
 * a displacement out of range) latches `bad`, and the caller declines the
 * function instead of shipping broken code. */
typedef uint32_t A64Label;

typedef struct {
    uint32_t at;      /* byte offset of the instruction */
    A64Label label;
    uint8_t  kind;    /* A64_FIX_* (asm_a64.c) */
} A64Fixup;

typedef struct {
    CodeBuf  *cb;
    int32_t  *label_pos;      /* -1 = not bound yet */
    uint32_t  n_labels, cap_labels;
    A64Fixup *fixups;
    uint32_t  n_fixups, cap_fixups;
    int       bad;
} A64Asm;

void     a64_asm_init(A64Asm *a, CodeBuf *cb);
void     a64_asm_free(A64Asm *a);
/* Append one instruction word; A64_BAD latches `bad`. */
void     a64_emit(A64Asm *a, uint32_t insn);
/* Load any 32- or 64-bit constant into RD (MOVZ / MOVN + MOVK, 1-4 words). */
void     a64_mov_imm(A64Asm *a, int sf, int rd, uint64_t value);
A64Label a64_label_new(A64Asm *a);
void     a64_bind(A64Asm *a, A64Label l);          /* at the current offset */
void     a64_b    (A64Asm *a, A64Label l);
void     a64_bl   (A64Asm *a, A64Label l);
void     a64_bcond(A64Asm *a, A64Cond cond, A64Label l);
void     a64_cbz  (A64Asm *a, int sf, int rt, A64Label l);
void     a64_cbnz (A64Asm *a, int sf, int rt, A64Label l);
void     a64_tbz  (A64Asm *a, int rt, int bit, A64Label l);
void     a64_tbnz (A64Asm *a, int rt, int bit, A64Label l);
void     a64_ldr_lit(A64Asm *a, int size, int rt, A64Label l);
/* Resolve every fixup.  Returns 1 when the code is complete, 0 if anything
 * failed (then `bad` is set). */
int      a64_finish(A64Asm *a);

#endif /* CL_JIT_ASM_A64_H */
