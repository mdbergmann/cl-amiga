/*
 * test_dynbind_threaded.c — Tests for Phase 3: Thread-Local Dynamic Bindings.
 *
 * Tests: TLV basic ops, nested dynamic bindings through TLV,
 * cl_symbol_value / cl_set_symbol_value / cl_symbol_boundp,
 * PROGV with TLV, GC with TLV entries, snapshot.
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
#include "core/thread.h"
#include "core/repl.h"
#include "platform/platform.h"
#include "platform/platform_thread.h"

#include <string.h>

/* Undef gc_root_count macro so we can access struct fields directly */
#undef gc_root_count

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
    cl_thread_shutdown();
    platform_shutdown();
}

/* Helper: evaluate a Lisp expression and return the printed result */
static const char *eval_print(const char *expr)
{
    static char buf[4096];
    CL_Obj result = cl_eval_string(expr);
    cl_prin1_to_string(result, buf, sizeof(buf));
    return buf;
}

/* ================================================================
 * TLV basic operations
 * ================================================================ */

TEST(tlv_basic_get_set_remove)
{
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*TLV-TEST-1*", 13);
    CL_Obj val;

    /* Initially no TLV entry */
    val = cl_tlv_get(t, sym);
    ASSERT_EQ(val, CL_TLV_ABSENT);

    /* Set a TLV entry */
    cl_tlv_set(t, sym, CL_MAKE_FIXNUM(42));
    val = cl_tlv_get(t, sym);
    ASSERT_EQ(val, CL_MAKE_FIXNUM(42));

    /* Update the entry */
    cl_tlv_set(t, sym, CL_MAKE_FIXNUM(99));
    val = cl_tlv_get(t, sym);
    ASSERT_EQ(val, CL_MAKE_FIXNUM(99));

    /* Remove the entry */
    cl_tlv_remove(t, sym);
    val = cl_tlv_get(t, sym);
    ASSERT_EQ(val, CL_TLV_ABSENT);
}

TEST(tlv_multiple_symbols)
{
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym1 = cl_intern("*TLV-MULTI-1*", 14);
    CL_Obj sym2 = cl_intern("*TLV-MULTI-2*", 14);
    CL_Obj sym3 = cl_intern("*TLV-MULTI-3*", 14);

    cl_tlv_set(t, sym1, CL_MAKE_FIXNUM(1));
    cl_tlv_set(t, sym2, CL_MAKE_FIXNUM(2));
    cl_tlv_set(t, sym3, CL_MAKE_FIXNUM(3));

    ASSERT_EQ(cl_tlv_get(t, sym1), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_tlv_get(t, sym2), CL_MAKE_FIXNUM(2));
    ASSERT_EQ(cl_tlv_get(t, sym3), CL_MAKE_FIXNUM(3));

    /* Remove middle one */
    cl_tlv_remove(t, sym2);
    ASSERT_EQ(cl_tlv_get(t, sym1), CL_MAKE_FIXNUM(1));
    ASSERT_EQ(cl_tlv_get(t, sym2), CL_TLV_ABSENT);
    ASSERT_EQ(cl_tlv_get(t, sym3), CL_MAKE_FIXNUM(3));

    /* Re-insert at tombstone */
    cl_tlv_set(t, sym2, CL_MAKE_FIXNUM(22));
    ASSERT_EQ(cl_tlv_get(t, sym2), CL_MAKE_FIXNUM(22));

    /* Clean up */
    cl_tlv_remove(t, sym1);
    cl_tlv_remove(t, sym2);
    cl_tlv_remove(t, sym3);
}

TEST(tlv_absent_vs_unbound)
{
    /* CL_TLV_ABSENT (no entry) is different from CL_UNBOUND (bound to unbound) */
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*TLV-ABSENT-TEST*", 18);

    ASSERT_EQ(cl_tlv_get(t, sym), CL_TLV_ABSENT);

    /* PROGV can bind a symbol to CL_UNBOUND (no value provided) */
    cl_tlv_set(t, sym, CL_UNBOUND);
    ASSERT_EQ(cl_tlv_get(t, sym), CL_UNBOUND);
    ASSERT(cl_tlv_get(t, sym) != CL_TLV_ABSENT);

    cl_tlv_remove(t, sym);
}

/* ================================================================
 * High-level accessors: cl_symbol_value, cl_set_symbol_value, cl_symbol_boundp
 * ================================================================ */

TEST(symbol_value_accessor_no_tlv)
{
    /* Without TLV entry, cl_symbol_value reads global symbol->value */
    CL_Obj sym = cl_intern("*SV-TEST-1*", 12);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(77);
    s->flags |= CL_SYM_SPECIAL;

    ASSERT_EQ(cl_symbol_value(sym), CL_MAKE_FIXNUM(77));

    /* Clean up */
    s->value = CL_UNBOUND;
}

TEST(symbol_value_accessor_with_tlv)
{
    /* With TLV entry, cl_symbol_value reads TLV, not global */
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*SV-TEST-2*", 12);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(100);
    s->flags |= CL_SYM_SPECIAL;

    /* Set TLV override */
    cl_tlv_set(t, sym, CL_MAKE_FIXNUM(200));

    /* Should read TLV, not global */
    ASSERT_EQ(cl_symbol_value(sym), CL_MAKE_FIXNUM(200));

    /* Global is unchanged */
    ASSERT_EQ(s->value, CL_MAKE_FIXNUM(100));

    /* Remove TLV — falls back to global */
    cl_tlv_remove(t, sym);
    ASSERT_EQ(cl_symbol_value(sym), CL_MAKE_FIXNUM(100));

    /* Clean up */
    s->value = CL_UNBOUND;
}

TEST(set_symbol_value_with_tlv)
{
    /* cl_set_symbol_value updates TLV if entry exists, else global */
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*SSV-TEST*", 11);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_MAKE_FIXNUM(10);
    s->flags |= CL_SYM_SPECIAL;

    /* No TLV → writes to global */
    cl_set_symbol_value(sym, CL_MAKE_FIXNUM(20));
    ASSERT_EQ(s->value, CL_MAKE_FIXNUM(20));

    /* Create TLV entry */
    cl_tlv_set(t, sym, CL_MAKE_FIXNUM(30));

    /* With TLV → writes to TLV, not global */
    cl_set_symbol_value(sym, CL_MAKE_FIXNUM(40));
    ASSERT_EQ(cl_tlv_get(t, sym), CL_MAKE_FIXNUM(40));
    ASSERT_EQ(s->value, CL_MAKE_FIXNUM(20));  /* global unchanged */

    cl_tlv_remove(t, sym);
    s->value = CL_UNBOUND;
}

TEST(symbol_boundp_accessor)
{
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*BOUNDP-TEST*", 14);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->value = CL_UNBOUND;
    s->flags |= CL_SYM_SPECIAL;

    /* Globally unbound, no TLV → not bound */
    ASSERT(!cl_symbol_boundp(sym));

    /* Set TLV to a value → bound */
    cl_tlv_set(t, sym, CL_MAKE_FIXNUM(5));
    ASSERT(cl_symbol_boundp(sym));

    /* Set TLV to CL_UNBOUND → not bound (even though entry exists) */
    cl_tlv_set(t, sym, CL_UNBOUND);
    ASSERT(!cl_symbol_boundp(sym));

    /* Remove TLV, set global → bound */
    cl_tlv_remove(t, sym);
    s->value = CL_MAKE_FIXNUM(10);
    ASSERT(cl_symbol_boundp(sym));

    s->value = CL_UNBOUND;
}

/* ================================================================
 * Dynamic binding through Lisp (OP_DYNBIND / OP_DYNUNBIND)
 * ================================================================ */

TEST(dynbind_basic_let)
{
    /* LET on a special variable should use TLV */
    ASSERT_STR_EQ(eval_print(
        "(progn (defvar *db-test-1* 10) *db-test-1*)"), "10");
    ASSERT_STR_EQ(eval_print(
        "(let ((*db-test-1* 20)) *db-test-1*)"), "20");
    /* After LET, should be restored */
    ASSERT_STR_EQ(eval_print("*db-test-1*"), "10");
}

TEST(dynbind_nested_let)
{
    /* Nested dynamic bindings */
    ASSERT_STR_EQ(eval_print(
        "(progn (defvar *db-nest* 1)"
        "  (let ((*db-nest* 2))"
        "    (let ((*db-nest* 3))"
        "      *db-nest*)))"), "3");
    ASSERT_STR_EQ(eval_print("*db-nest*"), "1");
}

TEST(dynbind_setq_inside_let)
{
    /* SETQ inside LET modifies TLV, not global */
    ASSERT_STR_EQ(eval_print(
        "(progn (defvar *db-setq* 10)"
        "  (let ((*db-setq* 20))"
        "    (setq *db-setq* 30)"
        "    *db-setq*))"), "30");
    /* Global should still be 10 */
    ASSERT_STR_EQ(eval_print("*db-setq*"), "10");
}

TEST(dynbind_progv)
{
    /* PROGV should use TLV */
    ASSERT_STR_EQ(eval_print(
        "(progn (defvar *pv-test* 5)"
        "  (progv '(*pv-test*) '(50)"
        "    *pv-test*))"), "50");
    ASSERT_STR_EQ(eval_print("*pv-test*"), "5");
}

TEST(dynbind_print_escape_override)
{
    /* prin1-to-string overrides dynamic binding of *print-escape* */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-escape* nil)) (prin1-to-string \"hi\"))"),
        "\"\\\"hi\\\"\"");
    /* princ-to-string overrides dynamic binding of *print-escape* */
    ASSERT_STR_EQ(eval_print(
        "(let ((*print-escape* t)) (princ-to-string \"hi\"))"),
        "\"hi\"");
}

/* ================================================================
 * TLV snapshot
 * ================================================================ */

TEST(tlv_snapshot)
{
    CL_Thread src;
    CL_Thread dst;
    CL_Obj sym = cl_intern("*SNAP-TEST*", 12);

    memset(&src, 0, sizeof(CL_Thread));
    memset(&dst, 0, sizeof(CL_Thread));

    cl_tlv_set(&src, sym, CL_MAKE_FIXNUM(123));
    ASSERT_EQ(cl_tlv_get(&dst, sym), CL_TLV_ABSENT);

    cl_tlv_snapshot(&dst, &src);
    ASSERT_EQ(cl_tlv_get(&dst, sym), CL_MAKE_FIXNUM(123));
}

/* ================================================================
 * GC with TLV entries
 * ================================================================ */

TEST(gc_with_tlv_entries)
{
    /* Ensure TLV entries survive GC */
    CL_Thread *t = cl_get_current_thread();
    CL_Obj sym = cl_intern("*GC-TLV-TEST*", 14);
    CL_Obj str;

    /* Create a heap object and store in TLV */
    str = cl_make_string("hello-gc", 8);
    CL_GC_PROTECT(str);
    cl_tlv_set(t, sym, str);
    CL_GC_UNPROTECT(1);

    /* Force GC */
    cl_gc();

    /* TLV entry should still be valid */
    {
        CL_Obj val = cl_tlv_get(t, sym);
        ASSERT(CL_STRING_P(val));
        {
            CL_String *s = (CL_String *)CL_OBJ_TO_PTR(val);
            ASSERT_EQ_INT((int)s->length, 8);
            ASSERT(memcmp(s->data, "hello-gc", 8) == 0);
        }
    }

    cl_tlv_remove(t, sym);
}

/* ================================================================
 * Regression: existing dynamic binding behavior unchanged
 * ================================================================ */

TEST(regression_defvar_defparameter)
{
    /* defvar sets global default, defparameter always updates */
    ASSERT_STR_EQ(eval_print("(defvar *reg-dv* 100)"), "*REG-DV*");
    ASSERT_STR_EQ(eval_print("*reg-dv*"), "100");
    ASSERT_STR_EQ(eval_print("(defvar *reg-dv* 200)"), "*REG-DV*");
    ASSERT_STR_EQ(eval_print("*reg-dv*"), "100");  /* defvar doesn't change */

    ASSERT_STR_EQ(eval_print("(defparameter *reg-dp* 100)"), "*REG-DP*");
    ASSERT_STR_EQ(eval_print("*reg-dp*"), "100");
    ASSERT_STR_EQ(eval_print("(defparameter *reg-dp* 200)"), "*REG-DP*");
    ASSERT_STR_EQ(eval_print("*reg-dp*"), "200");  /* defparameter updates */
}

TEST(regression_symbol_value_set_boundp)
{
    /* symbol-value, set, boundp from Lisp */
    ASSERT_STR_EQ(eval_print("(defvar *reg-sv* 42)"), "*REG-SV*");
    ASSERT_STR_EQ(eval_print("(symbol-value '*reg-sv*)"), "42");
    ASSERT_STR_EQ(eval_print("(set '*reg-sv* 99)"), "99");
    ASSERT_STR_EQ(eval_print("(symbol-value '*reg-sv*)"), "99");
    ASSERT_STR_EQ(eval_print("(boundp '*reg-sv*)"), "T");
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    test_init();
    setup();

    /* TLV basic operations */
    RUN(tlv_basic_get_set_remove);
    RUN(tlv_multiple_symbols);
    RUN(tlv_absent_vs_unbound);

    /* High-level accessors */
    RUN(symbol_value_accessor_no_tlv);
    RUN(symbol_value_accessor_with_tlv);
    RUN(set_symbol_value_with_tlv);
    RUN(symbol_boundp_accessor);

    /* Dynamic binding through Lisp */
    RUN(dynbind_basic_let);
    RUN(dynbind_nested_let);
    RUN(dynbind_setq_inside_let);
    RUN(dynbind_progv);
    RUN(dynbind_print_escape_override);

    /* Snapshot */
    RUN(tlv_snapshot);

    /* GC */
    RUN(gc_with_tlv_entries);

    /* Regression */
    RUN(regression_defvar_defparameter);
    RUN(regression_symbol_value_set_boundp);

    teardown();
    REPORT();
}
