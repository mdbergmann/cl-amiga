#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "platform/platform.h"

static void setup(void)
{
    platform_init();
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

TEST(alloc_cons)
{
    CL_Obj c = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    ASSERT(!CL_NULL_P(c));
    ASSERT(CL_CONS_P(c));
}

TEST(alloc_many_conses)
{
    int i;
    CL_Obj list = CL_NIL;
    for (i = 0; i < 1000; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }
    /* Verify first and last */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 999);
    /* Walk to end */
    for (i = 0; i < 999; i++) list = cl_cdr(list);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 0);
    ASSERT(CL_NULL_P(cl_cdr(list)));
}

TEST(alloc_string)
{
    CL_Obj s = cl_make_string("test", 4);
    CL_String *str = (CL_String *)CL_OBJ_TO_PTR(s);
    ASSERT(!CL_NULL_P(s));
    ASSERT_EQ_INT((int)str->length, 4);
    ASSERT_STR_EQ(str->data, "test");
}

TEST(alloc_vector)
{
    CL_Obj v = cl_make_vector(10);
    CL_Vector *vec;
    ASSERT(!CL_NULL_P(v));
    ASSERT(CL_VECTOR_P(v));
    vec = (CL_Vector *)CL_OBJ_TO_PTR(v);
    ASSERT_EQ_INT((int)vec->length, 10);
    /* All elements should be NIL */
    ASSERT(CL_NULL_P(vec->data[0]));
    ASSERT(CL_NULL_P(vec->data[9]));
}

TEST(gc_basic)
{
    /* Allocate some objects, run GC, check nothing crashes */
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    int i;

    CL_GC_PROTECT(kept);

    /* Allocate garbage */
    for (i = 0; i < 100; i++) {
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    }

    cl_gc();

    /* Protected object should survive */
    ASSERT(CL_CONS_P(kept));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(kept)), 42);

    CL_GC_UNPROTECT(1);
}

TEST(gc_chain)
{
    /* Build a list, protect head, GC should keep entire chain */
    CL_Obj list = CL_NIL;
    int i;

    for (i = 0; i < 50; i++) {
        list = cl_cons(CL_MAKE_FIXNUM(i), list);
    }
    CL_GC_PROTECT(list);

    /* Create garbage */
    for (i = 0; i < 200; i++) {
        cl_cons(CL_MAKE_FIXNUM(i + 1000), CL_NIL);
    }

    cl_gc();

    /* Verify list is intact */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 49);
    for (i = 0; i < 49; i++) list = cl_cdr(list);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(list)), 0);

    CL_GC_UNPROTECT(1);
}

TEST(heap_stats)
{
    /* Verify arena is initialized and stats don't crash */
    cl_mem_stats();
    ASSERT(cl_heap.arena != NULL);
    ASSERT(cl_heap.arena_size == CL_DEFAULT_HEAP_SIZE);
    ASSERT(cl_heap.bump > 0);  /* Some objects allocated during init */
}

int main(void)
{
    test_init();
    setup();

    RUN(alloc_cons);
    RUN(alloc_many_conses);
    RUN(alloc_string);
    RUN(alloc_vector);
    RUN(gc_basic);
    RUN(gc_chain);
    RUN(heap_stats);

    teardown();
    REPORT();
}
