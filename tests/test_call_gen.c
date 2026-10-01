/* The call generation word (cl_call_gen, mem.h; specs/jit-direct-calls.md
 * §1).  A JIT call site caches (callee, entry) and is valid only while its
 * recorded generation equals cl_call_gen, so every event that could make a
 * cached pair wrong must move the word: a function-cell write through any
 * public path, every collection (the cached callee is neither a root nor
 * relocated), TRACE/UNTRACE.  A source that forgets to bump is a
 * wrong-function-called bug on the Amiga, which is why each one is pinned
 * here on host, where there are no call sites to notice.
 *
 * The C-level sources are checked for an exact +1; the Lisp-level ones for
 * "moved" only, since evaluating a form may itself collect.
 * tests/test_call_gen.sh runs the Lisp side (also under gc-stress, and the
 * thread sources: MP:INTERRUPT-THREAD, stop-the-world). */

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
    cl_mem_shutdown();
    platform_shutdown();
}

static int truthy(const char *expr)
{
    return cl_eval_string(expr) != CL_NIL;
}

/* Evaluate EXPR; did cl_call_gen move across it? */
static int bumps(const char *expr)
{
    uint32_t before = cl_call_gen;
    cl_eval_string(expr);
    return cl_call_gen != before;
}

TEST(starts_nonzero_and_bump_advances_by_one)
{
    uint32_t g = cl_call_gen;
    ASSERT(g != 0);
    cl_call_gen_bump("test");
    ASSERT_EQ(cl_call_gen, g + 1);
}

TEST(bump_skips_zero_on_wraparound)
{
    uint32_t saved = cl_call_gen;
    cl_call_gen = 0xFFFFFFFFu;
    cl_call_gen_bump("test wrap");
    /* 0 is the "never filled" site generation: a wrap must not land on it */
    ASSERT_EQ(cl_call_gen, 1u);
    cl_call_gen_bump("test");
    ASSERT_EQ(cl_call_gen, 2u);
    cl_call_gen = saved;
}

TEST(setter_writes_the_cell_and_bumps_once)
{
    CL_Obj sym = cl_intern_in("CALL-GEN-SETTER", 15, cl_package_cl_user);
    CL_Obj fn = cl_eval_string("(lambda () 42)");
    uint32_t g = cl_call_gen;
    cl_symbol_set_function(sym, fn);
    ASSERT_EQ(cl_call_gen, g + 1);
    sym = cl_intern_in("CALL-GEN-SETTER", 15, cl_package_cl_user);
    ASSERT_EQ(((CL_Symbol *)CL_OBJ_TO_PTR(sym))->function, fn);
    ASSERT(truthy("(eql (call-gen-setter) 42)"));

    g = cl_call_gen;
    cl_symbol_set_function(sym, CL_UNBOUND);
    ASSERT_EQ(cl_call_gen, g + 1);
    ASSERT(!truthy("(fboundp 'call-gen-setter)"));
}

TEST(every_collection_bumps)
{
    uint32_t g = cl_call_gen;
    cl_gc();
    ASSERT_EQ(cl_call_gen, g + 1);
    g = cl_call_gen;
    cl_gc_compact();
    ASSERT_EQ(cl_call_gen, g + 1);
    g = cl_call_gen;
    cl_gc_reclaim_young();   /* a minor under gengc, a full sweep without */
    ASSERT_EQ(cl_call_gen, g + 1);
    ASSERT(bumps("(gc)"));
}

TEST(defun_and_redefinition_bump)
{
    ASSERT(bumps("(defun call-gen-f () 1)"));
    ASSERT(bumps("(defun call-gen-f () 2)"));
    ASSERT(truthy("(eql (call-gen-f) 2)"));
}

TEST(setf_fdefinition_and_symbol_function_bump)
{
    ASSERT(bumps("(setf (fdefinition 'call-gen-g) (lambda () 3))"));
    ASSERT(bumps("(setf (symbol-function 'call-gen-g) (lambda () 4))"));
    ASSERT(truthy("(eql (call-gen-g) 4)"));
    /* a SETF function's cell */
    ASSERT(bumps("(defun (setf call-gen-place) (v x) (list v x))"));
    ASSERT(truthy("(equal (setf (call-gen-place 1) 2) '(2 1))"));
}

TEST(fmakunbound_bumps)
{
    cl_eval_string("(defun call-gen-h () 5)");
    ASSERT(bumps("(fmakunbound 'call-gen-h)"));
    ASSERT(!truthy("(fboundp 'call-gen-h)"));
    cl_eval_string("(defun (setf call-gen-h2) (v) v)");
    ASSERT(bumps("(fmakunbound '(setf call-gen-h2))"));
    ASSERT(!truthy("(fboundp '(setf call-gen-h2))"));
}

TEST(trace_untrace_bump)
{
    cl_eval_string("(defun call-gen-t () 6)");
    ASSERT(bumps("(trace call-gen-t)"));
    ASSERT(bumps("(untrace call-gen-t)"));
    cl_eval_string("(trace call-gen-t)");
    ASSERT(bumps("(untrace)"));
}

TEST(ffi_stub_install_bumps)
{
    /* FFI:DEFCSTRUCT's installer puts FFI stubs into the accessors'
     * function cells (builtins_ffi.c): a reader, its %SET- writer, and the
     * reader-only embedded-struct accessor -- one bump per stub. */
    uint32_t g = cl_call_gen;
    cl_eval_string("(ffi::%define-cstruct-accessors '((call-gen-pt-x :u16 0)))");
    ASSERT(cl_call_gen - g >= 2);
    ASSERT(truthy("(and (fboundp 'call-gen-pt-x) (fboundp '%set-call-gen-pt-x))"));
    ASSERT(bumps("(ffi::%define-cstruct-accessors '((call-gen-pt-n (:struct call-gen-node) 4)))"));
    ASSERT(truthy("(fboundp 'call-gen-pt-n)"));
    /* redefinition rewrites the present cells */
    ASSERT(bumps("(ffi::%define-cstruct-accessors '((call-gen-pt-x :u32 0)))"));
}

TEST(calling_does_not_bump)
{
    /* Nothing on the per-call path writes the word (the CLAUDE.md hot-path
     * rule).  Heap is idle here, so a collection is not expected either. */
    uint32_t g;
    cl_eval_string("(defun call-gen-leaf (x) (+ x 1))");
    cl_eval_string("(defun call-gen-loop (n) (let ((s 0)) (dotimes (i n s) (setq s (call-gen-leaf s)))))");
    g = cl_call_gen;
    ASSERT(truthy("(eql (call-gen-loop 1000) 1000)"));
    ASSERT_EQ(cl_call_gen, g);
}

int main(void)
{
    test_init();
    setup();

    RUN(starts_nonzero_and_bump_advances_by_one);
    RUN(bump_skips_zero_on_wraparound);
    RUN(setter_writes_the_cell_and_bumps_once);
    RUN(every_collection_bumps);
    RUN(defun_and_redefinition_bump);
    RUN(setf_fdefinition_and_symbol_function_bump);
    RUN(fmakunbound_bumps);
    RUN(trace_untrace_bump);
    RUN(ffi_stub_install_bumps);
    RUN(calling_does_not_bump);

    teardown();
    REPORT();
}
