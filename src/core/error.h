#ifndef CL_ERROR_H
#define CL_ERROR_H

#include "types.h"
#include <setjmp.h>

/*
 * Error handling via setjmp/longjmp.
 * Provides a simple condition system for the REPL to catch errors
 * and continue rather than crashing.
 */

/* Fast non-signal-mask-saving setjmp/longjmp on POSIX.  Plain
 * setjmp on Darwin/Linux calls sigprocmask + (on Darwin) __sigaltstack
 * on every entry/exit — ~1.2k samples in a 30s sento bench profile
 * came from those calls alone, since handler-case sets up an error
 * frame per actor message.  _setjmp/_longjmp skip the mask plumbing,
 * which is safe here because the runtime never depends on signal-mask
 * preservation across longjmp (no signal-blocking critical sections
 * straddle CL_CATCH).  All setjmp/longjmp pairs in the runtime go
 * through these macros so they stay matched.
 *
 * AmigaOS keeps plain setjmp — m68k toolchain libc may not expose
 * _setjmp, and Amiga setjmp is already mask-free. */
#if defined(PLATFORM_POSIX)
#  define CL_SETJMP(buf)        _setjmp(buf)
#  define CL_LONGJMP(buf, val)  _longjmp((buf), (val))
#else
#  define CL_SETJMP(buf)        setjmp(buf)
#  define CL_LONGJMP(buf, val)  longjmp((buf), (val))
#endif

#define CL_ERR_NONE       0
#define CL_ERR_GENERAL    1
#define CL_ERR_TYPE       2
#define CL_ERR_UNBOUND    3
#define CL_ERR_ARGS       4
#define CL_ERR_PARSE      5
#define CL_ERR_OVERFLOW   6
#define CL_ERR_DIVZERO    7
#define CL_ERR_UNDEFINED  8
#define CL_ERR_STORAGE    9
#define CL_ERR_EXIT      10
#define CL_ERR_FILE      11
#define CL_ERR_EOF       12  /* maps to the END-OF-FILE condition (CLHS) */

/* Error handler frame stack */
#define CL_MAX_ERROR_FRAMES 16

typedef struct {
    jmp_buf buf;
    int active;
    /* gc_root_count at the time this frame was pushed.  cl_error_unwind
     * restores `gc_root_count` to this value before longjmping back to
     * the catch site, dropping any CL_GC_PROTECT entries that belonged to
     * C stack frames we are unwinding out of.  Without this restore, the
     * unwound frames' stack-local CL_Obj slots remain in gc_roots[] and
     * subsequent gc_mark walks dereference stale stack memory — see
     * "sento gc_mark SEGV" memory file for the discovery path. */
    int saved_gc_roots;
    /* jit_depth at the time this frame was pushed.  cl_error_unwind
     * restores `jit_depth` to this value before longjmping back to the
     * catch site.  See specs/native-backend.md §"GC interaction". */
    int saved_jit_depth;
    /* debugger_depth at the time this frame was pushed.  cl_error_unwind
     * restores `debugger_depth` to this value on the unwind path so that
     * the recursive-debugger guard (cl_invoke_debugger) cannot leak a
     * permanently-elevated count when a restart / debugger-hook longjmps
     * out past one or more debugger levels.  See debugger.c. */
    int saved_debugger_depth;
    /* in_debugger flag at the time this frame was pushed.  Restored on the
     * unwind path so an error escaping the interactive debugger loop (e.g.
     * a PRINT-OBJECT method that signals) cannot leave cl_in_debugger stuck
     * at 1, which would silently suppress the debugger forever after. */
    int saved_in_debugger;
    /* Active FASL-reader count at the time this frame was pushed.  cl_error
     * longjmps out of a FASL load without running cl_fasl_reader_unregister,
     * so the unwind path restores this count to drop any readers whose stack-
     * local CL_FaslReader was unwound — otherwise a later GC walks the active
     * list and dereferences freed stack memory.  Mirrors saved_gc_roots. */
    int saved_fasl_readers;
    /* Active-compiler chain snapshot (cl_compiler_mark) at push time.  A
     * cl_error longjmp abandons the C frames of any compile in progress within
     * this frame's dynamic extent; cl_error_unwind force-frees every compiler
     * created since, so the chain and env parent pointers stay consistent for
     * later compiles.  Without this, a macroexpansion error during COMPILE-FILE
     * stranded the in-progress compiler and a subsequent form walked a freed
     * parent env (use-after-free in cl_env_resolve_fun_upvalue) — crashed
     * compiling babel's large macro-generated forms.  Mirrors saved_gc_roots. */
    void *saved_active_compiler;
} CL_ErrorFrame;

/* Push an error frame.  Returns the frame index, or -1 on overflow.
 * Must be called BEFORE setjmp so that all side-effects are sequenced
 * before the setjmp point (C99 7.13.1.1 restricts contexts for setjmp). */
int cl_error_frame_push(void);

/* Push a new error frame and setjmp into it.
 * Stores 0 (direct return), an error code (longjmp return),
 * or CL_ERR_OVERFLOW into err_var.
 *
 * C99 §7.13.1.1 restricts setjmp invocation to one of:
 *   - the entire controlling expression of a selection/iteration statement
 *   - a single operand of a relational/equality comparison with an integer
 *     constant expression, itself the entire controlling expression
 *   - the operand of unary !, itself the entire controlling expression
 *   - the entire expression of an expression statement (possibly (void)-cast)
 *
 * `err_var = setjmp(...)` is NOT in any of these contexts — it is UB.  With
 * -O3/LTO the compiler can exploit this UB and emit code that corrupts the
 * stack after longjmp (observed: saved fp/lr slots in the caller's frame
 * get overwritten with stale register spills, so the next epilogue pops
 * bogus return addresses).  The rewrite below puts setjmp in the controlling
 * expression of an `else if`, which is conformant. The longjmp'd code
 * is communicated via the thread-local cl_error_code (set by cl_error()). */
#define CL_CATCH(err_var) do { \
    int _cl_cf_ = cl_error_frame_push(); \
    if (_cl_cf_ < 0) { \
        (err_var) = CL_ERR_OVERFLOW; \
    } else if (CL_SETJMP(cl_error_frames[_cl_cf_].buf) != 0) { \
        (err_var) = cl_error_code; \
    } else { \
        (err_var) = CL_ERR_NONE; \
    } \
} while(0)

/* Pop error frame (call in matching block after CL_CATCH) */
#define CL_UNCATCH() \
    do { if (cl_error_frame_top > 0) { \
        cl_error_frame_top--; \
        cl_error_frames[cl_error_frame_top].active = 0; \
    } } while(0)

/* Functions below all unwind via longjmp (or exit when no CL_CATCH is
 * active) and never return.  Marking them noreturn lets GCC drop the
 * post-call canary checks GCC otherwise inserts after non-noreturn
 * calls — which were firing on caller stack frames after deep
 * longjmps (e.g. through scan_body_for_boxing's CL_CATCH during
 * macro expansion) and aborting bi_compile_file mid-file. */
#if defined(__GNUC__) || defined(__clang__)
#  define CL_NORETURN __attribute__((noreturn))
#else
#  define CL_NORETURN
#endif

/* Signal an error — jumps to nearest CL_CATCH */
CL_NORETURN void cl_error(int code, const char *fmt, ...);

/* Unwind with an existing condition object. Caller must have already
 * signaled via cl_signal_condition. Preserves the condition so the
 * debugger can dispatch PRINT-OBJECT for a meaningful report. */
CL_NORETURN void cl_error_from_condition(CL_Obj condition);

/* Abort the current thread quietly: signal the condition so handler-case /
 * unwind-protect run, but skip the interactive debugger entirely.  Used for
 * controlled internal aborts such as destroy-thread's "Thread destroyed". */
CL_NORETURN void cl_abort_current_thread(const char *msg);

/* Signal a TYPE-ERROR with :datum and :expected-type slots populated,
 * then unwind. Use this whenever the call site knows both the bad
 * value and its expected type — it lets handler-case (type-error)
 * accessors return meaningful values per the HyperSpec. expected_type_name
 * is interned in COMMON-LISP. */
CL_NORETURN void cl_signal_type_error(CL_Obj datum, const char *expected_type_name,
                                      const char *fn_name);

/* Signal an ARITHMETIC-ERROR subtype (FLOATING-POINT-OVERFLOW etc.)
 * with :operation and :operands slots populated, then unwind.  Pass
 * the type name as one of SYM_FLOATING_POINT_OVERFLOW / _UNDERFLOW /
 * SYM_DIVISION_BY_ZERO etc., the operation as a symbol, operands as a
 * Lisp list (or CL_NIL). */
CL_NORETURN void cl_signal_arith_error(CL_Obj type_sym, CL_Obj operation,
                                       CL_Obj operands, const char *fn_name);

/* Signal an UNBOUND-VARIABLE / UNDEFINED-FUNCTION (both subtype
 * CELL-ERROR) with the :name slot populated to NAME, then unwind.
 * cl_error(CL_ERR_UNBOUND/UNDEFINED, ...) builds the condition with
 * only :format-control set, so (cell-error-name c) returns NIL — the
 * ANSI tests (symbol-function.error.5, makunbound.2) assert
 * (eq (cell-error-name c) sym), so use these helpers from the call
 * sites that already hold the symbol. */
CL_NORETURN void cl_signal_unbound_variable(CL_Obj name);
CL_NORETURN void cl_signal_undefined_function(CL_Obj name);

/* Print the current error message */
void cl_error_print(void);

/* Initialize error system */
void cl_error_init(void);

#include "thread.h"

#endif /* CL_ERROR_H */
