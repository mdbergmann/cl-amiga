#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "platform/platform.h"

#include <string.h>

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

/* --- Basic compaction --- */

TEST(compact_basic)
{
    /* Allocate a kept cons, create garbage, compact, verify kept cons intact */
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    int i;

    CL_GC_PROTECT(kept);

    /* Create garbage to interleave with kept object */
    for (i = 0; i < 1000; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_CONS_P(kept));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(kept)), 42);
    ASSERT(CL_NULL_P(cl_cdr(kept)));

    CL_GC_UNPROTECT(1);
}

/* --- Chain integrity after compaction --- */

TEST(compact_chain_integrity)
{
    /* Build a cons chain, interleave garbage, compact, walk the chain */
    CL_Obj chain = CL_NIL;
    int i;

    CL_GC_PROTECT(chain);

    for (i = 0; i < 50; i++) {
        chain = cl_cons(CL_MAKE_FIXNUM(i), chain);
        /* Interleave garbage */
        cl_cons(CL_MAKE_FIXNUM(9999), CL_NIL);
        cl_cons(CL_MAKE_FIXNUM(9999), CL_NIL);
    }

    cl_gc_compact();

    /* Walk chain: should go 49, 48, ..., 0 */
    {
        CL_Obj cur = chain;
        for (i = 49; i >= 0; i--) {
            ASSERT(CL_CONS_P(cur));
            ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cur)), i);
            cur = cl_cdr(cur);
        }
        ASSERT(CL_NULL_P(cur));
    }

    CL_GC_UNPROTECT(1);
}

/* --- Symbol preservation after compaction --- */

TEST(compact_symbol_values)
{
    CL_Obj sym = cl_intern("COMPACT-TEST-SYM", 16);
    CL_Obj val = cl_cons(CL_MAKE_FIXNUM(100), CL_MAKE_FIXNUM(200));
    CL_Symbol *sp;
    int i;

    CL_GC_PROTECT(sym);
    CL_GC_PROTECT(val);

    sp = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    sp->value = val;

    /* Generate garbage */
    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    /* Re-dereference after compaction (sym value moved) */
    sp = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    ASSERT(CL_CONS_P(sp->value));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(sp->value)), 100);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(sp->value)), 200);

    CL_GC_UNPROTECT(2);
}

/* --- String preservation --- */

TEST(compact_strings)
{
    CL_Obj str = cl_make_string("hello compaction", 16);
    CL_String *s;
    int i;

    CL_GC_PROTECT(str);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_STRING_P(str));
    s = (CL_String *)CL_OBJ_TO_PTR(str);
    ASSERT_EQ_INT((int)s->length, 16);
    ASSERT_STR_EQ(s->data, "hello compaction");

    CL_GC_UNPROTECT(1);
}

/* --- Vector element preservation --- */

TEST(compact_vector)
{
    CL_Obj vec = cl_make_vector(5);
    CL_Vector *v;
    int i;

    CL_GC_PROTECT(vec);

    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    for (i = 0; i < 5; i++)
        v->data[i] = CL_MAKE_FIXNUM(i * 10);

    /* Generate garbage */
    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    ASSERT_EQ_INT((int)v->length, 5);
    for (i = 0; i < 5; i++)
        ASSERT_EQ_INT(CL_FIXNUM_VAL(v->data[i]), i * 10);

    CL_GC_UNPROTECT(1);
}

/* --- Vector with heap-pointer elements --- */

TEST(compact_vector_heap_elts)
{
    CL_Obj vec = cl_make_vector(3);
    CL_Vector *v;
    CL_Obj s1, s2, s3;
    CL_String *sp;
    int i;

    CL_GC_PROTECT(vec);

    s1 = cl_make_string("alpha", 5);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[0] = s1;

    s2 = cl_make_string("beta", 4);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[1] = s2;

    s3 = cl_make_string("gamma", 5);
    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    v->data[2] = s3;

    /* Generate garbage */
    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    v = (CL_Vector *)CL_OBJ_TO_PTR(vec);
    ASSERT(CL_STRING_P(v->data[0]));
    sp = (CL_String *)CL_OBJ_TO_PTR(v->data[0]);
    ASSERT_STR_EQ(sp->data, "alpha");

    ASSERT(CL_STRING_P(v->data[1]));
    sp = (CL_String *)CL_OBJ_TO_PTR(v->data[1]);
    ASSERT_STR_EQ(sp->data, "beta");

    ASSERT(CL_STRING_P(v->data[2]));
    sp = (CL_String *)CL_OBJ_TO_PTR(v->data[2]);
    ASSERT_STR_EQ(sp->data, "gamma");

    CL_GC_UNPROTECT(1);
}

/* --- Struct slot preservation --- */

TEST(compact_struct)
{
    CL_Obj type_name = cl_intern("TEST-STRUCT", 11);
    CL_Obj st;
    CL_Struct *sp;
    int i;

    CL_GC_PROTECT(type_name);
    st = cl_make_struct(type_name, 3);
    CL_GC_PROTECT(st);

    sp = (CL_Struct *)CL_OBJ_TO_PTR(st);
    sp->slots[0] = CL_MAKE_FIXNUM(1);
    sp->slots[1] = CL_MAKE_FIXNUM(2);
    sp->slots[2] = cl_make_string("slot-val", 8);
    /* Re-deref after alloc */
    sp = (CL_Struct *)CL_OBJ_TO_PTR(st);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    sp = (CL_Struct *)CL_OBJ_TO_PTR(st);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(sp->slots[0]), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(sp->slots[1]), 2);
    ASSERT(CL_STRING_P(sp->slots[2]));
    {
        CL_String *ss = (CL_String *)CL_OBJ_TO_PTR(sp->slots[2]);
        ASSERT_STR_EQ(ss->data, "slot-val");
    }

    CL_GC_UNPROTECT(2);
}

/* --- Multiple compactions --- */

TEST(compact_multiple)
{
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(77), CL_NIL);
    int round, i;

    CL_GC_PROTECT(kept);

    for (round = 0; round < 5; round++) {
        for (i = 0; i < 200; i++)
            cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
        cl_gc_compact();
        ASSERT(CL_CONS_P(kept));
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(kept)), 77);
    }

    CL_GC_UNPROTECT(1);
}

/* --- Compaction reclaims fragmented space --- */

TEST(compact_reclaims_space)
{
    /* Create heavy fragmentation: allocate pairs, keep odd ones, discard even.
     * After normal GC the free list has many small gaps.
     * After compaction, bump pointer should be much lower. */
    CL_Obj keepers[50];
    uint32_t bump_before, bump_after;
    int i;

    for (i = 0; i < 50; i++) {
        CL_GC_PROTECT(keepers[i]);
    }

    for (i = 0; i < 50; i++) {
        cl_cons(CL_MAKE_FIXNUM(9999), CL_NIL);  /* garbage */
        keepers[i] = cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    }

    /* Normal GC first to make the garbage reclaimable */
    cl_gc();

    bump_before = cl_heap.bump;

    cl_gc_compact();

    bump_after = cl_heap.bump;

    /* After compaction, bump should be smaller (space reclaimed) */
    ASSERT(bump_after <= bump_before);

    /* Verify all keepers survived */
    for (i = 0; i < 50; i++)
        ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(keepers[i])), i);

    CL_GC_UNPROTECT(50);
}

/* --- Deep nesting survives compaction --- */

TEST(compact_deep_nesting)
{
    /* Build deeply nested structure: ((((1 . 2) . 3) . 4) . 5) */
    CL_Obj obj = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    int i;

    CL_GC_PROTECT(obj);

    for (i = 3; i <= 5; i++) {
        obj = cl_cons(obj, CL_MAKE_FIXNUM(i));
        /* Interleave garbage */
        cl_cons(CL_MAKE_FIXNUM(9999), CL_NIL);
    }

    cl_gc_compact();

    /* Verify structure */
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(obj)), 5);
    obj = cl_car(obj);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(obj)), 4);
    obj = cl_car(obj);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(obj)), 3);
    obj = cl_car(obj);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(obj)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(obj)), 2);

    CL_GC_UNPROTECT(1);
}

/* --- Ratio preservation --- */

TEST(compact_ratio)
{
    CL_Obj r = cl_make_ratio(CL_MAKE_FIXNUM(3), CL_MAKE_FIXNUM(7));
    CL_Ratio *rp;
    int i;

    CL_GC_PROTECT(r);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_RATIO_P(r));
    rp = (CL_Ratio *)CL_OBJ_TO_PTR(r);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rp->numerator), 3);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rp->denominator), 7);

    CL_GC_UNPROTECT(1);
}

/* --- Complex number preservation --- */

TEST(compact_complex)
{
    CL_Obj c = cl_make_complex(CL_MAKE_FIXNUM(10), CL_MAKE_FIXNUM(20));
    CL_Complex *cp;
    int i;

    CL_GC_PROTECT(c);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_COMPLEX_P(c));
    cp = (CL_Complex *)CL_OBJ_TO_PTR(c);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cp->realpart), 10);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cp->imagpart), 20);

    CL_GC_UNPROTECT(1);
}

/* --- Pathname preservation --- */

TEST(compact_pathname)
{
    CL_Obj host = CL_NIL;
    CL_Obj device = CL_NIL;
    CL_Obj dir = CL_NIL;
    CL_Obj name, type, version;
    CL_Obj pn;
    CL_Pathname *pp;
    int i;

    name = cl_make_string("test", 4);
    CL_GC_PROTECT(name);
    type = cl_make_string("lisp", 4);
    CL_GC_PROTECT(type);
    version = CL_NIL;

    pn = cl_make_pathname(host, device, dir, name, type, version);
    CL_GC_PROTECT(pn);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_PATHNAME_P(pn));
    pp = (CL_Pathname *)CL_OBJ_TO_PTR(pn);
    ASSERT(CL_STRING_P(pp->name));
    {
        CL_String *ns = (CL_String *)CL_OBJ_TO_PTR(pp->name);
        ASSERT_STR_EQ(ns->data, "test");
    }
    ASSERT(CL_STRING_P(pp->type));
    {
        CL_String *ts = (CL_String *)CL_OBJ_TO_PTR(pp->type);
        ASSERT_STR_EQ(ts->data, "lisp");
    }

    CL_GC_UNPROTECT(3);
}

/* --- Cell (boxed closure variable) preservation --- */

TEST(compact_cell)
{
    CL_Obj val = cl_cons(CL_MAKE_FIXNUM(42), CL_NIL);
    CL_Obj cell;
    CL_Cell *cp;
    int i;

    CL_GC_PROTECT(val);
    cell = cl_make_cell(val);
    CL_GC_PROTECT(cell);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    ASSERT(CL_CELL_P(cell));
    cp = (CL_Cell *)CL_OBJ_TO_PTR(cell);
    ASSERT(CL_CONS_P(cp->value));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cp->value)), 42);

    CL_GC_UNPROTECT(2);
}

/* --- Allocation after compaction works correctly --- */

TEST(compact_alloc_after)
{
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(1), CL_NIL);
    CL_Obj new_obj;
    int i;

    CL_GC_PROTECT(kept);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    /* Allocate new objects after compaction — should work with bump allocator */
    new_obj = cl_cons(CL_MAKE_FIXNUM(99), kept);
    ASSERT(CL_CONS_P(new_obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(new_obj)), 99);
    ASSERT(CL_CONS_P(cl_cdr(new_obj)));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cl_cdr(new_obj))), 1);

    CL_GC_UNPROTECT(1);
}

/* --- Free list is empty after compaction --- */

TEST(compact_clears_free_list)
{
    int i;
    CL_Obj kept = cl_cons(CL_MAKE_FIXNUM(1), CL_NIL);

    CL_GC_PROTECT(kept);

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    /* Normal GC creates free list entries */
    cl_gc();
    /* Free list should have entries now (from freed garbage) */

    cl_gc_compact();

    /* After compaction, free list must be empty */
    ASSERT_EQ_INT((int)cl_heap.free_list, 0);

    CL_GC_UNPROTECT(1);
}

/* --- Mixed object types survive --- */

TEST(compact_mixed_types)
{
    CL_Obj cons_obj = cl_cons(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(2));
    CL_Obj str_obj, vec_obj, ratio_obj;
    CL_String *sp;
    CL_Vector *vp;
    CL_Ratio *rp;
    int i;

    CL_GC_PROTECT(cons_obj);
    str_obj = cl_make_string("mixed", 5);
    CL_GC_PROTECT(str_obj);
    vec_obj = cl_make_vector(3);
    CL_GC_PROTECT(vec_obj);
    ratio_obj = cl_make_ratio(CL_MAKE_FIXNUM(1), CL_MAKE_FIXNUM(3));
    CL_GC_PROTECT(ratio_obj);

    /* Set vector elements to reference other heap objects */
    vp = (CL_Vector *)CL_OBJ_TO_PTR(vec_obj);
    vp->data[0] = cons_obj;
    vp->data[1] = str_obj;
    vp->data[2] = ratio_obj;

    for (i = 0; i < 500; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);

    cl_gc_compact();

    /* Verify cons */
    ASSERT(CL_CONS_P(cons_obj));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(cons_obj)), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_cdr(cons_obj)), 2);

    /* Verify string */
    ASSERT(CL_STRING_P(str_obj));
    sp = (CL_String *)CL_OBJ_TO_PTR(str_obj);
    ASSERT_STR_EQ(sp->data, "mixed");

    /* Verify vector elements still point to the right objects */
    vp = (CL_Vector *)CL_OBJ_TO_PTR(vec_obj);
    ASSERT_EQ(vp->data[0], cons_obj);
    ASSERT_EQ(vp->data[1], str_obj);
    ASSERT_EQ(vp->data[2], ratio_obj);

    /* Verify ratio */
    ASSERT(CL_RATIO_P(ratio_obj));
    rp = (CL_Ratio *)CL_OBJ_TO_PTR(ratio_obj);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rp->numerator), 1);
    ASSERT_EQ_INT(CL_FIXNUM_VAL(rp->denominator), 3);

    CL_GC_UNPROTECT(4);
}

int main(void)
{
    setup();

    RUN(compact_basic);
    RUN(compact_chain_integrity);
    RUN(compact_symbol_values);
    RUN(compact_strings);
    RUN(compact_vector);
    RUN(compact_vector_heap_elts);
    RUN(compact_struct);
    RUN(compact_multiple);
    RUN(compact_reclaims_space);
    RUN(compact_deep_nesting);
    RUN(compact_ratio);
    RUN(compact_complex);
    RUN(compact_pathname);
    RUN(compact_cell);
    RUN(compact_alloc_after);
    RUN(compact_clears_free_list);
    RUN(compact_mixed_types);

    teardown();
    REPORT();
}
