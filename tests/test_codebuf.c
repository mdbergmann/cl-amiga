/* test_codebuf.c — unit tests for the JIT's growable byte buffer. */

#include "test.h"
#include "jit/codebuf.h"

TEST(init_zero_cap_no_alloc)
{
    CodeBuf cb;
    cb_init(&cb, 0);
    ASSERT_EQ_INT(cb.pos, 0);
    ASSERT_EQ_INT(cb.cap, 0);
    ASSERT_EQ_INT(cb.oom, 0);
    ASSERT(cb.buf == NULL);
    cb_free(&cb);
}

TEST(init_with_cap_allocates)
{
    CodeBuf cb;
    cb_init(&cb, 32);
    ASSERT_EQ_INT(cb.pos, 0);
    ASSERT_EQ_INT(cb.cap, 32);
    ASSERT(cb.buf != NULL);
    ASSERT_EQ_INT(cb.oom, 0);
    cb_free(&cb);
}

TEST(emit_u8_appends_one_byte)
{
    CodeBuf cb;
    cb_init(&cb, 0);
    cb_emit_u8(&cb, 0xAB);
    cb_emit_u8(&cb, 0xCD);
    ASSERT_EQ_INT(cb_len(&cb), 2);
    ASSERT_EQ_INT(cb_data(&cb)[0], 0xAB);
    ASSERT_EQ_INT(cb_data(&cb)[1], 0xCD);
    cb_free(&cb);
}

TEST(emit_u16_is_big_endian)
{
    CodeBuf cb;
    cb_init(&cb, 16);
    cb_emit_u16(&cb, 0xABCD);
    ASSERT_EQ_INT(cb_len(&cb), 2);
    ASSERT_EQ_INT(cb_data(&cb)[0], 0xAB);
    ASSERT_EQ_INT(cb_data(&cb)[1], 0xCD);
    cb_free(&cb);
}

TEST(emit_u32_is_big_endian)
{
    CodeBuf cb;
    cb_init(&cb, 16);
    cb_emit_u32(&cb, 0xDEADBEEFu);
    ASSERT_EQ_INT(cb_len(&cb), 4);
    ASSERT_EQ_INT(cb_data(&cb)[0], 0xDE);
    ASSERT_EQ_INT(cb_data(&cb)[1], 0xAD);
    ASSERT_EQ_INT(cb_data(&cb)[2], 0xBE);
    ASSERT_EQ_INT(cb_data(&cb)[3], 0xEF);
    cb_free(&cb);
}

TEST(emit_bytes_block_copy)
{
    CodeBuf cb;
    uint8_t src[5] = { 1, 2, 3, 4, 5 };
    uint32_t i;
    cb_init(&cb, 0);
    cb_emit_bytes(&cb, src, 5);
    ASSERT_EQ_INT(cb_len(&cb), 5);
    for (i = 0; i < 5; i++)
        ASSERT_EQ_INT(cb_data(&cb)[i], (int)src[i]);
    cb_free(&cb);
}

TEST(growth_past_initial_cap_preserves_bytes)
{
    /* Force several rounds of growth; check every byte is intact. */
    CodeBuf cb;
    uint32_t i;
    const uint32_t N = 300;  /* > CB_MIN_GROW (64) → triggers growth */

    cb_init(&cb, 0);
    for (i = 0; i < N; i++)
        cb_emit_u8(&cb, (uint8_t)(i & 0xFF));

    ASSERT_EQ_INT(cb_len(&cb), (int)N);
    ASSERT(cb.cap >= N);
    ASSERT_EQ_INT(cb.oom, 0);

    for (i = 0; i < N; i++) {
        if (cb_data(&cb)[i] != (uint8_t)(i & 0xFF)) {
            ASSERT(0 && "byte at index i not preserved across growth");
            break;
        }
    }
    cb_free(&cb);
}

TEST(growth_in_the_middle_of_a_u32)
{
    /* Initial cap leaves only 2 free bytes; the u32 must trigger growth
     * mid-emit and still land 4 contiguous bytes big-endian. */
    CodeBuf cb;
    cb_init(&cb, 2);
    cb_emit_u8(&cb, 0xAA);
    cb_emit_u8(&cb, 0xBB);
    ASSERT_EQ_INT(cb_len(&cb), 2);
    ASSERT_EQ_INT(cb.cap, 2);

    cb_emit_u32(&cb, 0x12345678u);
    ASSERT_EQ_INT(cb_len(&cb), 6);
    ASSERT(cb.cap >= 6);
    ASSERT_EQ_INT(cb_data(&cb)[0], 0xAA);
    ASSERT_EQ_INT(cb_data(&cb)[1], 0xBB);
    ASSERT_EQ_INT(cb_data(&cb)[2], 0x12);
    ASSERT_EQ_INT(cb_data(&cb)[3], 0x34);
    ASSERT_EQ_INT(cb_data(&cb)[4], 0x56);
    ASSERT_EQ_INT(cb_data(&cb)[5], 0x78);
    cb_free(&cb);
}

TEST(finish_transfers_ownership_and_resets)
{
    CodeBuf cb;
    uint8_t *out;
    uint32_t len = 0xDEADBEEFu;

    cb_init(&cb, 0);
    cb_emit_u16(&cb, 0xCAFE);
    cb_emit_u16(&cb, 0xBABE);

    out = cb_finish(&cb, &len);
    ASSERT(out != NULL);
    ASSERT_EQ_INT(len, 4);
    ASSERT_EQ_INT(out[0], 0xCA);
    ASSERT_EQ_INT(out[1], 0xFE);
    ASSERT_EQ_INT(out[2], 0xBA);
    ASSERT_EQ_INT(out[3], 0xBE);

    /* CodeBuf was reset — it must be safe to immediately reuse. */
    ASSERT(cb.buf == NULL);
    ASSERT_EQ_INT(cb.pos, 0);
    ASSERT_EQ_INT(cb.cap, 0);
    ASSERT_EQ_INT(cb.oom, 0);

    cb_emit_u8(&cb, 0x99);
    ASSERT_EQ_INT(cb_len(&cb), 1);
    ASSERT_EQ_INT(cb_data(&cb)[0], 0x99);

    /* Caller owns the previously-returned buffer; cb_free here only
     * cleans up the new allocation, not `out`. */
    cb_free(&cb);

    /* Free the transferred buffer through the same allocator. */
    {
        extern void platform_free(void *);
        platform_free(out);
    }
}

TEST(finish_with_no_writes_returns_empty)
{
    CodeBuf cb;
    uint8_t *out;
    uint32_t len = 0xFFFFFFFFu;

    cb_init(&cb, 0);
    out = cb_finish(&cb, &len);

    /* Nothing was written → buffer pointer may be NULL, length is 0. */
    ASSERT_EQ_INT(len, 0);
    ASSERT(out == NULL);
    /* And the CodeBuf is in clean reset state. */
    ASSERT_EQ_INT(cb.pos, 0);
    ASSERT_EQ_INT(cb.cap, 0);
}

TEST(free_is_idempotent_on_fresh_codebuf)
{
    CodeBuf cb;
    cb_init(&cb, 0);
    cb_free(&cb);
    cb_free(&cb);  /* must not crash */
    ASSERT(cb.buf == NULL);
}

int main(void)
{
    test_init();
    RUN(init_zero_cap_no_alloc);
    RUN(init_with_cap_allocates);
    RUN(emit_u8_appends_one_byte);
    RUN(emit_u16_is_big_endian);
    RUN(emit_u32_is_big_endian);
    RUN(emit_bytes_block_copy);
    RUN(growth_past_initial_cap_preserves_bytes);
    RUN(growth_in_the_middle_of_a_u32);
    RUN(finish_transfers_ownership_and_resets);
    RUN(finish_with_no_writes_returns_empty);
    RUN(free_is_idempotent_on_fresh_codebuf);
    REPORT();
}
