/* test_codeheap.c — the AArch64 JIT's executable memory (codeheap.c).
 *
 * Installed code must really execute (MAP_JIT, the write window and the
 * instruction-cache flush all have to be right for that), freed blocks
 * must come back, foreign and double frees must be refused, and shutdown
 * must unmap everything.  Builds without JIT_A64 have no code heap; the
 * test then has nothing to check.  See specs/native-backend-a64.md. */

#include "test.h"
#include "jit/jit.h"

#ifdef JIT_A64

#include <pthread.h>
#include "jit/asm_a64.h"
#include "jit/codeheap.h"
#include "platform/platform.h"

typedef uint32_t (*ret_fn)(void);

/* Assemble `mov w0, #value; ret` and install it. */
static void *install_return(uint32_t value, uint32_t padding_words)
{
    CodeBuf cb;
    A64Asm a;
    uint8_t *code;
    uint32_t len, i;
    void *entry;
    cb_init(&cb, 0);
    a64_asm_init(&a, &cb);
    a64_mov_imm(&a, 0, 0, value);
    a64_emit(&a, a64_ret());
    for (i = 0; i < padding_words; i++) a64_emit(&a, a64_brk(0));
    if (!a64_finish(&a)) { a64_asm_free(&a); cb_free(&cb); return NULL; }
    a64_asm_free(&a);
    code = cb_finish(&cb, &len);
    entry = cl_codeheap_install(code, len);
    platform_free(code);
    return entry;
}

TEST(installed_code_executes)
{
    void *e1 = install_return(42, 0);
    void *e2 = install_return(0xDEADBEEFu, 0);
    ASSERT(e1 != NULL && e2 != NULL);
    ASSERT_EQ_INT(((uintptr_t)e1) % 16, 0);
    ASSERT_EQ_INT(((ret_fn)e1)(), 42);
    ASSERT_EQ_INT(((ret_fn)e2)(), 0xDEADBEEFu);
    cl_codeheap_free(e1);
    cl_codeheap_free(e2);
}

/* A freed block is handed out again, and code written into it runs:
 * the flush after a reuse is what makes the new words visible. */
TEST(freed_block_is_reused_and_runs_new_code)
{
    void *e1 = install_return(1, 0), *e2;
    uint32_t live = cl_codeheap_live_bytes();
    ASSERT(e1 != NULL);
    ((ret_fn)e1)();
    cl_codeheap_free(e1);
    ASSERT(cl_codeheap_live_bytes() < live);
    e2 = install_return(2, 0);
    ASSERT(e2 == e1);
    ASSERT_EQ_INT(((ret_fn)e2)(), 2);
    cl_codeheap_free(e2);
}

/* A large free block is split: a small install takes its front and the
 * rest stays available. */
TEST(large_free_block_is_split)
{
    void *big = install_return(3, 256), *small1, *small2;
    ASSERT(big != NULL);
    cl_codeheap_free(big);
    small1 = install_return(4, 0);
    small2 = install_return(5, 0);
    ASSERT(small1 == big);
    ASSERT(small2 != NULL && small2 != small1);
    ASSERT_EQ_INT(((ret_fn)small1)(), 4);
    ASSERT_EQ_INT(((ret_fn)small2)(), 5);
    cl_codeheap_free(small1);
    cl_codeheap_free(small2);
}

/* Pointers the heap did not hand out, and a second free of the same block,
 * are refused instead of corrupting the free list. */
TEST(foreign_and_double_free_are_refused)
{
    char local[32];
    void *e = install_return(6, 0), *again;
    uint32_t live;
    ASSERT(e != NULL);
    cl_codeheap_free(NULL);
    cl_codeheap_free(local + 16);
    live = cl_codeheap_live_bytes();
    cl_codeheap_free(e);
    ASSERT(cl_codeheap_live_bytes() < live);
    live = cl_codeheap_live_bytes();
    cl_codeheap_free(e);
    ASSERT_EQ_INT(cl_codeheap_live_bytes(), live);
    again = install_return(7, 0);   /* the block is on the list once */
    ASSERT(again == e);
    ASSERT(install_return(8, 0) != again);
    cl_codeheap_free(again);
}

/* A block bigger than a chunk gets a chunk of its own. */
TEST(oversize_block_gets_its_own_chunk)
{
    uint32_t mapped = cl_codeheap_mapped_bytes();
    void *huge = install_return(9, (1u << 20) / 4 + 16);
    ASSERT(huge != NULL);
    ASSERT(cl_codeheap_mapped_bytes() >= mapped + (1u << 20) + 64);
    ASSERT_EQ_INT(((ret_fn)huge)(), 9);
    cl_codeheap_free(huge);
}

/* Threads install, run and free concurrently: the lock and the per-thread
 * write window.  Each checks that its code returns its own value. */
static void *churn(void *arg)
{
    uint32_t base = (uint32_t)(uintptr_t)arg, i, bad = 0;
    for (i = 0; i < 400; i++) {
        void *e = install_return(base + i, i % 7);
        if (e == NULL || ((ret_fn)e)() != base + i) bad++;
        cl_codeheap_free(e);
    }
    return (void *)(uintptr_t)bad;
}

TEST(concurrent_install_run_free)
{
    pthread_t th[4];
    void *bad;
    int i;
    uint32_t total = 0;
    for (i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, churn, (void *)(uintptr_t)(1000u * (uint32_t)(i + 1)));
    for (i = 0; i < 4; i++) {
        pthread_join(th[i], &bad);
        total += (uint32_t)(uintptr_t)bad;
    }
    ASSERT_EQ_INT(total, 0);
}

TEST(shutdown_unmaps_everything)
{
    ASSERT(install_return(10, 0) != NULL);
    ASSERT(cl_codeheap_mapped_bytes() > 0);
    cl_codeheap_shutdown();
    ASSERT_EQ_INT(cl_codeheap_mapped_bytes(), 0);
    ASSERT_EQ_INT(cl_codeheap_live_bytes(), 0);
    /* Without init the heap installs nothing; after it, it maps afresh. */
    ASSERT(install_return(11, 0) == NULL);
    cl_codeheap_init();
    ASSERT_EQ_INT(((ret_fn)install_return(12, 0))(), 12);
    cl_codeheap_shutdown();
}

int main(void)
{
    test_init();
    cl_codeheap_init();
    RUN(installed_code_executes);
    RUN(freed_block_is_reused_and_runs_new_code);
    RUN(large_free_block_is_split);
    RUN(foreign_and_double_free_are_refused);
    RUN(oversize_block_gets_its_own_chunk);
    RUN(concurrent_install_run_free);
    RUN(shutdown_unmaps_everything);
    REPORT();
}

#else  /* !JIT_A64 */

TEST(no_code_heap_without_the_backend)
{
    ASSERT(1);
}

int main(void)
{
    test_init();
    RUN(no_code_heap_without_the_backend);
    REPORT();
}

#endif
