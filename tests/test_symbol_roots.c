#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/stream.h"
#include "platform/platform.h"

/*
 * Cached global CL_Obj handles (SYM_* / KW_* in symbol.c, the stdio
 * stream singletons in stream.c) hold raw arena offsets.  The moving
 * compactor only rewrites offsets it can reach through registered
 * roots; an unregistered handle keeps its pre-move value, so identity
 * comparisons (pathname keywords, *PRINT-OBJECT-HOOK* dispatch, stdio
 * stream writes) silently target the wrong object after the first
 * compaction that relocates the symbol.
 *
 * To make the compaction actually MOVE the tested objects, the tests
 * first drop *RANDOM-STATE*'s value: the random-state object is
 * allocated mid-way through cl_symbol_init, directly BELOW the
 * pathname keywords and everything interned after them, so freeing it
 * forces every later object to slide down.  Each test asserts that the
 * movement really happened — if boot allocation order ever changes and
 * nothing moves, the test fails loudly instead of passing vacuously.
 */

static void setup(void)
{
    platform_init();
    cl_thread_init();
    cl_error_init();
    cl_mem_init(CL_DEFAULT_HEAP_SIZE);
    cl_package_init();
    cl_symbol_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

/* Free the boot random-state object (see file comment). */
static void drop_random_state_value(void)
{
    CL_Obj rs = cl_intern_in("*RANDOM-STATE*", 14, cl_package_cl);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(rs);
    ASSERT(!CL_NULL_P(s->value));   /* boot gave it a random-state */
    s->value = CL_NIL;
}

TEST(symbol_handles_survive_compaction)
{
    CL_Obj kw_abs_before, sym_mod_before;

    setup();

    ASSERT(!CL_NULL_P(KW_ABSOLUTE));
    ASSERT(!CL_NULL_P(SYM_STAR_MODULES));

    kw_abs_before = KW_ABSOLUTE;
    sym_mod_before = SYM_STAR_MODULES;

    drop_random_state_value();
    cl_gc_compact();

    /* Precondition: the compaction moved the symbols (dead random-state
     * below them).  Without movement this test proves nothing. */
    ASSERT((CL_Obj)cl_intern_keyword("ABSOLUTE", 8) != kw_abs_before);
    ASSERT((CL_Obj)cl_intern_in("*MODULES*", 9, cl_package_cl)
           != sym_mod_before);

    /* Identity: each cached handle must be the interned symbol. */
    ASSERT_EQ_INT((int)KW_ABSOLUTE,
                  (int)cl_intern_keyword("ABSOLUTE", 8));
    ASSERT_EQ_INT((int)KW_DIRECTORY,
                  (int)cl_intern_keyword("DIRECTORY", 9));
    ASSERT_EQ_INT((int)KW_WILD,
                  (int)cl_intern_keyword("WILD", 4));
    ASSERT_EQ_INT((int)SYM_STAR_MODULES,
                  (int)cl_intern_in("*MODULES*", 9, cl_package_cl));
    ASSERT_EQ_INT((int)SYM_STAR_READ_EVAL,
                  (int)cl_intern_in("*READ-EVAL*", 11, cl_package_cl));

    /* Dereference sanity: the handles still point at SYMBOL headers. */
    ASSERT(CL_SYMBOL_P(KW_ABSOLUTE));
    ASSERT(CL_SYMBOL_P(SYM_STAR_MODULES));

    teardown();
}

TEST(stream_handles_survive_compaction)
{
    CL_Obj stdout_before;

    setup();
    cl_stream_init();

    ASSERT(!CL_NULL_P(cl_stdout_stream));
    stdout_before = cl_stdout_stream;

    drop_random_state_value();
    cl_gc_compact();

    /* The symbol values of *STANDARD-INPUT* / *STANDARD-OUTPUT* /
     * *ERROR-OUTPUT* are forwarded via the package registry; the cached
     * C globals must agree with them after compaction. */
    {
        CL_Symbol *s;
        s = (CL_Symbol *)CL_OBJ_TO_PTR(cl_intern_in("*STANDARD-OUTPUT*", 17,
                                                    cl_package_cl));
        /* Precondition: the stream really moved. */
        ASSERT(s->value != stdout_before);
        ASSERT_EQ_INT((int)s->value, (int)cl_stdout_stream);

        s = (CL_Symbol *)CL_OBJ_TO_PTR(cl_intern_in("*STANDARD-INPUT*", 16,
                                                    cl_package_cl));
        ASSERT_EQ_INT((int)s->value, (int)cl_stdin_stream);
        s = (CL_Symbol *)CL_OBJ_TO_PTR(cl_intern_in("*ERROR-OUTPUT*", 14,
                                                    cl_package_cl));
        ASSERT_EQ_INT((int)s->value, (int)cl_stderr_stream);
    }

    teardown();
}

/* Repeated init/shutdown cycles (every C unit test binary does this)
 * must not duplicate registry entries: registration is idempotent per
 * address and cl_mem_init resets the registry for the fresh heap. */
TEST(reinit_does_not_leak_or_duplicate_roots)
{
    int i;
    for (i = 0; i < 3; i++) {
        setup();
        ASSERT_EQ_INT(cl_gc_audit_roots(), 0);
        drop_random_state_value();
        cl_gc_compact();
        ASSERT_EQ_INT((int)KW_ABSOLUTE,
                      (int)cl_intern_keyword("ABSOLUTE", 8));
        teardown();
    }
}

int main(void)
{
    test_init();
    RUN(symbol_handles_survive_compaction);
    RUN(stream_handles_survive_compaction);
    RUN(reinit_does_not_leak_or_duplicate_roots);
    REPORT();
}
