/* asm_a64.c — AArch64 instruction encoders and the label assembler.
 * See asm_a64.h.  Field layouts are from the Arm ARM (DDI 0487), C4.1;
 * each encoder names the instruction class it builds. */

#include "jit/asm_a64.h"
#include "platform/platform.h"

#define R5(r)  ((uint32_t)(r) & 31u)
#define SF(sf) ((sf) ? 0x80000000u : 0u)

/* --- Moves ------------------------------------------------------------- */

/* Move wide (immediate): sf opc 100101 hw imm16 Rd. */
static uint32_t move_wide(uint32_t opc_base, int sf, int rd, uint32_t imm16,
                          int shift)
{
    if (imm16 > 0xFFFFu) return A64_BAD;
    if (shift != 0 && shift != 16 && shift != 32 && shift != 48) return A64_BAD;
    if (!sf && shift > 16) return A64_BAD;
    return opc_base | SF(sf) | ((uint32_t)(shift / 16) << 21) | (imm16 << 5) | R5(rd);
}

uint32_t a64_movz(int sf, int rd, uint32_t imm16, int shift)
{ return move_wide(0x52800000u, sf, rd, imm16, shift); }
uint32_t a64_movk(int sf, int rd, uint32_t imm16, int shift)
{ return move_wide(0x72800000u, sf, rd, imm16, shift); }
uint32_t a64_movn(int sf, int rd, uint32_t imm16, int shift)
{ return move_wide(0x12800000u, sf, rd, imm16, shift); }

/* ORR (shifted register) with the zero register as the first operand. */
uint32_t a64_mov_reg(int sf, int rd, int rm)
{ return 0x2A000000u | SF(sf) | (R5(rm) << 16) | (31u << 5) | R5(rd); }

/* ADD (immediate) #0: the MOV alias that reaches SP. */
uint32_t a64_mov_sp(int rd, int rn)
{ return 0x91000000u | (R5(rn) << 5) | R5(rd); }

/* --- Arithmetic -------------------------------------------------------- */

/* Add/subtract (immediate): sf op S 100010 sh imm12 Rn Rd. */
static uint32_t addsub_imm(uint32_t base, int sf, int rd, int rn, uint32_t imm)
{
    uint32_t sh = 0;
    if (imm > 0xFFFu) {
        if ((imm & 0xFFFu) != 0 || (imm >> 12) > 0xFFFu) return A64_BAD;
        imm >>= 12;
        sh = 1;
    }
    return base | SF(sf) | (sh << 22) | (imm << 10) | (R5(rn) << 5) | R5(rd);
}

uint32_t a64_add_imm (int sf, int rd, int rn, uint32_t imm) { return addsub_imm(0x11000000u, sf, rd, rn, imm); }
uint32_t a64_sub_imm (int sf, int rd, int rn, uint32_t imm) { return addsub_imm(0x51000000u, sf, rd, rn, imm); }
uint32_t a64_adds_imm(int sf, int rd, int rn, uint32_t imm) { return addsub_imm(0x31000000u, sf, rd, rn, imm); }
uint32_t a64_subs_imm(int sf, int rd, int rn, uint32_t imm) { return addsub_imm(0x71000000u, sf, rd, rn, imm); }
uint32_t a64_cmp_imm (int sf, int rn, uint32_t imm)         { return addsub_imm(0x71000000u, sf, A64_ZR, rn, imm); }

/* Add/subtract and logical (shifted register), LSL #0:
 * sf opc 01011/01010 shift N Rm imm6 Rn Rd. */
static uint32_t reg3(uint32_t base, int sf, int rd, int rn, int rm)
{ return base | SF(sf) | (R5(rm) << 16) | (R5(rn) << 5) | R5(rd); }

uint32_t a64_add_reg (int sf, int rd, int rn, int rm) { return reg3(0x0B000000u, sf, rd, rn, rm); }
uint32_t a64_sub_reg (int sf, int rd, int rn, int rm) { return reg3(0x4B000000u, sf, rd, rn, rm); }
uint32_t a64_adds_reg(int sf, int rd, int rn, int rm) { return reg3(0x2B000000u, sf, rd, rn, rm); }
uint32_t a64_subs_reg(int sf, int rd, int rn, int rm) { return reg3(0x6B000000u, sf, rd, rn, rm); }
uint32_t a64_cmp_reg (int sf, int rn, int rm)         { return reg3(0x6B000000u, sf, A64_ZR, rn, rm); }
uint32_t a64_and_reg (int sf, int rd, int rn, int rm) { return reg3(0x0A000000u, sf, rd, rn, rm); }
uint32_t a64_orr_reg (int sf, int rd, int rn, int rm) { return reg3(0x2A000000u, sf, rd, rn, rm); }
uint32_t a64_eor_reg (int sf, int rd, int rn, int rm) { return reg3(0x4A000000u, sf, rd, rn, rm); }
uint32_t a64_tst_reg (int sf, int rn, int rm)         { return reg3(0x6A000000u, sf, A64_ZR, rn, rm); }

/* Add/subtract (extended register): 64-bit, option UXTW (010) / SXTW (110). */
uint32_t a64_add_uxtw(int rd, int rn, int rm, int shift)
{
    if (shift < 0 || shift > 4) return A64_BAD;
    return 0x8B200000u | (R5(rm) << 16) | (2u << 13) | ((uint32_t)shift << 10)
         | (R5(rn) << 5) | R5(rd);
}

uint32_t a64_cmp_sxtw(int rn, int rm)
{ return 0xEB200000u | (R5(rm) << 16) | (6u << 13) | (R5(rn) << 5) | 31u; }

/* Bitfield moves: SBFM / UBFM, sf opc 100110 N immr imms Rn Rd. */
static uint32_t bitfield(uint32_t base32, int sf, int rd, int rn,
                         uint32_t immr, uint32_t imms)
{
    uint32_t base = sf ? (base32 | 0x80400000u) : base32;   /* sf, N */
    return base | (immr << 16) | (imms << 10) | (R5(rn) << 5) | R5(rd);
}

uint32_t a64_asr_imm(int sf, int rd, int rn, int shift)
{
    int width = sf ? 64 : 32;
    if (shift < 0 || shift >= width) return A64_BAD;
    return bitfield(0x13000000u, sf, rd, rn, (uint32_t)shift, (uint32_t)(width - 1));
}

uint32_t a64_lsr_imm(int sf, int rd, int rn, int shift)
{
    int width = sf ? 64 : 32;
    if (shift < 0 || shift >= width) return A64_BAD;
    return bitfield(0x53000000u, sf, rd, rn, (uint32_t)shift, (uint32_t)(width - 1));
}

uint32_t a64_lsl_imm(int sf, int rd, int rn, int shift)
{
    int width = sf ? 64 : 32;
    if (shift < 0 || shift >= width) return A64_BAD;
    return bitfield(0x53000000u, sf, rd, rn,
                    (uint32_t)((width - shift) % width), (uint32_t)(width - 1 - shift));
}

/* SMADDL xd, wn, wm, xzr. */
uint32_t a64_smull(int rd, int rn, int rm)
{ return 0x9B207C00u | (R5(rm) << 16) | (R5(rn) << 5) | R5(rd); }

/* Conditional select: sf 0 0 11010100 Rm cond 0 o2 Rn Rd. */
uint32_t a64_csel(int sf, int rd, int rn, int rm, A64Cond cond)
{
    return 0x1A800000u | SF(sf) | (R5(rm) << 16) | ((uint32_t)cond << 12)
         | (R5(rn) << 5) | R5(rd);
}

/* CSINC rd, zr, zr, invert(cond). */
uint32_t a64_cset(int sf, int rd, A64Cond cond)
{
    if (cond >= A64_AL) return A64_BAD;
    return 0x1A800400u | SF(sf) | (31u << 16) | ((uint32_t)A64_INVERT(cond) << 12)
         | (31u << 5) | R5(rd);
}

/* --- Loads and stores -------------------------------------------------- */

static int size_bits(int size)
{
    switch (size) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: return -1;
    }
}

/* Load/store register (unsigned immediate): size 111 0 01 opc imm12 Rn Rt. */
static uint32_t ldst_uoff(int load, int size, int rt, int rn, uint32_t off)
{
    int sb = size_bits(size);
    if (sb < 0 || (off % (uint32_t)size) != 0 || off / (uint32_t)size > 0xFFFu)
        return A64_BAD;
    return 0x39000000u | ((uint32_t)sb << 30) | ((load ? 1u : 0u) << 22)
         | ((off / (uint32_t)size) << 10) | (R5(rn) << 5) | R5(rt);
}

uint32_t a64_ldr_uoff(int size, int rt, int rn, uint32_t off) { return ldst_uoff(1, size, rt, rn, off); }
uint32_t a64_str_uoff(int size, int rt, int rn, uint32_t off) { return ldst_uoff(0, size, rt, rn, off); }

/* Load/store register (unscaled / post-index / pre-index):
 * size 111 0 00 opc 0 imm9 mode Rn Rt, mode 00 = unscaled, 01 = post,
 * 11 = pre. */
static uint32_t ldst_imm9(int load, int mode, int size, int rt, int rn, int32_t off)
{
    int sb = size_bits(size);
    if (sb < 0 || off < -256 || off > 255) return A64_BAD;
    return 0x38000000u | ((uint32_t)sb << 30) | ((load ? 1u : 0u) << 22)
         | (((uint32_t)off & 0x1FFu) << 12) | ((uint32_t)mode << 10)
         | (R5(rn) << 5) | R5(rt);
}

uint32_t a64_ldur(int size, int rt, int rn, int32_t off) { return ldst_imm9(1, 0, size, rt, rn, off); }
uint32_t a64_stur(int size, int rt, int rn, int32_t off) { return ldst_imm9(0, 0, size, rt, rn, off); }

uint32_t a64_ldr_post(int size, int rt, int rn, int32_t off)
{ return (size == 4 || size == 8) ? ldst_imm9(1, 1, size, rt, rn, off) : A64_BAD; }
uint32_t a64_str_post(int size, int rt, int rn, int32_t off)
{ return (size == 4 || size == 8) ? ldst_imm9(0, 1, size, rt, rn, off) : A64_BAD; }
uint32_t a64_ldr_pre(int size, int rt, int rn, int32_t off)
{ return (size == 4 || size == 8) ? ldst_imm9(1, 3, size, rt, rn, off) : A64_BAD; }
uint32_t a64_str_pre(int size, int rt, int rn, int32_t off)
{ return (size == 4 || size == 8) ? ldst_imm9(0, 3, size, rt, rn, off) : A64_BAD; }

/* Load/store register (register offset), 32-bit, option UXTW, S = 0. */
uint32_t a64_ldr_w_uxtw(int rt, int rn, int rm)
{ return 0xB8604800u | (R5(rm) << 16) | (R5(rn) << 5) | R5(rt); }
uint32_t a64_str_w_uxtw(int rt, int rn, int rm)
{ return 0xB8204800u | (R5(rm) << 16) | (R5(rn) << 5) | R5(rt); }

/* Load/store pair, 64-bit: opc=10 101 0 mode L imm7 Rt2 Rn Rt. */
static uint32_t ldstp(uint32_t base, int rt, int rt2, int rn, int32_t off)
{
    if ((off % 8) != 0 || off < -512 || off > 504) return A64_BAD;
    return base | ((((uint32_t)(off / 8)) & 0x7Fu) << 15) | (R5(rt2) << 10)
         | (R5(rn) << 5) | R5(rt);
}

uint32_t a64_stp_pre (int rt, int rt2, int rn, int32_t off) { return ldstp(0xA9800000u, rt, rt2, rn, off); }
uint32_t a64_ldp_post(int rt, int rt2, int rn, int32_t off) { return ldstp(0xA8C00000u, rt, rt2, rn, off); }
uint32_t a64_stp_off (int rt, int rt2, int rn, int32_t off) { return ldstp(0xA9000000u, rt, rt2, rn, off); }
uint32_t a64_ldp_off (int rt, int rt2, int rn, int32_t off) { return ldstp(0xA9400000u, rt, rt2, rn, off); }

/* --- Control ----------------------------------------------------------- */

uint32_t a64_ret(void)      { return 0xD65F03C0u; }
uint32_t a64_br (int rn)    { return 0xD61F0000u | (R5(rn) << 5); }
uint32_t a64_blr(int rn)    { return 0xD63F0000u | (R5(rn) << 5); }
uint32_t a64_nop(void)      { return 0xD503201Fu; }
uint32_t a64_dmb_ish(void)  { return 0xD5033BBFu; }
uint32_t a64_brk(uint32_t imm16)
{ return (imm16 > 0xFFFFu) ? A64_BAD : (0xD4200000u | (imm16 << 5)); }

/* A word-aligned displacement that fits a signed BITS-bit word count. */
static int disp_fits(int32_t disp, int bits)
{
    int32_t words, lim;
    if ((disp & 3) != 0) return 0;
    words = disp / 4;
    lim = (int32_t)1 << (bits - 1);
    return words >= -lim && words < lim;
}

static uint32_t disp_field(int32_t disp, int bits)
{ return ((uint32_t)(disp / 4)) & (((uint32_t)1 << bits) - 1u); }

uint32_t a64_b_rel(int32_t disp)
{ return disp_fits(disp, 26) ? (0x14000000u | disp_field(disp, 26)) : A64_BAD; }
uint32_t a64_bl_rel(int32_t disp)
{ return disp_fits(disp, 26) ? (0x94000000u | disp_field(disp, 26)) : A64_BAD; }

uint32_t a64_bcond_rel(A64Cond cond, int32_t disp)
{
    if (!disp_fits(disp, 19)) return A64_BAD;
    return 0x54000000u | (disp_field(disp, 19) << 5) | (uint32_t)cond;
}

uint32_t a64_cbz_rel(int sf, int rt, int32_t disp)
{ return disp_fits(disp, 19) ? (0x34000000u | SF(sf) | (disp_field(disp, 19) << 5) | R5(rt)) : A64_BAD; }
uint32_t a64_cbnz_rel(int sf, int rt, int32_t disp)
{ return disp_fits(disp, 19) ? (0x35000000u | SF(sf) | (disp_field(disp, 19) << 5) | R5(rt)) : A64_BAD; }

static uint32_t test_branch(uint32_t base, int rt, int bit, int32_t disp)
{
    if (bit < 0 || bit > 63 || !disp_fits(disp, 14)) return A64_BAD;
    return base | ((uint32_t)(bit >> 5) << 31) | ((uint32_t)(bit & 31) << 19)
         | (disp_field(disp, 14) << 5) | R5(rt);
}

uint32_t a64_tbz_rel (int rt, int bit, int32_t disp) { return test_branch(0x36000000u, rt, bit, disp); }
uint32_t a64_tbnz_rel(int rt, int bit, int32_t disp) { return test_branch(0x37000000u, rt, bit, disp); }

uint32_t a64_ldr_lit_rel(int size, int rt, int32_t disp)
{
    uint32_t base;
    if (size == 4) base = 0x18000000u;
    else if (size == 8) base = 0x58000000u;
    else return A64_BAD;
    return disp_fits(disp, 19) ? (base | (disp_field(disp, 19) << 5) | R5(rt)) : A64_BAD;
}

/* --- Label assembler --------------------------------------------------- */

enum { A64_FIX_B26, A64_FIX_BL26, A64_FIX_BCOND, A64_FIX_CBZ, A64_FIX_CBNZ,
       A64_FIX_TBZ, A64_FIX_TBNZ, A64_FIX_LDR_LIT };

void a64_asm_init(A64Asm *a, CodeBuf *cb)
{
    a->cb = cb;
    a->label_pos = NULL;
    a->n_labels = a->cap_labels = 0;
    a->fixups = NULL;
    a->n_fixups = a->cap_fixups = 0;
    a->bad = 0;
}

void a64_asm_free(A64Asm *a)
{
    if (a->label_pos) platform_free(a->label_pos);
    if (a->fixups) platform_free(a->fixups);
    a->label_pos = NULL;
    a->fixups = NULL;
    a->n_labels = a->cap_labels = a->n_fixups = a->cap_fixups = 0;
}

void a64_emit(A64Asm *a, uint32_t insn)
{
    if (insn == A64_BAD) { a->bad = 1; return; }
    cb_emit_u32_le(a->cb, insn);
    if (a->cb->oom) a->bad = 1;
}

void a64_mov_imm(A64Asm *a, int sf, int rd, uint64_t value)
{
    int halves = sf ? 4 : 2, zeros = 0, ones = 0, i, first = 1, inverted;
    uint32_t fill;
    if (!sf) value &= 0xFFFFFFFFu;
    for (i = 0; i < halves; i++) {
        uint32_t h = (uint32_t)(value >> (16 * i)) & 0xFFFFu;
        if (h == 0) zeros++;
        if (h == 0xFFFFu) ones++;
    }
    /* MOVN when more halfwords are all-ones than all-zero: fewer MOVKs. */
    inverted = ones > zeros;
    fill = inverted ? 0xFFFFu : 0;
    for (i = 0; i < halves; i++) {
        uint32_t h = (uint32_t)(value >> (16 * i)) & 0xFFFFu;
        if (h == fill) continue;
        if (first) {
            a64_emit(a, inverted ? a64_movn(sf, rd, (~h) & 0xFFFFu, 16 * i)
                                 : a64_movz(sf, rd, h, 16 * i));
            first = 0;
        } else {
            a64_emit(a, a64_movk(sf, rd, h, 16 * i));
        }
    }
    if (first)   /* every halfword is the fill: 0 or all ones */
        a64_emit(a, inverted ? a64_movn(sf, rd, 0, 0) : a64_movz(sf, rd, 0, 0));
}

A64Label a64_label_new(A64Asm *a)
{
    if (a->n_labels == a->cap_labels) {
        uint32_t cap = a->cap_labels ? a->cap_labels * 2 : 16, i;
        int32_t *grown = (int32_t *)platform_alloc((unsigned long)cap * sizeof(int32_t));
        if (grown == NULL) { a->bad = 1; return 0; }
        for (i = 0; i < a->n_labels; i++) grown[i] = a->label_pos[i];
        if (a->label_pos) platform_free(a->label_pos);
        a->label_pos = grown;
        a->cap_labels = cap;
    }
    a->label_pos[a->n_labels] = -1;
    return a->n_labels++;
}

void a64_bind(A64Asm *a, A64Label l)
{
    if (l >= a->n_labels || a->label_pos[l] >= 0) { a->bad = 1; return; }
    a->label_pos[l] = (int32_t)cb_len(a->cb);
}

/* Emit a placeholder word and remember to patch it. */
static void emit_fixup(A64Asm *a, uint8_t kind, A64Label l, uint32_t placeholder)
{
    if (a->n_fixups == a->cap_fixups) {
        uint32_t cap = a->cap_fixups ? a->cap_fixups * 2 : 16, i;
        A64Fixup *grown = (A64Fixup *)platform_alloc((unsigned long)cap * sizeof(A64Fixup));
        if (grown == NULL) { a->bad = 1; return; }
        for (i = 0; i < a->n_fixups; i++) grown[i] = a->fixups[i];
        if (a->fixups) platform_free(a->fixups);
        a->fixups = grown;
        a->cap_fixups = cap;
    }
    a->fixups[a->n_fixups].at = cb_len(a->cb);
    a->fixups[a->n_fixups].label = l;
    a->fixups[a->n_fixups].kind = kind;
    a->n_fixups++;
    a64_emit(a, placeholder);
}

/* The placeholder keeps the operand fields (register, condition, bit) so
 * that finish only has to add the displacement. */
void a64_b(A64Asm *a, A64Label l)     { emit_fixup(a, A64_FIX_B26, l, a64_b_rel(0)); }
void a64_bl(A64Asm *a, A64Label l)    { emit_fixup(a, A64_FIX_BL26, l, a64_bl_rel(0)); }
void a64_bcond(A64Asm *a, A64Cond cond, A64Label l)
{ emit_fixup(a, A64_FIX_BCOND, l, a64_bcond_rel(cond, 0)); }
void a64_cbz(A64Asm *a, int sf, int rt, A64Label l)
{ emit_fixup(a, A64_FIX_CBZ, l, a64_cbz_rel(sf, rt, 0)); }
void a64_cbnz(A64Asm *a, int sf, int rt, A64Label l)
{ emit_fixup(a, A64_FIX_CBNZ, l, a64_cbnz_rel(sf, rt, 0)); }
void a64_tbz(A64Asm *a, int rt, int bit, A64Label l)
{ emit_fixup(a, A64_FIX_TBZ, l, a64_tbz_rel(rt, bit, 0)); }
void a64_tbnz(A64Asm *a, int rt, int bit, A64Label l)
{ emit_fixup(a, A64_FIX_TBNZ, l, a64_tbnz_rel(rt, bit, 0)); }
void a64_ldr_lit(A64Asm *a, int size, int rt, A64Label l)
{ emit_fixup(a, A64_FIX_LDR_LIT, l, a64_ldr_lit_rel(size, rt, 0)); }

static uint32_t read_le32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void write_le32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

int a64_finish(A64Asm *a)
{
    uint32_t i;
    uint8_t *code;
    if (a->bad || a->cb->oom) { a->bad = 1; return 0; }
    code = cb_data(a->cb);
    for (i = 0; i < a->n_fixups; i++) {
        const A64Fixup *f = &a->fixups[i];
        uint32_t word, insn, rt, b40, b5;
        int32_t target, disp;
        if (f->label >= a->n_labels || a->label_pos[f->label] < 0) { a->bad = 1; return 0; }
        target = a->label_pos[f->label];
        disp = target - (int32_t)f->at;
        word = read_le32(code + f->at);
        rt = word & 31u;
        switch (f->kind) {
        case A64_FIX_B26:   insn = a64_b_rel(disp); break;
        case A64_FIX_BL26:  insn = a64_bl_rel(disp); break;
        case A64_FIX_BCOND: insn = a64_bcond_rel((A64Cond)(word & 15u), disp); break;
        case A64_FIX_CBZ:   insn = a64_cbz_rel((word >> 31) & 1, (int)rt, disp); break;
        case A64_FIX_CBNZ:  insn = a64_cbnz_rel((word >> 31) & 1, (int)rt, disp); break;
        case A64_FIX_TBZ:
        case A64_FIX_TBNZ:
            b5 = (word >> 31) & 1u;
            b40 = (word >> 19) & 31u;
            insn = (f->kind == A64_FIX_TBZ ? a64_tbz_rel : a64_tbnz_rel)(
                       (int)rt, (int)((b5 << 5) | b40), disp);
            break;
        case A64_FIX_LDR_LIT:
            insn = a64_ldr_lit_rel(((word >> 30) & 1u) ? 8 : 4, (int)rt, disp);
            break;
        default: insn = A64_BAD; break;
        }
        if (insn == A64_BAD) { a->bad = 1; return 0; }
        write_le32(code + f->at, insn);
    }
    return 1;
}
