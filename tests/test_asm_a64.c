/* test_asm_a64.c — the AArch64 encoders and label assembler (asm_a64.c).
 *
 * The table pins every encoder against the word clang assembles for the
 * same instruction (`clang -c -arch arm64`, recorded once when the encoder
 * was written; no assembler is needed to run this test).  The rest checks
 * the A64_BAD contract on operands that do not fit, the MOVZ/MOVN/MOVK
 * synthesis of wide constants, and branch fixups through the label
 * assembler.  Pure encoding: runs on every host, not only arm64.
 * See specs/native-backend-a64.md. */

#include "test.h"
#include "jit/asm_a64.h"

typedef struct {
    const char *asm_text;
    uint32_t    ours;
    uint32_t    clang;
} EncCase;

TEST(every_encoder_matches_clang)
{
    const EncCase cases[] = {   /* C99: a local aggregate may call functions */
    { "movz w0, #0", a64_movz(0, 0, 0, 0), 0x52800000U },
    { "movz w3, #0xbeef, lsl #16", a64_movz(0, 3, 0xBEEF, 16), 0x52B7DDE3U },
    { "movz x9, #0x1234, lsl #48", a64_movz(1, 9, 0x1234, 48), 0xD2E24689U },
    { "movk w1, #0xabcd", a64_movk(0, 1, 0xABCD, 0), 0x729579A1U },
    { "movk x22, #0xffff, lsl #32", a64_movk(1, 22, 0xFFFF, 32), 0xF2DFFFF6U },
    { "movn w2, #5", a64_movn(0, 2, 5, 0), 0x128000A2U },
    { "movn x2, #0, lsl #16", a64_movn(1, 2, 0, 16), 0x92A00002U },
    { "mov w0, w19", a64_mov_reg(0, 0, 19), 0x2A1303E0U },
    { "mov x29, x1", a64_mov_reg(1, 29, 1), 0xAA0103FDU },
    { "mov x29, sp", a64_mov_sp(29, 31), 0x910003FDU },
    { "mov sp, x29", a64_mov_sp(31, 29), 0x910003BFU },
    { "add w1, w2, #4095", a64_add_imm(0, 1, 2, 4095), 0x113FFC41U },
    { "add x20, x20, #4", a64_add_imm(1, 20, 20, 4), 0x91001294U },
    { "add sp, sp, #16, lsl #12", a64_add_imm(1, 31, 31, 0x10000), 0x914043FFU },
    { "sub w9, w10, #1", a64_sub_imm(0, 9, 10, 1), 0x51000549U },
    { "sub sp, sp, #96", a64_sub_imm(1, 31, 31, 96), 0xD10183FFU },
    { "adds w0, w1, #7", a64_adds_imm(0, 0, 1, 7), 0x31001C20U },
    { "subs x3, x4, #2, lsl #12", a64_subs_imm(1, 3, 4, 0x2000), 0xF1400883U },
    { "cmp w9, #0", a64_cmp_imm(0, 9, 0), 0x7100013FU },
    { "cmp x9, #255", a64_cmp_imm(1, 9, 255), 0xF103FD3FU },
    { "add w0, w1, w2", a64_add_reg(0, 0, 1, 2), 0x0B020020U },
    { "add x0, x1, x2", a64_add_reg(1, 0, 1, 2), 0x8B020020U },
    { "sub w5, w6, w7", a64_sub_reg(0, 5, 6, 7), 0x4B0700C5U },
    { "adds w0, w9, w10", a64_adds_reg(0, 0, 9, 10), 0x2B0A0120U },
    { "subs x11, x12, x13", a64_subs_reg(1, 11, 12, 13), 0xEB0D018BU },
    { "cmp w9, w10", a64_cmp_reg(0, 9, 10), 0x6B0A013FU },
    { "cmp x20, x21", a64_cmp_reg(1, 20, 21), 0xEB15029FU },
    { "add x9, x23, w1, uxtw", a64_add_uxtw(9, 23, 1, 0), 0x8B2142E9U },
    { "add x9, x23, w1, uxtw #2", a64_add_uxtw(9, 23, 1, 2), 0x8B214AE9U },
    { "cmp x11, w11, sxtw", a64_cmp_sxtw(11, 11), 0xEB2BC17FU },
    { "and w9, w0, w1", a64_and_reg(0, 9, 0, 1), 0x0A010009U },
    { "orr w9, w9, w10", a64_orr_reg(0, 9, 9, 10), 0x2A0A0129U },
    { "eor x2, x3, x4", a64_eor_reg(1, 2, 3, 4), 0xCA040062U },
    { "tst w9, w10", a64_tst_reg(0, 9, 10), 0x6A0A013FU },
    { "asr w9, w0, #1", a64_asr_imm(0, 9, 0, 1), 0x13017C09U },
    { "asr x9, x0, #63", a64_asr_imm(1, 9, 0, 63), 0x937FFC09U },
    { "lsl w1, w2, #1", a64_lsl_imm(0, 1, 2, 1), 0x531F7841U },
    { "lsl x1, x2, #3", a64_lsl_imm(1, 1, 2, 3), 0xD37DF041U },
    { "lsl w1, w2, #31", a64_lsl_imm(0, 1, 2, 31), 0x53010041U },
    { "lsr w1, w2, #8", a64_lsr_imm(0, 1, 2, 8), 0x53087C41U },
    { "lsr x1, x2, #32", a64_lsr_imm(1, 1, 2, 32), 0xD360FC41U },
    { "smull x11, w9, w10", a64_smull(11, 9, 10), 0x9B2A7D2BU },
    { "csel w0, w1, w2, lt", a64_csel(0, 0, 1, 2, A64_LT), 0x1A82B020U },
    { "csel x0, x1, x2, eq", a64_csel(1, 0, 1, 2, A64_EQ), 0x9A820020U },
    { "cset w0, eq", a64_cset(0, 0, A64_EQ), 0x1A9F17E0U },
    { "cset w9, gt", a64_cset(0, 9, A64_GT), 0x1A9FD7E9U },
    { "cset x9, lo", a64_cset(1, 9, A64_LO), 0x9A9F27E9U },
    { "ldrb w9, [x19, #7]", a64_ldr_uoff(1, 9, 19, 7), 0x39401E69U },
    { "ldrh w9, [x19, #6]", a64_ldr_uoff(2, 9, 19, 6), 0x79400E69U },
    { "ldr w0, [x21, #16380]", a64_ldr_uoff(4, 0, 21, 16380), 0xB97FFEA0U },
    { "ldr x16, [x22, #32760]", a64_ldr_uoff(8, 16, 22, 32760), 0xF97FFED0U },
    { "strb w9, [x19]", a64_str_uoff(1, 9, 19, 0), 0x39000269U },
    { "strh w9, [x19, #2]", a64_str_uoff(2, 9, 19, 2), 0x79000669U },
    { "str w0, [x20, #4]", a64_str_uoff(4, 0, 20, 4), 0xB9000680U },
    { "str x30, [sp, #8]", a64_str_uoff(8, 30, 31, 8), 0xF90007FEU },
    { "ldur w0, [x20, #-4]", a64_ldur(4, 0, 20, -4), 0xB85FC280U },
    { "stur w0, [x20, #-256]", a64_stur(4, 0, 20, -256), 0xB8100280U },
    { "ldur x1, [x2, #255]", a64_ldur(8, 1, 2, 255), 0xF84FF041U },
    { "str w0, [x20], #4", a64_str_post(4, 0, 20, 4), 0xB8004680U },
    { "ldr w0, [x20], #-4", a64_ldr_post(4, 0, 20, -4), 0xB85FC680U },
    { "ldr w0, [x20, #-4]!", a64_ldr_pre(4, 0, 20, -4), 0xB85FCE80U },
    { "str x30, [sp, #-16]!", a64_str_pre(8, 30, 31, -16), 0xF81F0FFEU },
    { "ldr x30, [sp], #16", a64_ldr_post(8, 30, 31, 16), 0xF84107FEU },
    { "ldr w0, [x23, w1, uxtw]", a64_ldr_w_uxtw(0, 23, 1), 0xB8614AE0U },
    { "str w2, [x23, w3, uxtw]", a64_str_w_uxtw(2, 23, 3), 0xB8234AE2U },
    { "stp x29, x30, [sp, #-96]!", a64_stp_pre(29, 30, 31, -96), 0xA9BA7BFDU },
    { "ldp x29, x30, [sp], #96", a64_ldp_post(29, 30, 31, 96), 0xA8C67BFDU },
    { "stp x19, x20, [sp, #16]", a64_stp_off(19, 20, 31, 16), 0xA90153F3U },
    { "ldp x25, x26, [sp, #504]", a64_ldp_off(25, 26, 31, 504), 0xA95FEBF9U },
    { "stp x19, x20, [sp, #-512]!", a64_stp_pre(19, 20, 31, -512), 0xA9A053F3U },
    { "ret", a64_ret(), 0xD65F03C0U },
    { "br x16", a64_br(16), 0xD61F0200U },
    { "blr x16", a64_blr(16), 0xD63F0200U },
    { "nop", a64_nop(), 0xD503201FU },
    { "dmb ish", a64_dmb_ish(), 0xD5033BBFU },
    { "brk #0x3e8", a64_brk(0x3e8), 0xD4207D00U },
    { "b .+8", a64_b_rel(8), 0x14000002U },
    { "b .-4", a64_b_rel(-4), 0x17FFFFFFU },
    { "b .+0x7fffffc", a64_b_rel(0x7FFFFFC), 0x15FFFFFFU },
    { "bl .-0x8000000", a64_bl_rel(-0x8000000), 0x96000000U },
    { "b.ne .+12", a64_bcond_rel(A64_NE, 12), 0x54000061U },
    { "b.vs .-16", a64_bcond_rel(A64_VS, -16), 0x54FFFF86U },
    { "b.le .+0xffffc", a64_bcond_rel(A64_LE, 0xFFFFC), 0x547FFFEDU },
    { "cbz w9, .+8", a64_cbz_rel(0, 9, 8), 0x34000049U },
    { "cbnz x9, .-8", a64_cbnz_rel(1, 9, -8), 0xB5FFFFC9U },
    { "tbz w9, #0, .+16", a64_tbz_rel(9, 0, 16), 0x36000089U },
    { "tbnz x9, #33, .-32", a64_tbnz_rel(9, 33, -32), 0xB70FFF09U },
    { "tbz w1, #31, .+0x7ffc", a64_tbz_rel(1, 31, 0x7FFC), 0x36FBFFE1U },
    { "ldr w0, .+8", a64_ldr_lit_rel(4, 0, 8), 0x18000040U },
    { "ldr x16, .-12", a64_ldr_lit_rel(8, 16, -12), 0x58FFFFB0U },
    };
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (cases[i].ours != cases[i].clang)
            printf("    %-32s ours %08X clang %08X\n", cases[i].asm_text,
                   (unsigned)cases[i].ours, (unsigned)cases[i].clang);
        ASSERT_EQ_INT(cases[i].ours, cases[i].clang);
    }
}

/* Operands that do not fit their field yield A64_BAD, never a wrong word. */
TEST(out_of_range_operands_are_bad)
{
    ASSERT_EQ_INT(a64_movz(0, 0, 0x10000, 0), A64_BAD);     /* imm16 too wide */
    ASSERT_EQ_INT(a64_movz(0, 0, 1, 32), A64_BAD);          /* W has 2 halves */
    ASSERT_EQ_INT(a64_movk(1, 0, 1, 8), A64_BAD);           /* not 0/16/32/48 */
    ASSERT_EQ_INT(a64_add_imm(1, 0, 0, 4097), A64_BAD);     /* neither form */
    ASSERT_EQ_INT(a64_sub_imm(1, 0, 0, 0x1000000), A64_BAD);
    ASSERT_EQ_INT(a64_add_uxtw(0, 1, 2, 5), A64_BAD);
    ASSERT_EQ_INT(a64_asr_imm(0, 0, 0, 32), A64_BAD);
    ASSERT_EQ_INT(a64_lsl_imm(1, 0, 0, 64), A64_BAD);
    ASSERT_EQ_INT(a64_lsr_imm(0, 0, 0, -1), A64_BAD);
    ASSERT_EQ_INT(a64_cset(0, 0, A64_AL), A64_BAD);
    ASSERT_EQ_INT(a64_ldr_uoff(4, 0, 1, 2), A64_BAD);       /* misaligned */
    ASSERT_EQ_INT(a64_ldr_uoff(4, 0, 1, 16384), A64_BAD);   /* 4096 words */
    ASSERT_EQ_INT(a64_str_uoff(3, 0, 1, 0), A64_BAD);       /* no such size */
    ASSERT_EQ_INT(a64_ldur(4, 0, 1, 256), A64_BAD);
    ASSERT_EQ_INT(a64_stur(4, 0, 1, -257), A64_BAD);
    ASSERT_EQ_INT(a64_str_post(2, 0, 1, 2), A64_BAD);       /* 4 or 8 only */
    ASSERT_EQ_INT(a64_stp_pre(19, 20, 31, -520), A64_BAD);
    ASSERT_EQ_INT(a64_ldp_off(19, 20, 31, 4), A64_BAD);     /* not 8-aligned */
    ASSERT_EQ_INT(a64_brk(0x10000), A64_BAD);
    ASSERT_EQ_INT(a64_b_rel(2), A64_BAD);                   /* not word-aligned */
    ASSERT_EQ_INT(a64_b_rel(0x8000000), A64_BAD);
    ASSERT_EQ_INT(a64_bcond_rel(A64_EQ, 0x100000), A64_BAD);
    ASSERT_EQ_INT(a64_cbz_rel(0, 0, -0x100004), A64_BAD);
    ASSERT_EQ_INT(a64_tbz_rel(0, 0, 0x8000), A64_BAD);
    ASSERT_EQ_INT(a64_tbnz_rel(0, 64, 4), A64_BAD);
    ASSERT_EQ_INT(a64_ldr_lit_rel(2, 0, 4), A64_BAD);
}

static uint32_t word_at(CodeBuf *cb, uint32_t i)
{
    const uint8_t *p = cb_data(cb) + 4 * i;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* a64_mov_imm picks MOVZ or MOVN by which needs fewer MOVKs. */
TEST(mov_imm_synthesises_wide_constants)
{
    CodeBuf cb;
    A64Asm a;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    a64_mov_imm(&a, 0, 0, 0);                       /* movz w0, #0 */
    a64_mov_imm(&a, 0, 1, 0x12340000u);             /* movz w1, #0x1234, lsl #16 */
    a64_mov_imm(&a, 0, 2, 0xFFFFFFFEu);             /* movn w2, #1 */
    a64_mov_imm(&a, 1, 3, 0x0000123400005678ull);   /* movz + movk */
    a64_mov_imm(&a, 1, 4, 0xFFFFFFFFFFFF1234ull);   /* movn x4, #0xedcb */
    a64_mov_imm(&a, 1, 5, 0xFFFFFFFFFFFFFFFFull);   /* movn x5, #0 */
    ASSERT_EQ_INT(a.bad, 0);
    ASSERT_EQ_INT(cb_len(&cb), 4 * 7);
    ASSERT_EQ_INT(word_at(&cb, 0), a64_movz(0, 0, 0, 0));
    ASSERT_EQ_INT(word_at(&cb, 1), a64_movz(0, 1, 0x1234, 16));
    ASSERT_EQ_INT(word_at(&cb, 2), a64_movn(0, 2, 1, 0));
    ASSERT_EQ_INT(word_at(&cb, 3), a64_movz(1, 3, 0x5678, 0));
    ASSERT_EQ_INT(word_at(&cb, 4), a64_movk(1, 3, 0x1234, 32));
    ASSERT_EQ_INT(word_at(&cb, 5), a64_movn(1, 4, 0xEDCB, 0));
    ASSERT_EQ_INT(word_at(&cb, 6), a64_movn(1, 5, 0, 0));
    a64_asm_free(&a);
    cb_free(&cb);
}

/* Forward and backward branches of every fixup kind resolve to the same
 * words the PC-relative encoders produce for the same distances. */
TEST(label_fixups_resolve_both_directions)
{
    CodeBuf cb;
    A64Asm a;
    A64Label top, fwd;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    top = a64_label_new(&a);
    fwd = a64_label_new(&a);
    a64_bind(&a, top);                       /* word 0 */
    a64_emit(&a, a64_nop());
    a64_b(&a, fwd);                          /* word 1 -> 9: +32 */
    a64_bcond(&a, A64_NE, fwd);              /* word 2: +28 */
    a64_cbz(&a, 0, 9, fwd);                  /* word 3: +24 */
    a64_cbnz(&a, 1, 10, top);                /* word 4: -16 */
    a64_tbz(&a, 9, 0, fwd);                  /* word 5: +16 */
    a64_tbnz(&a, 11, 40, top);               /* word 6: -24 */
    a64_ldr_lit(&a, 8, 16, fwd);             /* word 7: +8 */
    a64_bl(&a, top);                         /* word 8: -32 */
    a64_bind(&a, fwd);                       /* word 9 */
    a64_emit(&a, a64_ret());
    ASSERT_EQ_INT(a64_finish(&a), 1);
    ASSERT_EQ_INT(word_at(&cb, 1), a64_b_rel(32));
    ASSERT_EQ_INT(word_at(&cb, 2), a64_bcond_rel(A64_NE, 28));
    ASSERT_EQ_INT(word_at(&cb, 3), a64_cbz_rel(0, 9, 24));
    ASSERT_EQ_INT(word_at(&cb, 4), a64_cbnz_rel(1, 10, -16));
    ASSERT_EQ_INT(word_at(&cb, 5), a64_tbz_rel(9, 0, 16));
    ASSERT_EQ_INT(word_at(&cb, 6), a64_tbnz_rel(11, 40, -24));
    ASSERT_EQ_INT(word_at(&cb, 7), a64_ldr_lit_rel(8, 16, 8));
    ASSERT_EQ_INT(word_at(&cb, 8), a64_bl_rel(-32));
    ASSERT_EQ_INT(word_at(&cb, 9), a64_ret());
    a64_asm_free(&a);
    cb_free(&cb);
}

/* A label used but never bound fails finish instead of leaving a zero
 * displacement (a branch to itself) in the code. */
TEST(unbound_label_fails_finish)
{
    CodeBuf cb;
    A64Asm a;
    A64Label l;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    l = a64_label_new(&a);
    a64_b(&a, l);
    ASSERT_EQ_INT(a64_finish(&a), 0);
    ASSERT_EQ_INT(a.bad, 1);
    a64_asm_free(&a);
    cb_free(&cb);
}

/* A test-bit branch farther than +-32 KB cannot be encoded: finish fails. */
TEST(out_of_range_fixup_fails_finish)
{
    CodeBuf cb;
    A64Asm a;
    A64Label l;
    int i;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    l = a64_label_new(&a);
    a64_tbz(&a, 0, 0, l);
    for (i = 0; i < 8192; i++) a64_emit(&a, a64_nop());   /* 32 KB */
    a64_bind(&a, l);
    ASSERT_EQ_INT(a64_finish(&a), 0);
    a64_asm_free(&a);
    cb_free(&cb);
}

/* An encoder's A64_BAD reaching a64_emit latches `bad` and emits nothing;
 * binding a label twice is an error too. */
TEST(bad_word_and_double_bind_latch)
{
    CodeBuf cb;
    A64Asm a;
    A64Label l;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    a64_emit(&a, a64_add_imm(1, 0, 0, 4097));
    ASSERT_EQ_INT(a.bad, 1);
    ASSERT_EQ_INT(cb_len(&cb), 0);
    a64_asm_free(&a);
    a64_asm_init(&a, &cb);
    l = a64_label_new(&a);
    a64_bind(&a, l);
    a64_bind(&a, l);
    ASSERT_EQ_INT(a.bad, 1);
    ASSERT_EQ_INT(a64_finish(&a), 0);
    a64_asm_free(&a);
    cb_free(&cb);
}

int main(void)
{
    test_init();
    RUN(every_encoder_matches_clang);
    RUN(out_of_range_operands_are_bad);
    RUN(mov_imm_synthesises_wide_constants);
    RUN(label_fixups_resolve_both_directions);
    RUN(unbound_label_fails_finish);
    RUN(out_of_range_fixup_fails_finish);
    RUN(bad_word_and_double_bind_latch);
    REPORT();
}
