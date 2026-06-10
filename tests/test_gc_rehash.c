/* Regression tests for post-compaction rehashing of identity-keyed tables.
 *
 * The compacting GC relocates objects, which changes their arena offsets.  Two
 * data structures derive a hash slot from an object's offset and therefore go
 * stale across a compaction unless they are rebuilt:
 *
 *   1. EQUAL/EQL/EQUALP hash tables, when a key bottoms out in an object
 *      (symbol, struct, instance, or a cons whose car is such) — those hash by
 *      identity (the offset).  Before the fix only EQ tables were rehashed, so
 *      an EQUAL-keyed entry inserted before a compaction became un-findable
 *      afterwards.  This is what made ASDF's `visited-actions` lose entries and
 *      surfaced as "No applicable method for STATUS-BITS (NULL)" loading babel.
 *
 *   2. The per-thread TLV table that holds active dynamic (special-variable)
 *      bindings — open-addressed and keyed by `sym >> 2`.  A relocated symbol
 *      lands in a different probe slot, so the binding silently reverted to the
 *      symbol's global value (this dropped ASDF's `*asdf-session*` mid-perform).
 *
 * These bugs only reproduce under a *moving* compaction that actually changes
 * offsets, so each test forces cl_gc_compact() directly (the allocator's
 * fragmentation heuristic, and the GC-stress full-compact-to-canonical-layout
 * mode, do not reliably relocate a live object to a new offset).
 */
#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
#include "core/thread.h"
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
    cl_reader_init();
    cl_printer_init();
    cl_compiler_init();
    cl_vm_init(0, 0);
    cl_builtins_init();
    cl_repl_init();
}

static void teardown(void)
{
    cl_mem_shutdown();
    platform_shutdown();
}

static const char *eval_print(const char *str)
{
    static char buf[256];
    CL_Obj result = cl_eval_string(str);
    cl_prin1_to_string(result, buf, sizeof(buf));
    return buf;
}

static void make_garbage(int n)
{
    int i;
    for (i = 0; i < n; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
}

/* --- EQUAL table with an object (struct-cons) key survives compaction --- */
TEST(rehash_equal_object_key)
{
    const char *r;

    /* Garbage below the key, so reclaiming it slides the key to a new offset
     * when we compact — changing the identity hash of its car.  The car is a
     * STRUCT, which (like ASDF's operation/component instances) is hashed by
     * identity under EQUAL — unlike a cons that bottoms out in a fixnum. */
    make_garbage(800);

    cl_eval_string("(defstruct grh-s id)");
    cl_eval_string("(defparameter *grh-ht* (make-hash-table :test 'equal))");
    cl_eval_string("(defparameter *grh-k* (cons (make-grh-s :id 1) 3))");
    cl_eval_string("(setf (gethash *grh-k* *grh-ht*) 777)");

    /* Churn, then a normal GC to free it, then a forced moving compaction. */
    cl_eval_string("(dotimes (i 20000) (cons i i))");
    cl_gc();
    cl_gc_compact();
    cl_gc_compact();

    /* Same key object, looked up after it (and its car) relocated. */
    r = eval_print("(gethash *grh-k* *grh-ht*)");
    ASSERT_STR_EQ(r, "777");
}

/* --- EQUAL table with a symbol key survives compaction --- */
TEST(rehash_equal_symbol_key)
{
    const char *r;

    make_garbage(800);
    cl_eval_string("(defparameter *grh-ht2* (make-hash-table :test 'equal))");
    cl_eval_string("(setf (gethash 'grh-sym-key *grh-ht2*) 555)");
    cl_eval_string("(dotimes (i 20000) (cons i i))");
    cl_gc();
    cl_gc_compact();
    cl_gc_compact();

    r = eval_print("(gethash 'grh-sym-key *grh-ht2*)");
    ASSERT_STR_EQ(r, "555");
}

/* --- A dynamic (special) binding survives compaction --- */
TEST(rehash_tlv_dynamic_binding)
{
    CL_Thread *t = CT;
    CL_Obj sym, value, got;

    /* Garbage so the about-to-be-created symbol/value relocate on compaction. */
    make_garbage(800);

    /* A fresh symbol interned now sits at a high offset; bind it in the TLV
     * table the way OP_DYNBIND would for `(let ((*x* ...)) ...)`. */
    sym = cl_intern("GRH-TLV-SYM", 11);
    CL_GC_PROTECT(sym);
    value = cl_cons(CL_MAKE_FIXNUM(12345), CL_NIL);   /* rooted via the TLV entry */
    cl_tlv_set(t, sym, value);

    /* Sanity: the binding resolves before any compaction. */
    got = cl_tlv_get(t, sym);
    ASSERT(got != CL_TLV_ABSENT);

    /* Force a moving compaction: the symbol relocates, so its TLV probe slot
     * changes.  Without the rehash, cl_tlv_get would now miss. */
    make_garbage(800);
    cl_gc();
    cl_gc_compact();
    cl_gc_compact();

    /* `sym` was GC-protected, so it holds the post-move offset. */
    got = cl_tlv_get(t, sym);
    ASSERT(got != CL_TLV_ABSENT);
    ASSERT(CL_CONS_P(got));
    ASSERT_EQ_INT(CL_FIXNUM_VAL(cl_car(got)), 12345);

    cl_tlv_remove(t, sym);
    CL_GC_UNPROTECT(1);
}

int main(void)
{
    setup();

    RUN(rehash_equal_object_key);
    RUN(rehash_equal_symbol_key);
    RUN(rehash_tlv_dynamic_binding);

    teardown();
    REPORT();
}
