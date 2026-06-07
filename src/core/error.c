#include "error.h"
#include "mem.h"
#include "vm.h"
#include "symbol.h"
#include "debugger.h"
#include "printer.h"
#include "color.h"
#include "stream.h"
#include "fasl.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* All error state now lives in CL_Thread.
 * Compatibility macros in thread.h redirect the old names. */

/* Restore the current thread's jit_depth to new_depth, keeping the
 * global cl_jit_active_threads counter consistent.  Used on every
 * cl_error unwind path so longjmp out of JIT'd code drops both the
 * per-thread depth and the global "any-thread-active" gate.
 * See specs/native-backend.md §"GC interaction" option A. */
static void cl_jit_restore_depth(int new_depth)
{
    extern volatile int cl_jit_active_threads;
    int cur = CT->jit_depth;
    if (cur > 0 && new_depth == 0) cl_jit_active_threads--;
    else if (cur == 0 && new_depth > 0) cl_jit_active_threads++;
    CT->jit_depth = new_depth;
    if (new_depth == 0) CT->jit_stack_top = NULL;
}

int cl_error_frame_push(void)
{
    if (cl_error_frame_top >= CL_MAX_ERROR_FRAMES) return -1;
    cl_error_frames[cl_error_frame_top].active = 1;
    /* Snapshot gc_root_count so cl_error_unwind can drop any CL_GC_PROTECT
     * entries pushed by C stack frames we will unwind out of.  Must be
     * sequenced BEFORE setjmp so the value is well-defined on the longjmp
     * return path. */
    cl_error_frames[cl_error_frame_top].saved_gc_roots = gc_root_count;
    cl_error_frames[cl_error_frame_top].saved_jit_depth = CT->jit_depth;
    cl_error_frames[cl_error_frame_top].saved_debugger_depth = cl_debugger_depth;
    cl_error_frames[cl_error_frame_top].saved_in_debugger = cl_in_debugger;
    cl_error_frames[cl_error_frame_top].saved_fasl_readers = cl_fasl_reader_save_count();
    return cl_error_frame_top++;
}

void cl_error_init(void)
{
    cl_error_frame_top = 0;
    cl_error_code = CL_ERR_NONE;
    cl_error_msg[0] = '\0';
}

/* Unwind after the debugger returned — shared between cl_error and
 * cl_error_from_condition. Assumes cl_error_code/cl_error_msg are set. */
CL_NORETURN static void cl_error_unwind(int code)
{
    /* Check for interposing unwind-protect frames in NLX stack.
     * Skip stale frames whose VM frame was reused by a tail call —
     * longjmping to a stale UWPROT restores wrong code/constants. */
    {
        int i;
        for (i = cl_nlx_top - 1; i >= 0; i--) {
            if (cl_nlx_stack[i].type == CL_NLX_UWPROT) {
                CL_Frame *tf = &cl_vm.frames[cl_nlx_stack[i].vm_fp - 1];
                if (tf->code != cl_nlx_stack[i].code)
                    continue;
                cl_pending_throw = 2;
                cl_pending_error_code = code;
                strncpy(cl_pending_error_msg, cl_error_msg,
                        sizeof(cl_pending_error_msg) - 1);
                cl_pending_error_msg[sizeof(cl_pending_error_msg) - 1] = '\0';
                cl_nlx_top = i;
                CL_LONGJMP(cl_nlx_stack[i].buf, 1);
            }
        }
    }

    /* No UWPROT found — propagating to C error handler. */
    if (cl_error_frame_top > 1) {
        /* Nested error frame — jump to it without destroying global state.
         * The caller is responsible for restoring VM/binding state.
         * Don't decrement here — CL_UNCATCH at the catch site pops.
         *
         * Restore gc_root_count to the value captured at CL_CATCH push
         * time: any entries pushed since then live in C stack frames we
         * are unwinding out of and would dangle in gc_roots[]. */
        gc_root_count = cl_error_frames[cl_error_frame_top - 1].saved_gc_roots;
        cl_jit_restore_depth(cl_error_frames[cl_error_frame_top - 1].saved_jit_depth);
        cl_debugger_depth = cl_error_frames[cl_error_frame_top - 1].saved_debugger_depth;
        cl_in_debugger = cl_error_frames[cl_error_frame_top - 1].saved_in_debugger;
        cl_fasl_reader_restore_count(cl_error_frames[cl_error_frame_top - 1].saved_fasl_readers);
        CL_LONGJMP(cl_error_frames[cl_error_frame_top - 1].buf, code);
    }

    /* Outermost error frame (REPL) — full cleanup.
     * NLX frames (catch/uwprot) are invalid once we leave the VM,
     * so clear the NLX stack and reset pending state.
     * Restore all dynamic bindings before leaving the VM.
     * Reset GC root stack — longjmp invalidates stack-local roots. */
    cl_nlx_top = 0;
    cl_saved_pending_top = 0;
    cl_pending_throw = 0;
    cl_dynbind_restore_to(0);
    cl_handler_top = 0;
    cl_restart_top = 0;
    cl_gc_reset_roots();
    cl_jit_restore_depth(0);
    cl_debugger_depth = 0;
    cl_in_debugger = 0;

    if (cl_error_frame_top > 0) {
        /* Drop any active FASL readers — their stack-local CL_FaslReaders are
         * unwound away.  Restore to the outermost frame's snapshot (normally 0). */
        cl_fasl_reader_restore_count(cl_error_frames[cl_error_frame_top - 1].saved_fasl_readers);
        CL_LONGJMP(cl_error_frames[cl_error_frame_top - 1].buf, code);
    }

    /* No error frame — fatal */
    cl_color_set(CL_COLOR_RED);
    platform_write_string("FATAL ERROR: ");
    platform_write_string(cl_error_msg);
    cl_color_reset();
    platform_write_string("\n");
    exit(1);
}

void cl_error(int code, const char *fmt, ...)
{
    va_list ap;

    cl_error_code = code;

    va_start(ap, fmt);
    vsnprintf(cl_error_msg, sizeof(cl_error_msg), fmt, ap);
    va_end(ap);

    /* Exit request: skip debugger/conditions, just unwind */
    if (code == CL_ERR_EXIT) {
        cl_nlx_top = 0;
        cl_saved_pending_top = 0;
        cl_pending_throw = 0;
        cl_dynbind_restore_to(0);
        cl_handler_top = 0;
        cl_restart_top = 0;
        cl_gc_reset_roots();
        cl_jit_restore_depth(0);
        cl_debugger_depth = 0;
        cl_in_debugger = 0;
        if (cl_error_frame_top > 0) {
            /* Don't decrement here — CL_UNCATCH at the catch site pops.
             * For nested frames, restore gc_root_count to the catch-site
             * snapshot so any roots pushed in unwound C frames are dropped
             * (cl_gc_reset_roots above zeroed it; that suffices, but the
             * explicit restore is symmetric with the cl_error_unwind path
             * and keeps any permanent roots installed by the outer frame). */
            gc_root_count = cl_error_frames[cl_error_frame_top - 1].saved_gc_roots;
            cl_jit_restore_depth(cl_error_frames[cl_error_frame_top - 1].saved_jit_depth);
            cl_fasl_reader_restore_count(cl_error_frames[cl_error_frame_top - 1].saved_fasl_readers);
            CL_LONGJMP(cl_error_frames[cl_error_frame_top - 1].buf, code);
        }
        exit(cl_exit_code);
    }

    /* Capture backtrace while VM frames are still intact */
    cl_capture_backtrace();

    /* Signal through condition handler stack before unwinding */
    {
        CL_Obj cond = cl_create_condition_from_error(code, cl_error_msg);
        cl_signal_condition(cond);
        /* Invoke debugger before unwinding (returns if user picks "top level") */
        cl_invoke_debugger(cond);
    }

    cl_error_unwind(code);
}

/* Abort the current thread quietly.
 *
 * Used by destroy-thread: the "Thread destroyed" unwind is a controlled,
 * internal abort requested by another thread (mp:destroy-thread), not a
 * user-facing error that warrants the debugger.  We still signal the condition
 * so any handler-case / unwind-protect in the thread body runs (matching
 * cl_error's behaviour and CCL/SBCL thread-abort semantics), but we deliberately
 * skip cl_invoke_debugger so a killed SLY worker unwinds silently instead of
 * emitting a "Debugger entered" banner on every disconnect. */
CL_NORETURN void cl_abort_current_thread(const char *msg)
{
    cl_error_code = CL_ERR_GENERAL;
    strncpy(cl_error_msg, msg, sizeof(cl_error_msg) - 1);
    cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';

    /* Capture backtrace while VM frames are still intact, then signal so
     * handler-case / unwind-protect cleanups run — but never enter the
     * debugger. */
    cl_capture_backtrace();
    {
        CL_Obj cond = cl_create_condition_from_error(CL_ERR_GENERAL, cl_error_msg);
        cl_signal_condition(cond);
    }

    cl_error_unwind(CL_ERR_GENERAL);  /* no debugger — does not return */
}

/* Unwind with an existing condition object (e.g. one built by the Lisp
 * ERROR builtin). Caller is responsible for having already run
 * cl_signal_condition — we skip re-signaling to avoid running handlers twice.
 * Unlike cl_error, this preserves the original condition's type and slots
 * so the debugger can dispatch PRINT-OBJECT and show a meaningful report. */
void cl_error_from_condition(CL_Obj condition)
{
    CL_Condition *c;
    CL_Obj report = CL_NIL;

    if (!CL_CONDITION_P(condition)) {
        cl_error(CL_ERR_GENERAL, "cl_error_from_condition: not a condition");
    }

    /* Map condition type → numeric code so callers (incl. C-level tests
     * via eval_print's "ERROR:N") see the same code as the legacy
     * cl_error(CL_ERR_*, ...) path. */
    {
        CL_Obj t = ((CL_Condition *)CL_OBJ_TO_PTR(condition))->type_name;
        if      (t == SYM_TYPE_ERROR)               cl_error_code = CL_ERR_TYPE;
        else if (t == SYM_UNBOUND_VARIABLE_COND)    cl_error_code = CL_ERR_UNBOUND;
        else if (t == SYM_UNDEFINED_FUNCTION_COND)  cl_error_code = CL_ERR_UNDEFINED;
        else if (t == SYM_DIVISION_BY_ZERO)         cl_error_code = CL_ERR_DIVZERO;
        else if (t == SYM_ARITHMETIC_ERROR)         cl_error_code = CL_ERR_OVERFLOW;
        else if (t == SYM_PROGRAM_ERROR)            cl_error_code = CL_ERR_ARGS;
        else if (t == SYM_FILE_ERROR)               cl_error_code = CL_ERR_FILE;
        else                                        cl_error_code = CL_ERR_GENERAL;
    }

    /* Try PRINT-OBJECT dispatch (e.g. ASDF conditions rely on it for their
     * report). Falls through to the condition's report_string and finally
     * the type name. */
    if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK)) {
        CL_Obj hook_val = cl_symbol_value(SYM_PRINT_OBJECT_HOOK);
        if (!CL_NULL_P(hook_val)) {
            CL_Obj hook_args[1];
            CL_Obj result;
            hook_args[0] = condition;
            result = cl_vm_apply(hook_val, hook_args, 1);
            if (!CL_NULL_P(result) && CL_HEAP_P(result) &&
                CL_HDR_TYPE(CL_OBJ_TO_PTR(result)) == TYPE_STRING) {
                report = result;
            }
        }
    }

    c = (CL_Condition *)CL_OBJ_TO_PTR(condition);
    if (CL_NULL_P(report)) {
        report = c->report_string;
    }

    if (!CL_NULL_P(report) && CL_HEAP_P(report) &&
        CL_HDR_TYPE(CL_OBJ_TO_PTR(report)) == TYPE_STRING) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(report);
        char typebuf[96];
        cl_prin1_to_string(c->type_name, typebuf, sizeof(typebuf));
        snprintf(cl_error_msg, sizeof(cl_error_msg), "%s: %s",
                 typebuf, s->data);
    } else {
        cl_prin1_to_string(c->type_name, cl_error_msg, sizeof(cl_error_msg));
    }

    cl_capture_backtrace();
    cl_invoke_debugger(condition);
    cl_error_unwind(cl_error_code);
}

void cl_error_print(void)
{
    cl_color_set(CL_COLOR_RED);
    cl_write_cstring_to_error("ERROR: ");
    cl_write_cstring_to_error(cl_error_msg);
    cl_color_reset();
    cl_write_cstring_to_error("\n");
    if (cl_backtrace_buf[0] != '\0') {
        cl_write_cstring_to_error("Backtrace:\n");
        cl_write_cstring_to_error(cl_backtrace_buf);
    }
}
