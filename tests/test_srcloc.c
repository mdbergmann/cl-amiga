/* Tests for EXT:FUNCTION-SOURCE-LOCATION — the native primitive behind the
 * Sly/SLYNK find-definitions (M-.) backend.
 *
 * The location data lives on CL_Bytecode.source_file / .source_line, captured
 * at compile time from cl_current_source_file (set by LOAD).  The primitive
 * resolves a closure/bytecode to that data and returns (FILE LINE), or
 * :NOT-AVAILABLE when there is no code object or no recorded file (e.g. forms
 * compiled at the REPL, where source_file is NULL).
 *
 * To exercise the (file line) path without an actual file we set
 * cl_current_source_file directly — the same per-thread variable LOAD binds —
 * before compiling, mirroring the runtime path. */

#include "test.h"
#include "core/types.h"
#include "core/mem.h"
#include "core/error.h"
#include "core/package.h"
#include "core/symbol.h"
#include "core/reader.h"
#include "core/printer.h"
#include "core/compiler.h"
#include "core/thread.h"
#include "core/vm.h"
#include "core/builtins.h"
#include "core/repl.h"
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
    cl_current_source_file = NULL;
    cl_mem_shutdown();
    platform_shutdown();
}

static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* Compile a form as if it came from FILE, so the resulting bytecode carries
 * source_file/source_line.  Restores the previous file afterward. */
static void eval_in_file(const char *file, const char *form)
{
    const char *prev = cl_current_source_file;
    cl_current_source_file = cl_intern_source_file(file);
    cl_eval_string(form);
    cl_current_source_file = prev;
}

/* --- :NOT-AVAILABLE paths --- */

TEST(srcloc_repl_function_not_available)
{
    /* Defined at the REPL (no source file) -> no location. */
    cl_eval_string("(defun sl-repl (x) x)");
    ASSERT(truthy("(eq (ext:function-source-location #'sl-repl) :not-available)"));
}

TEST(srcloc_non_function_not_available)
{
    ASSERT(truthy("(eq (ext:function-source-location 42) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location \"str\") :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location '(1 2 3)) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location nil) :not-available)"));
}

TEST(srcloc_builtin_not_available)
{
    /* C builtins have no bytecode, hence no recorded source. */
    ASSERT(truthy("(eq (ext:function-source-location #'car) :not-available)"));
    ASSERT(truthy("(eq (ext:function-source-location #'cons) :not-available)"));
}

/* --- (FILE LINE) path --- */

TEST(srcloc_file_function)
{
    eval_in_file("sl-defun.lisp", "(defun sl-filefn (a b) (+ a b))");
    /* Result is (file line): a 2-element list, file a string, line an int. */
    ASSERT(truthy("(consp (ext:function-source-location #'sl-filefn))"));
    ASSERT(truthy("(= (length (ext:function-source-location #'sl-filefn)) 2)"));
    ASSERT(truthy("(stringp (first (ext:function-source-location #'sl-filefn)))"));
    ASSERT(truthy("(integerp (second (ext:function-source-location #'sl-filefn)))"));
    /* The recorded namestring is the one we compiled under. */
    ASSERT(truthy("(string= (first (ext:function-source-location #'sl-filefn))"
                  "         \"sl-defun.lisp\")"));
    /* Line is non-negative. */
    ASSERT(truthy("(>= (second (ext:function-source-location #'sl-filefn)) 0)"));
}

TEST(srcloc_file_lambda_object)
{
    /* A bare closure object (not via a symbol) resolves the same way. */
    eval_in_file("sl-lambda.lisp", "(defparameter *sl-clo* (lambda (x) (* x x)))");
    ASSERT(truthy("(consp (ext:function-source-location *sl-clo*))"));
    ASSERT(truthy("(string= (first (ext:function-source-location *sl-clo*))"
                  "         \"sl-lambda.lisp\")"));
}

TEST(srcloc_macro_function)
{
    /* Macros are functions too; their expander carries the location. */
    eval_in_file("sl-macro.lisp", "(defmacro sl-mac (x) `(list ,x))");
    ASSERT(truthy("(consp (ext:function-source-location (macro-function 'sl-mac)))"));
    ASSERT(truthy("(string= (first (ext:function-source-location"
                  "                  (macro-function 'sl-mac)))"
                  "         \"sl-macro.lisp\")"));
}

/* --- The reader's cons -> line table across a moving collection ---
 *
 * The table is direct-mapped: an entry sits in the slot its key (the cons's
 * arena offset) hashes to, and a lookup probes only that slot.  A collector
 * that moves the cons must therefore move the entry too.  It used to rewrite
 * the key in place, leaving the entry in the old offset's slot, so every
 * form moved between READ and the compiler lost its lines: a nested lambda
 * reported its enclosing form's line (or 0) after any compaction or
 * minor collection. */

/* Read "(sl-a\n (sl-b\n  (sl-c\n   (sl-d))))" behind a run of garbage, so
 * any moving collection slides it down. */
static CL_Obj read_nested_behind_garbage(void)
{
    CL_ReadStream s;
    int i;
    for (i = 0; i < 4000; i++)
        cl_cons(CL_MAKE_FIXNUM(i), CL_NIL);
    s.buf = "(sl-a\n (sl-b\n  (sl-c\n   (sl-d))))";
    s.pos = 0;
    s.len = (int)strlen(s.buf);
    s.line = 1;
    return cl_read_from_string(&s);
}

/* The four list heads of that form, outermost first. */
static void nested_heads(CL_Obj form, CL_Obj heads[4])
{
    int i;
    heads[0] = form;
    for (i = 1; i < 4; i++)
        heads[i] = cl_car(cl_cdr(heads[i - 1]));
}

static void assert_nested_lines(CL_Obj form)
{
    CL_Obj h[4];
    nested_heads(form, h);
    ASSERT_EQ_INT(cl_srcloc_lookup(h[0]), 1);
    ASSERT_EQ_INT(cl_srcloc_lookup(h[1]), 2);
    ASSERT_EQ_INT(cl_srcloc_lookup(h[2]), 3);
    ASSERT_EQ_INT(cl_srcloc_lookup(h[3]), 4);
}

/* Every live entry sits in its key's home slot — the only slot a lookup
 * probes. */
static int srcloc_all_home(void)
{
    uint32_t i;
    for (i = 0; i < CL_SRCLOC_SIZE; i++) {
        CL_Obj k = cl_srcloc_table[i].cons_obj;
        if (!CL_NULL_P(k) && CL_SRCLOC_INDEX(k) != i) return 0;
    }
    return 1;
}

TEST(srcloc_lines_survive_compaction)
{
    CL_Obj form = read_nested_behind_garbage();
    CL_Obj before = form;
    CL_GC_PROTECT(form);
    assert_nested_lines(form);
    cl_gc_compact();
    ASSERT(form != before);            /* it moved: the case under test */
    assert_nested_lines(form);
    ASSERT(srcloc_all_home());
    CL_GC_UNPROTECT(1);
}

#ifdef CL_GENGC
TEST(srcloc_lines_survive_minor_gc)
{
    CL_Obj form, before;
    if (!cl_gengc_enabled()) return;
    form = read_nested_behind_garbage();
    before = form;
    CL_GC_PROTECT(form);
    assert_nested_lines(form);
    ASSERT_EQ_INT(cl_gc_minor(cl_heap.gc_count), 1);
    ASSERT(form != before);            /* promoted out of the nursery */
    assert_nested_lines(form);
    ASSERT(srcloc_all_home());
    CL_GC_UNPROTECT(1);
}
#endif

static void srcloc_put(uint32_t slot, CL_Obj key, int line)
{
    cl_srcloc_table[slot].cons_obj = key;
    cl_srcloc_table[slot].line = (uint16_t)line;
    cl_srcloc_table[slot].file_id = 0;
}

/* Put KEY's entry in the first free slot after its home: misplaced. */
static void srcloc_park(CL_Obj key, int line)
{
    uint32_t slot = CL_SRCLOC_INDEX(key);
    do {
        slot = (slot + 1) % CL_SRCLOC_SIZE;
    } while (!CL_NULL_P(cl_srcloc_table[slot].cons_obj));
    srcloc_put(slot, key, line);
}

static void srcloc_clear(void)
{
    uint32_t i;
    for (i = 0; i < CL_SRCLOC_SIZE; i++)
        cl_srcloc_table[i].cons_obj = CL_NIL;
}

/* White box: the re-homing pass itself.  Three entries in a cycle of wrong
 * slots (a in b's home, b in c's, c in a's) need the swap chain.  Two keys
 * sharing a home are a collision, and the higher offset (the more recent
 * cons) keeps the slot whether it is the entry carried there or the one
 * already settled in it.  The heap is compacted first and everything
 * allocated after it stays live, so the next compaction forwards every key
 * to itself: only the re-homing acts. */
TEST(srcloc_rehome_cycle_and_collision)
{
    CL_Obj a = CL_NIL, b = CL_NIL, c = CL_NIL, d = CL_NIL, e = CL_NIL;
    CL_Obj a0, b0, c0, d0, e0;
    uint32_t i;

    CL_GC_PROTECT(a); CL_GC_PROTECT(b); CL_GC_PROTECT(c);
    CL_GC_PROTECT(d); CL_GC_PROTECT(e);

    cl_gc_compact();
    a = cl_cons(CL_MAKE_FIXNUM(1), CL_NIL);
    b = cl_cons(CL_MAKE_FIXNUM(2), CL_NIL);
    c = cl_cons(CL_MAKE_FIXNUM(3), CL_NIL);
    d = cl_cons(CL_MAKE_FIXNUM(4), CL_NIL);
    /* e: the first later cons sharing d's home; each try is chained onto
     * the next, so none of them is garbage. */
    for (i = 0; i < 4 * CL_SRCLOC_SIZE; i++) {
        e = cl_cons(CL_MAKE_FIXNUM(5), e);
        if (CL_SRCLOC_INDEX(e) == CL_SRCLOC_INDEX(d)) break;
    }
    ASSERT(CL_SRCLOC_INDEX(e) == CL_SRCLOC_INDEX(d));
    ASSERT(e > d);
    ASSERT(CL_SRCLOC_INDEX(a) != CL_SRCLOC_INDEX(b));
    ASSERT(CL_SRCLOC_INDEX(b) != CL_SRCLOC_INDEX(c));
    ASSERT(CL_SRCLOC_INDEX(a) != CL_SRCLOC_INDEX(c));
    a0 = a; b0 = b; c0 = c; d0 = d; e0 = e;

    /* The cycle, plus d settled at home and the newer e carried to it. */
    srcloc_clear();
    srcloc_put(CL_SRCLOC_INDEX(b), a, 11);
    srcloc_put(CL_SRCLOC_INDEX(c), b, 12);
    srcloc_put(CL_SRCLOC_INDEX(a), c, 13);
    srcloc_put(CL_SRCLOC_INDEX(d), d, 14);
    srcloc_park(e, 15);
    ASSERT(!srcloc_all_home());

    cl_gc_compact();

    ASSERT(a == a0 && b == b0 && c == c0 && d == d0 && e == e0);
    ASSERT(srcloc_all_home());
    ASSERT_EQ_INT(cl_srcloc_lookup(a), 11);
    ASSERT_EQ_INT(cl_srcloc_lookup(b), 12);
    ASSERT_EQ_INT(cl_srcloc_lookup(c), 13);
    ASSERT_EQ_INT(cl_srcloc_lookup(e), 15);
    ASSERT_EQ_INT(cl_srcloc_lookup(d), 0);

    /* The other way round: e settled, the older d carried to it. */
    srcloc_clear();
    srcloc_put(CL_SRCLOC_INDEX(e), e, 15);
    srcloc_park(d, 14);

    cl_gc_compact();

    ASSERT(d == d0 && e == e0);
    ASSERT(srcloc_all_home());
    ASSERT_EQ_INT(cl_srcloc_lookup(e), 15);
    ASSERT_EQ_INT(cl_srcloc_lookup(d), 0);

    CL_GC_UNPROTECT(5);
}

/* End to end: a macro that compacts while it expands moves the rest of the
 * form between READ and the compiler.  The lambda it expands to must still
 * report its own line (3), not the macro call's (2). */
TEST(srcloc_nested_lambda_line_after_moving_gc)
{
    eval_in_file("sl-moved.lisp",
                 "(defmacro sl-compact-now (form) (ext:gc-compact) form)");
    eval_in_file("sl-moved.lisp",
                 "(defparameter *sl-moved*\n"
                 "  (sl-compact-now\n"
                 "   (lambda (y)\n"
                 "     y)))");
    ASSERT(truthy("(equal (ext:function-source-location *sl-moved*)"
                  "       '(\"sl-moved.lisp\" 3))"));
}

int main(void)
{
    test_init();
    setup();

    RUN(srcloc_repl_function_not_available);
    RUN(srcloc_non_function_not_available);
    RUN(srcloc_builtin_not_available);
    RUN(srcloc_file_function);
    RUN(srcloc_file_lambda_object);
    RUN(srcloc_macro_function);
    RUN(srcloc_lines_survive_compaction);
#ifdef CL_GENGC
    RUN(srcloc_lines_survive_minor_gc);
#endif
    RUN(srcloc_rehome_cycle_and_collision);
    RUN(srcloc_nested_lambda_line_after_moving_gc);

    teardown();
    REPORT();
}
