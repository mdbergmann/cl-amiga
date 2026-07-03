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
#include "core/debugger.h"
#include "core/repl.h"
#include "core/thread.h"
#include "platform/platform.h"

/*
 * Non-local exits and the JIT bookkeeping / error-frame snapshots.
 *
 * (a) CL_NLXFrame.saved_jit_depth: a THROW / RETURN-FROM / GO that
 *     unwinds past cl_jit_invoke longjmps over its C epilogue, so
 *     CT->jit_depth stayed stale.  The next JIT entry then saw
 *     jit_depth > 0 and kept a stale jit_stack_top, so the
 *     conservative GC scan window excluded the live JIT frames —
 *     missed roots under compaction.  Each NLX frame now snapshots
 *     jit_depth at push and every longjmp landing restores it.
 *
 * (b) OP_UWRETHROW's deferred-error replay (pending_throw == 2)
 *     longjmp'd to the top C error frame with NONE of cl_error's
 *     per-frame restores: gc_root_count kept entries pointing into
 *     unwound C stack frames (later gc_mark dereferences freed
 *     stack), jit_depth stayed stale, FASL readers / compiler chain
 *     stranded.  It now goes through cl_error_frame_longjmp.
 *
 * jit_depth is simulated from a registered builtin — the host has no
 * m68k JIT, but the bookkeeping (jit_depth / cl_jit_active_threads /
 * NLX+error-frame snapshots) is identical on both platforms.
 */

extern volatile int cl_jit_active_threads;  /* mem.c */

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
    cl_debugger_init();
    cl_repl_init_no_userinit(1);
}

/* Reset simulated-JIT state between tests (shared heap: boot is too
 * heavy to re-init per test, and catch/throw state is self-contained). */
static void reset_jit_state(void)
{
    cl_jit_active_threads = 0;
    CT->jit_depth = 0;
    CT->jit_stack_top = NULL;
}

/* Builtin that pretends we entered JIT'd code: bumps jit_depth exactly
 * like cl_jit_invoke's prologue.  jit_stack_top stays NULL so the
 * conservative scan is a no-op (we only test the bookkeeping). */
static CL_Obj fake_jit_enter(CL_Obj *args, int n)
{
    (void)args; (void)n;
    if (CT->jit_depth == 0) cl_jit_active_threads++;
    CT->jit_depth++;
    return CL_T;
}

static void register_fake_jit_enter(void)
{
    CL_Obj sym = cl_intern_in("FAKE-JIT-ENTER", 14, cl_package_cl_user);
    CL_Obj fn = cl_make_function(fake_jit_enter, sym, 0, 0);
    ((CL_Symbol *)CL_OBJ_TO_PTR(sym))->function = fn;
}

/* --- (a) NLX landings restore jit_depth --- */

TEST(throw_past_jit_restores_depth)
{
    CL_Obj result;
    int err = 0;

    reset_jit_state();

    CL_CATCH(err);
    if (!err) {
        result = cl_eval_string(
            "(catch 'jt (progn (fake-jit-enter) (throw 'jt 42)))");
        ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 42);
        /* The catch landing must have restored the push-time depth. */
        ASSERT_EQ_INT(CT->jit_depth, 0);
        ASSERT_EQ_INT(cl_jit_active_threads, 0);
    } else {
        ASSERT(0 && "eval signalled unexpectedly");
    }
    CL_UNCATCH();
    reset_jit_state();
}

TEST(return_from_past_jit_restores_depth)
{
    CL_Obj result;
    int err = 0;

    reset_jit_state();

    CL_CATCH(err);
    if (!err) {
        /* RETURN-FROM across a closure boundary forces the NLX (block)
         * path rather than a local jump. */
        result = cl_eval_string(
            "(block b (funcall (lambda () (fake-jit-enter) "
            "(return-from b 7))))");
        ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 7);
        ASSERT_EQ_INT(CT->jit_depth, 0);
        ASSERT_EQ_INT(cl_jit_active_threads, 0);
    } else {
        ASSERT(0 && "eval signalled unexpectedly");
    }
    CL_UNCATCH();
    reset_jit_state();
}

TEST(go_past_jit_restores_depth)
{
    CL_Obj result;
    int err = 0;

    reset_jit_state();

    CL_CATCH(err);
    if (!err) {
        result = cl_eval_string(
            "(let ((n 0))"
            "  (tagbody"
            "     (funcall (lambda () (fake-jit-enter) (go out)))"
            "     (setq n 99)"
            "   out)"
            "  n)");
        ASSERT_EQ_INT(CL_FIXNUM_VAL(result), 0);
        ASSERT_EQ_INT(CT->jit_depth, 0);
        ASSERT_EQ_INT(cl_jit_active_threads, 0);
    } else {
        ASSERT(0 && "eval signalled unexpectedly");
    }
    CL_UNCATCH();
    reset_jit_state();
}

/* --- (b) UWRETHROW deferred-error replay restores error-frame state --- */

TEST(uwrethrow_error_replay_restores_frame_state)
{
    int err = 0;
    CL_Obj dummy = CL_NIL;
    int roots_at_catch;

    reset_jit_state();

    roots_at_catch = gc_root_count;
    CL_CATCH(err);
    if (!err) {
        /* Push a root and enter "JIT" after the frame was pushed: both
         * must be rolled back to the frame snapshot when the deferred
         * error lands here. */
        CL_GC_PROTECT(dummy);
        CT->jit_depth = 1;
        CT->jit_stack_top = NULL;
        cl_jit_active_threads = 1;

        /* The error unwinds into the unwind-protect cleanup first, so
         * the final propagation to this error frame goes through
         * OP_UWRETHROW's pending_throw == 2 replay. */
        cl_eval_string("(unwind-protect (error \"boom\") (cons 1 2))");
        ASSERT(0 && "error did not unwind");
    } else {
        /* Pre-fix: gc_root_count kept the dummy entry (dangling into
         * this frame after a real unwind) and jit_depth stayed 1. */
        ASSERT_EQ_INT(gc_root_count, roots_at_catch);
        ASSERT_EQ_INT(CT->jit_depth, 0);
        ASSERT_EQ_INT(cl_jit_active_threads, 0);
    }
    CL_UNCATCH();
    reset_jit_state();
}

int main(void)
{
    test_init();
    setup();
    register_fake_jit_enter();
    RUN(throw_past_jit_restores_depth);
    RUN(return_from_past_jit_restores_depth);
    RUN(go_past_jit_restores_depth);
    RUN(uwrethrow_error_replay_restores_frame_state);
    cl_mem_shutdown();
    platform_shutdown();
    REPORT();
}
