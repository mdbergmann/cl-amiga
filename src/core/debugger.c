/*
 * debugger.c — Interactive debugger for CL-Amiga
 *
 * When an unhandled error occurs in the interactive REPL, displays
 * the condition, backtrace, and available restarts, then lets the
 * user choose a restart or evaluate expressions for inspection.
 */

#include "debugger.h"
#include "builtins.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "error.h"
#include "vm.h"
#include "printer.h"
#include "repl.h"
#include "color.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int cl_debugger_enabled = 0;
/* cl_in_debugger is now in CL_Thread (macro from thread.h) */

static CL_Obj SYM_DEBUGGER_HOOK;

/* Helper to register a builtin (also exports from CL package since
 * cl_debugger_init runs after the bulk cl_package_export_all_cl_symbols call) */
static void defun(const char *name, CL_CFunc func, int min, int max)
{
    CL_Obj sym = cl_intern_in(name, (uint32_t)strlen(name), cl_package_cl);
    CL_Obj fn = cl_make_function(func, sym, min, max);
    CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
    s->function = fn;
    cl_export_symbol(sym, cl_package_cl);
}

/* Display condition info.
 *
 * Format: "Debugger entered: <TYPE>: <report>"
 * The report is obtained via *print-object-hook* if the condition's class
 * has a PRINT-OBJECT method (e.g. ASDF conditions), falling back to the
 * condition's :format-control/:format-arguments report string, and
 * finally to just the type name. */
static void display_condition(CL_Obj condition)
{
    CL_Condition *cond = (CL_Condition *)CL_OBJ_TO_PTR(condition);
    char buf[256];
    CL_Obj report_str = CL_NIL;

    /* Try PRINT-OBJECT dispatch via the hook set up by clos.lisp. */
    if (!CL_NULL_P(SYM_PRINT_OBJECT_HOOK)) {
        CL_Obj hook_val = cl_symbol_value(SYM_PRINT_OBJECT_HOOK);
        if (!CL_NULL_P(hook_val)) {
            CL_Obj hook_args[1];
            CL_Obj result;
            hook_args[0] = condition;
            result = cl_vm_apply(hook_val, hook_args, 1);
            if (!CL_NULL_P(result) && CL_HEAP_P(result) &&
                CL_HDR_TYPE(CL_OBJ_TO_PTR(result)) == TYPE_STRING) {
                report_str = result;
            }
        }
    }

    cl_color_set(CL_COLOR_BOLD_RED);
    platform_write_string("\nDebugger entered: ");
    cl_prin1_to_string(cond->type_name, buf, sizeof(buf));
    platform_write_string(buf);

    if (!CL_NULL_P(report_str)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(report_str);
        platform_write_string(": ");
        platform_write_string(s->data);
    } else if (!CL_NULL_P(cond->report_string)) {
        CL_String *s = (CL_String *)CL_OBJ_TO_PTR(cond->report_string);
        platform_write_string(": ");
        platform_write_string(s->data);
    }
    cl_color_reset();
    platform_write_string("\n");
}

/* Display backtrace */
static void display_backtrace(void)
{
    if (cl_backtrace_buf[0] != '\0') {
        platform_write_string("\nBacktrace:\n");
        platform_write_string(cl_backtrace_buf);
        platform_write_string("\n");
    }
}

/* Display available restarts and return count (including "top level") */
static int display_restarts(void)
{
    int i, idx = 0;
    char numbuf[16];

    platform_write_string("Available restarts:\n");

    for (i = cl_restart_top - 1; i >= 0; i--) {
        char namebuf[128];
        snprintf(numbuf, sizeof(numbuf), "  %d: ", idx);
        platform_write_string(numbuf);
        cl_prin1_to_string(cl_restart_stack[i].name, namebuf, sizeof(namebuf));
        platform_write_string(namebuf);
        platform_write_string("\n");
        idx++;
    }

    /* Always add "Return to top level" as last option */
    snprintf(numbuf, sizeof(numbuf), "  %d: ", idx);
    platform_write_string(numbuf);
    platform_write_string("Return to top level\n");

    return idx + 1;
}

/* Display help */
static void display_help(void)
{
    platform_write_string("Debugger commands:\n");
    platform_write_string("  <number>  — invoke restart by number\n");
    platform_write_string("  :bt       — show backtrace\n");
    platform_write_string("  :q        — return to top level\n");
    platform_write_string("  :help     — show this help\n");
    platform_write_string("  <expr>    — evaluate a Lisp expression\n");
}

/* Invoke restart at index (0 = topmost restart, counting down) */
static void invoke_restart_at(int idx)
{
    int stack_idx = cl_restart_top - 1 - idx;

    if (stack_idx < 0 || stack_idx >= cl_restart_top) {
        platform_write_string("Invalid restart index\n");
        return;
    }

    {
        CL_Obj result = cl_vm_apply(cl_restart_stack[stack_idx].handler,
                                     NULL, 0);
        cl_throw_to_tag(cl_restart_stack[stack_idx].tag, result);
        /* Does not return (longjmp) */
    }
}

/* (invoke-debugger condition) — Lisp builtin */
static CL_Obj bi_invoke_debugger(CL_Obj *args, int n)
{
    CL_UNUSED(n);
    if (!CL_CONDITION_P(args[0]))
        cl_error(CL_ERR_TYPE, "INVOKE-DEBUGGER: argument must be a condition");

    cl_invoke_debugger(args[0]);

    /* If debugger returns (user chose "top level"), signal error to unwind */
    cl_error(CL_ERR_GENERAL, "Return to top level");
    return CL_NIL; /* unreachable */
}

/* Jump directly to the top-level (REPL) error frame, clearing all
 * VM/NLX/handler/restart state.  Used by :q and "Return to top level"
 * so a single command always reaches the REPL prompt. */
static void jump_to_top_level(void)
{
    cl_in_debugger = 0;
    cl_nlx_top = 0;
    cl_pending_throw = 0;
    cl_dynbind_restore_to(0);
    cl_handler_top = 0;
    cl_restart_top = 0;
    cl_gc_reset_roots();
    cl_vm.sp = 0;
    cl_vm.fp = 0;

    if (cl_error_frame_top > 0) {
        int code = cl_error_code;
        /* Reset to bottom-most frame (the REPL's error frame) */
        cl_error_frame_top = 1;
        cl_error_frames[0].active = 0;
        CL_LONGJMP(cl_error_frames[0].buf, code);
    }

    /* No error frame — fatal (should not happen in interactive REPL) */
    platform_write_string("No error frame — cannot return to top level\n");
}

/* Core debugger loop */
void cl_invoke_debugger(CL_Obj condition)
{
    int num_restarts;
    char line[1024];

    /* Recursion guard */
    if (cl_in_debugger)
        return;

    /* Always check *debugger-hook* per CL spec, even if interactive
     * debugger is disabled (e.g., batch mode or tests) */
    if (!CL_NULL_P(SYM_DEBUGGER_HOOK) && CL_SYMBOL_P(SYM_DEBUGGER_HOOK))
    {
        CL_Obj hook_val = cl_symbol_value(SYM_DEBUGGER_HOOK);

        if (!CL_NULL_P(hook_val)) {
            CL_Obj saved_hook = hook_val;
            CL_Obj hook_args[2];
            int err;

            /* Set *debugger-hook* to NIL before calling hook */
            cl_set_symbol_value(SYM_DEBUGGER_HOOK, CL_NIL);

            hook_args[0] = condition;
            hook_args[1] = saved_hook;

            /* Call hook protected by CL_CATCH — if hook transfers
             * control (longjmp), we never reach the restore */
            CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                cl_vm_apply(saved_hook, hook_args, 2);
                CL_UNCATCH();
            } else {
                CL_UNCATCH();
                /* Hook signaled an error — restore and fall through */
            }

            /* Restore hook value */
            cl_set_symbol_value(SYM_DEBUGGER_HOOK, saved_hook);
        }
    }

    /* Interactive debugger loop only if enabled */
    if (!cl_debugger_enabled)
        return;

    cl_in_debugger = 1;

    display_condition(condition);
    display_backtrace();
    num_restarts = display_restarts();
    display_help();

    /* Mini-REPL loop */
    cl_color_set(CL_COLOR_DIM_MAGENTA);
    platform_write_string("\nDebug> ");
    cl_color_reset();

    while (platform_read_line(line, sizeof(line))) {
        /* Skip empty lines */
        if (line[0] == '\0') {
            cl_color_set(CL_COLOR_DIM_MAGENTA);
            platform_write_string("Debug> ");
            cl_color_reset();
            continue;
        }

        /* Check for commands */
        if (strcmp(line, ":bt") == 0) {
            display_backtrace();
            cl_color_set(CL_COLOR_DIM_MAGENTA);
            platform_write_string("Debug> ");
            cl_color_reset();
            continue;
        }

        if (strcmp(line, ":q") == 0) {
            jump_to_top_level(); /* longjmp — does not return */
        }

        if (strcmp(line, ":help") == 0) {
            display_help();
            cl_color_set(CL_COLOR_DIM_MAGENTA);
            platform_write_string("Debug> ");
            cl_color_reset();
            continue;
        }

        /* Check if input is a number (restart index) */
        {
            char *endp;
            long idx = strtol(line, &endp, 10);
            if (endp != line && *endp == '\0') {
                /* It's a number */
                if (idx >= 0 && idx < num_restarts) {
                    if (idx == num_restarts - 1) {
                        /* "Return to top level" */
                        jump_to_top_level(); /* longjmp — does not return */
                    }
                    /* Invoke the restart — this does not return (longjmp) */
                    cl_in_debugger = 0;
                    invoke_restart_at((int)idx);
                    /* If we somehow get here, re-enter debugger */
                    cl_in_debugger = 1;
                } else {
                    platform_write_string("Invalid restart number\n");
                }
                cl_color_set(CL_COLOR_DIM_MAGENTA);
                platform_write_string("Debug> ");
                cl_color_reset();
                continue;
            }
        }

        /* Otherwise, eval as Lisp expression */
        {
            int err;

            /* Reset VM state before eval */
            cl_vm.sp = 0;
            cl_vm.fp = 0;

            CL_CATCH(err);
            if (err == CL_ERR_NONE) {
                CL_Obj result = cl_eval_string(line);
                char buf[512];
                cl_prin1_to_string(result, buf, sizeof(buf));
                platform_write_string(buf);
                platform_write_string("\n");
                CL_UNCATCH();
            } else {
                CL_UNCATCH();
                cl_color_set(CL_COLOR_RED);
                platform_write_string("Error during eval: ");
                platform_write_string(cl_error_msg);
                cl_color_reset();
                platform_write_string("\n");
                /* Reset VM state after error */
                cl_vm.sp = 0;
                cl_vm.fp = 0;
            }
        }

        cl_color_set(CL_COLOR_DIM_MAGENTA);
        platform_write_string("Debug> ");
        cl_color_reset();
    }

    cl_in_debugger = 0;
}

void cl_debugger_init(void)
{
    /* Intern *DEBUGGER-HOOK* as special variable, value NIL */
    SYM_DEBUGGER_HOOK = cl_intern_in("*DEBUGGER-HOOK*", 15, cl_package_cl);
    {
        CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(SYM_DEBUGGER_HOOK);
        s->flags |= CL_SYM_SPECIAL;
        s->value = CL_NIL;
    }
    cl_export_symbol(SYM_DEBUGGER_HOOK, cl_package_cl);

    /* Register invoke-debugger builtin */
    defun("INVOKE-DEBUGGER", bi_invoke_debugger, 1, 1);

    /* Register cached symbols for GC compaction forwarding */
    cl_gc_register_root(&SYM_DEBUGGER_HOOK);
}
