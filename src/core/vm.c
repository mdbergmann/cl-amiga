#include "vm.h"
#include "opcodes.h"
#include "symbol.h"
#include "mem.h"
#include "bignum.h"
#include "ratio.h"
#include "float.h"
#include "error.h"
#include "compiler.h"
#include "printer.h"
#include "../platform/platform.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

CL_VM cl_vm;

/* Debug: UWP stack watchpoint.  Activate with -DCL_DEBUG_UWP */
#ifdef CL_DEBUG_UWP
static int dbg_watch_idx = -1;
static CL_Obj dbg_watch_orig = 0;
static int dbg_watch_nlx = -1;
static void dbg_check_watch_impl(const char *where) {
    if (dbg_watch_idx >= 0 && dbg_watch_nlx >= 0 &&
        cl_nlx_top > dbg_watch_nlx &&
        cl_vm.stack[dbg_watch_idx] != dbg_watch_orig) {
        fprintf(stderr, "[WATCH] stack[%d] changed from 0x%x to 0x%x by %s, fp=%d sp=%d\n",
                dbg_watch_idx, (unsigned)dbg_watch_orig,
                (unsigned)cl_vm.stack[dbg_watch_idx], where, cl_vm.fp, cl_vm.sp);
        {
            int fi;
            for (fi = cl_vm.fp - 1; fi >= 0 && fi >= cl_vm.fp - 15; fi--) {
                CL_Frame *f = &cl_vm.frames[fi];
                const char *fn = "?";
                CL_Bytecode *fbc = NULL;
                if (CL_CLOSURE_P(f->bytecode)) {
                    CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(f->bytecode);
                    fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
                } else if (CL_BYTECODE_P(f->bytecode)) {
                    fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(f->bytecode);
                }
                if (fbc && fbc->name != CL_NIL) fn = cl_symbol_name(fbc->name);
                else if (fbc) fn = "<anon>";
                fprintf(stderr, "  frame[%d] fn=%s bp=%d n_locals=%d\n",
                        fi, fn, f->bp, f->n_locals);
            }
        }
        dbg_watch_idx = -1;
    }
}
#define DBG_CHECK_WATCH(w) dbg_check_watch_impl(w)
#else
#define DBG_CHECK_WATCH(w) ((void)0)
#endif

/* Multiple values */
CL_Obj cl_mv_values[CL_MAX_MV];
int cl_mv_count = 1;

/* Trace */
int cl_trace_depth = 0;
int cl_trace_count = 0;

/* Backtrace */
char cl_backtrace_buf[CL_BACKTRACE_BUF_SIZE];

/* NLX stack */
CL_NLXFrame cl_nlx_stack[CL_MAX_NLX_FRAMES];
int cl_nlx_top = 0;

/* Dynamic binding stack */
CL_DynBinding cl_dyn_stack[CL_MAX_DYN_BINDINGS];
int cl_dyn_top = 0;

/* Handler binding stack */
CL_HandlerBinding cl_handler_stack[CL_MAX_HANDLER_BINDINGS];
int cl_handler_top = 0;
int cl_handler_floor = 0;

/* Restart binding stack */
CL_RestartBinding cl_restart_stack[CL_MAX_RESTART_BINDINGS];
int cl_restart_top = 0;
int cl_restart_floor = 0;

void cl_dynbind_restore_to(int mark)
{
    while (cl_dyn_top > mark) {
        cl_dyn_top--;
        {
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(cl_dyn_stack[cl_dyn_top].symbol);
            s->value = cl_dyn_stack[cl_dyn_top].old_value;
        }
    }
}

/* Pending throw state */
int cl_pending_throw = 0;
CL_Obj cl_pending_tag = 0;
CL_Obj cl_pending_value = 0;
int cl_pending_error_code = 0;
char cl_pending_error_msg[512];

void cl_vm_init(uint32_t stack_size, int frame_size)
{
    if (stack_size == 0) stack_size = CL_VM_STACK_SIZE;
    if (frame_size == 0) frame_size = CL_VM_FRAME_SIZE;

    cl_vm.stack = (CL_Obj *)platform_alloc(stack_size * sizeof(CL_Obj));
    cl_vm.stack_size = stack_size;
    cl_vm.frames = (CL_Frame *)platform_alloc(frame_size * sizeof(CL_Frame));
    cl_vm.frame_size = frame_size;

    cl_vm.sp = 0;
    cl_vm.fp = 0;
    cl_nlx_top = 0;
    cl_dyn_top = 0;
    cl_handler_top = 0;
    cl_restart_top = 0;
    cl_pending_throw = 0;
    cl_mv_count = 1;
    cl_trace_depth = 0;
}

void cl_vm_shutdown(void)
{
    if (cl_vm.stack) {
        platform_free(cl_vm.stack);
        cl_vm.stack = NULL;
    }
    if (cl_vm.frames) {
        platform_free(cl_vm.frames);
        cl_vm.frames = NULL;
    }
}

void cl_vm_push(CL_Obj val)
{
    if (cl_vm.sp >= (int)cl_vm.stack_size)
        cl_error(CL_ERR_OVERFLOW, "VM stack overflow");
    cl_vm.stack[cl_vm.sp++] = val;
#ifdef CL_DEBUG_UWP
    if (dbg_watch_idx >= 0 && (cl_vm.sp - 1) == dbg_watch_idx && val != dbg_watch_orig)
        DBG_CHECK_WATCH("cl_vm_push");
#endif
}

CL_Obj cl_vm_pop(void)
{
    if (cl_vm.sp <= 0)
        cl_error(CL_ERR_OVERFLOW, "VM stack underflow");
    return cl_vm.stack[--cl_vm.sp];
}

/* Forward declarations */
static CL_Obj call_builtin(CL_Function *func, CL_Obj *args, int nargs);
static CL_Obj cl_vm_run(int base_fp, int base_nlx);
void vm_trace_dump(void);

CL_Obj cl_vm_apply(CL_Obj func, CL_Obj *args, int nargs)
{
    /*
     * Apply: C builtins called directly (no VM entry),
     * bytecode/closures use a stack-local stub frame + cl_vm_run
     * (no heap allocation, no cl_vm_eval recursion).
     */
    int i;

    /* Clamp nargs to 255 (OP_CALL u8 limit) */
    if (nargs > 255) nargs = 255;

    /* C builtins: call directly, no VM entry needed. */
    if (CL_FUNCTION_P(func)) {
        CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func);
        return call_builtin(f, args, nargs);
    }

    /* Bytecode / closure: push func+args on VM stack, set up a tiny
     * stub frame with OP_CALL+OP_HALT, and run the dispatch loop.
     * The stub code lives in frame->stub_code (heap-allocated with the
     * frame array) so it survives longjmp past this C frame. */
    {
        CL_Frame *frame;
        int base_fp, base_nlx;
        CL_Obj result;

#ifdef DEBUG_VM
        cl_check_c_stack("cl_vm_apply");
#endif

        /* Push a minimal stub frame BEFORE pushing func+args.
         * bp = current sp, n_locals = 0.  After OP_CALL consumes
         * func+args and pushes the result, sp = bp+1 so OP_HALT
         * sees the result. */
        base_fp = cl_vm.fp;
        base_nlx = cl_nlx_top;
        if (cl_vm.fp >= cl_vm.frame_size)
            cl_error(CL_ERR_OVERFLOW, "VM frame stack overflow");

        frame = &cl_vm.frames[cl_vm.fp++];
        /* Build stub code in the frame itself (not on C stack) */
        frame->stub_code[0] = OP_CALL;
        frame->stub_code[1] = (uint8_t)nargs;
        frame->stub_code[2] = OP_HALT;
        frame->bytecode = CL_NIL;
        frame->code = frame->stub_code;
        frame->constants = NULL;
        frame->ip = 0;
        frame->bp = cl_vm.sp;  /* bp before func+args */
        frame->n_locals = 0;
        frame->nargs = 0;

        /* Push function and arguments onto VM stack */
        cl_vm_push(func);
        for (i = 0; i < nargs; i++)
            cl_vm_push(args[i]);

        result = cl_vm_run(base_fp, base_nlx);
        return result;
    }
}

static uint16_t read_u16(uint8_t *code, uint32_t *ip)
{
    uint16_t val = (code[*ip] << 8) | code[*ip + 1];
    *ip += 2;
    return val;
}

static int16_t read_i16(uint8_t *code, uint32_t *ip)
{
    return (int16_t)read_u16(code, ip);
}

/* --- Trace helpers --- */

static CL_Obj get_func_name(CL_Obj func_obj)
{
    if (CL_FUNCTION_P(func_obj)) {
        return ((CL_Function *)CL_OBJ_TO_PTR(func_obj))->name;
    } else if (CL_CLOSURE_P(func_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
        return ((CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode))->name;
    } else if (CL_BYTECODE_P(func_obj)) {
        return ((CL_Bytecode *)CL_OBJ_TO_PTR(func_obj))->name;
    }
    return CL_NIL;
}

static int is_func_traced(CL_Obj func_obj)
{
    CL_Obj name;
    if (cl_trace_count <= 0) return 0;
    name = get_func_name(func_obj);
    if (!CL_NULL_P(name) && CL_SYMBOL_P(name)) {
        return (((CL_Symbol *)CL_OBJ_TO_PTR(name))->flags & CL_SYM_TRACED) != 0;
    }
    return 0;
}

static void trace_print_entry(CL_Obj name_sym, CL_Obj *args, int nargs)
{
    char buf[128];
    int i;
    for (i = 0; i < cl_trace_depth; i++)
        platform_write_string("  ");
    snprintf(buf, sizeof(buf), "%d: (", cl_trace_depth);
    platform_write_string(buf);
    platform_write_string(cl_symbol_name(name_sym));
    for (i = 0; i < nargs; i++) {
        platform_write_string(" ");
        cl_prin1_to_string(args[i], buf, sizeof(buf));
        platform_write_string(buf);
    }
    platform_write_string(")\n");
}

static void trace_print_exit(CL_Obj name_sym, CL_Obj result)
{
    char buf[128];
    int i;
    for (i = 0; i < cl_trace_depth; i++)
        platform_write_string("  ");
    snprintf(buf, sizeof(buf), "%d: ", cl_trace_depth);
    platform_write_string(buf);
    platform_write_string(cl_symbol_name(name_sym));
    platform_write_string(" returned ");
    cl_prin1_to_string(result, buf, sizeof(buf));
    platform_write_string(buf);
    platform_write_string("\n");
}

/* --- Backtrace capture --- */

/* Look up source line for a given IP in a bytecode's line map.
 * Returns 0 if no mapping found. Uses binary-like scan (entries sorted by pc). */
static int lookup_source_line(CL_Bytecode *bc, uint32_t ip)
{
    int i, best_line = 0;
    if (!bc->line_map || bc->line_map_count == 0) return 0;
    for (i = 0; i < bc->line_map_count; i++) {
        if (bc->line_map[i].pc <= ip)
            best_line = bc->line_map[i].line;
        else
            break;
    }
    return best_line;
}

static CL_Bytecode *get_frame_bytecode(CL_Frame *f)
{
    CL_Obj bc_obj = f->bytecode;
    if (CL_BYTECODE_P(bc_obj))
        return (CL_Bytecode *)CL_OBJ_TO_PTR(bc_obj);
    if (CL_CLOSURE_P(bc_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(bc_obj);
        if (CL_BYTECODE_P(cl->bytecode))
            return (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    }
    return NULL;
}

void cl_capture_backtrace(void)
{
    int i, pos = 0;
    int max_show = 20;
    int depth = 0;

    cl_backtrace_buf[0] = '\0';
    if (cl_vm.fp <= 0) return;

    for (i = cl_vm.fp - 1; i >= 0 && depth < max_show; i--, depth++) {
        CL_Frame *f = &cl_vm.frames[i];
        CL_Obj name = get_func_name(f->bytecode);
        CL_Bytecode *bc = get_frame_bytecode(f);
        int line = 0;
        const char *file = NULL;
        int n;

        if (bc) {
            line = lookup_source_line(bc, f->ip);
            file = bc->source_file;
        }

        n = snprintf(cl_backtrace_buf + pos,
                     CL_BACKTRACE_BUF_SIZE - pos, "  %d: ", depth);
        pos += n;
        if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;

        if (!CL_NULL_P(name) && CL_SYMBOL_P(name)) {
            if (file && line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s (%s:%d)\n", cl_symbol_name(name), file, line);
            } else if (line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s (line %d)\n", cl_symbol_name(name), line);
            } else {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "%s\n", cl_symbol_name(name));
            }
        } else {
            if (file && line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous> (%s:%d)\n", file, line);
            } else if (line > 0) {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous> (line %d)\n", line);
            } else {
                n = snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "<anonymous>\n");
            }
        }
        pos += n;
        if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;
    }

    if (cl_vm.fp > max_show) {
        snprintf(cl_backtrace_buf + pos,
                 CL_BACKTRACE_BUF_SIZE - pos,
                 "  ... %d more frames\n", cl_vm.fp - max_show);
    }
}

/* Check if an NLX frame is stale (its target frame was reused by a different function).
 * This happens when a tail call or return reuses a frame after an NLX frame was established. */
static int nlx_frame_is_stale(CL_NLXFrame *nlx)
{
    CL_Frame *target = &cl_vm.frames[nlx->vm_fp - 1];
    return target->code != nlx->code;
}

/* Call a built-in C function */
static CL_Obj call_builtin(CL_Function *func, CL_Obj *args, int nargs)
{
    CL_Obj result;
    CL_CFunc fptr;
#ifdef DEBUG_VM
    cl_check_c_stack("call_builtin");
#endif
    if (nargs < func->min_args) {
        cl_error(CL_ERR_ARGS, "%s: too few arguments (got %d, need %d)",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name),
                 nargs, func->min_args);
    }
    if (func->max_args >= 0 && nargs > func->max_args) {
        cl_error(CL_ERR_ARGS, "%s: too many arguments (got %d, max %d)",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name),
                 nargs, func->max_args);
    }
    cl_mv_count = 1;
    fptr = func->func;
    if (!fptr) {
        cl_error(CL_ERR_TYPE, "NULL C function pointer in %s",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name));
    }
    result = fptr(args, nargs);
    cl_mv_values[0] = result;
    return result;
}

/* C stack base for overflow detection */
char *cl_c_stack_base = NULL;

#ifdef DEBUG_VM
/* Recursion depth tracking for cl_vm_eval */
int vm_eval_depth = 0;
static int vm_eval_max_depth = 0;

#define C_STACK_LIMIT (4 * 1024 * 1024)  /* 4MB of 8MB, leave 4MB margin */

long c_stack_max_seen = 0;

void cl_check_c_stack(const char *context)
{
    volatile char probe;
    long used;
    if (!cl_c_stack_base) cl_c_stack_base = (char *)&probe;
    used = (long)(cl_c_stack_base - (char *)&probe);
    if (used < 0) used = -used;  /* Handle stack growing up or down */
    if (used > c_stack_max_seen) {
        c_stack_max_seen = used;
        if (c_stack_max_seen > 1024 * 1024)  /* Print only above 1MB */
            fprintf(stderr, "[STACK] %s: %ldKB\n", context, used / 1024);
    }
    if (used > C_STACK_LIMIT) {
        fprintf(stderr, "[STACK] OVERFLOW in %s: %ldKB (limit=%ldKB)\n",
                context, used / 1024, (long)C_STACK_LIMIT / 1024);
        cl_error(CL_ERR_OVERFLOW,
                 "C stack overflow in %s (used=%ldKB)",
                 context, used / 1024);
    }
}
#endif /* DEBUG_VM */

/* Shared buffers for OP_CALL keyword processing and OP_APPLY argument flattening.
 * Moved out of cl_vm_eval to keep the per-recursion stack frame small.
 * Safe because these buffers are fully consumed before any recursive call. */
static CL_Obj vm_extra_args[256];
static CL_Obj vm_flat_args[64];

/* Last-dispatch diagnostic globals (readable from crash handler / debugger) */
volatile uint8_t dbg_last_op;
volatile uint32_t dbg_last_ip;
volatile int dbg_last_fp;
volatile uint8_t *dbg_last_code;

/* Circular trace buffer for crash diagnostics */
#define VM_TRACE_SIZE 64
static struct {
    uint8_t op;
    uint32_t ip;
    int fp;
    int sp;
    uint8_t *code;
} vm_trace[VM_TRACE_SIZE];
static int vm_trace_idx = 0;

void vm_trace_dump(void)
{
    int i, idx;
    fprintf(stderr, "=== VM trace (last %d ops) ===\n", VM_TRACE_SIZE);
    for (i = 0; i < VM_TRACE_SIZE; i++) {
        idx = (vm_trace_idx + i) % VM_TRACE_SIZE;
        if (vm_trace[idx].code == NULL && vm_trace[idx].op == 0) continue;
        fprintf(stderr, "  [%d] op=0x%02x ip=%u fp=%d sp=%d code=%p\n",
                i, vm_trace[idx].op, vm_trace[idx].ip,
                vm_trace[idx].fp, vm_trace[idx].sp,
                (void *)vm_trace[idx].code);
    }
}

/*
 * cl_vm_run — the shared dispatch loop.
 * Expects a frame to already be pushed on cl_vm.frames[].
 * Runs until cl_vm.fp drops to base_fp, then returns the result.
 * Both cl_vm_eval and cl_vm_apply use this.
 */
static CL_Obj cl_vm_run(int base_fp, int base_nlx)
{
    CL_Frame *frame = &cl_vm.frames[cl_vm.fp - 1];
    uint8_t *code = frame->code;
    CL_Obj *constants = frame->constants;
    uint32_t ip = frame->ip;

    for (;;) {
        uint8_t op;

        /* Record trace for crash diagnostics */
        vm_trace[vm_trace_idx].op = code ? code[ip] : 0;
        vm_trace[vm_trace_idx].ip = ip;
        vm_trace[vm_trace_idx].fp = cl_vm.fp;
        vm_trace[vm_trace_idx].sp = cl_vm.sp;
        vm_trace[vm_trace_idx].code = code;
        vm_trace_idx = (vm_trace_idx + 1) % VM_TRACE_SIZE;

        /* Validate code pointer before each dispatch */
        if (!code) {
            const char *fn = "?";
            CL_Bytecode *fbc = NULL;
            if (frame && CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
            } else if (frame && CL_BYTECODE_P(frame->bytecode)) {
                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
            }
            if (fbc && fbc->name != CL_NIL && CL_SYMBOL_P(fbc->name))
                fn = cl_symbol_name(fbc->name);
            fprintf(stderr, "[VM] NULL code pointer in dispatch: ip=%u fn=%s fp=%d sp=%d\n",
                    ip, fn, cl_vm.fp, cl_vm.sp);
            cl_capture_backtrace();
            fprintf(stderr, "%s", cl_backtrace_buf);
            cl_error(CL_ERR_GENERAL, "NULL code pointer in VM dispatch (fn=%s)", fn);
        }

        op = code[ip++];

        /* Trap opcode 0 explicitly — prevents jump-table jump to NULL */
        if (__builtin_expect(op == 0x00, 0)) {
            const char *fn = "?";
            CL_Bytecode *fbc = NULL;
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                if (CL_BYTECODE_P(cc->bytecode))
                    fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
            } else if (CL_BYTECODE_P(frame->bytecode)) {
                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
            }
            if (fbc && fbc->name != CL_NIL && CL_SYMBOL_P(fbc->name))
                fn = cl_symbol_name(fbc->name);
            fprintf(stderr, "[VM] ZERO opcode at ip=%u fn=%s code=%p code_len=%u fp=%d sp=%d\n",
                    ip - 1, fn, (void *)code, fbc ? fbc->code_len : 0, cl_vm.fp, cl_vm.sp);
            if (fbc) {
                uint32_t j;
                fprintf(stderr, "[VM]   code bytes: ");
                for (j = 0; j < fbc->code_len && j < 32; j++)
                    fprintf(stderr, "%02x ", code[j]);
                fprintf(stderr, "\n");
                fprintf(stderr, "[VM]   bc->code bytes: ");
                for (j = 0; j < fbc->code_len && j < 32; j++)
                    fprintf(stderr, "%02x ", fbc->code[j]);
                fprintf(stderr, "\n");
                if (code != fbc->code)
                    fprintf(stderr, "[VM]   *** code ptrs DIFFER: frame->code=%p bc->code=%p ***\n",
                            (void *)code, (void *)fbc->code);
            }
            fflush(stderr);
            cl_capture_backtrace();
            fprintf(stderr, "%s", cl_backtrace_buf);
            vm_trace_dump();
            abort();
        }

        /* Debug: check watchpoint before instruction dispatch */
#ifdef CL_DEBUG_UWP
        if (dbg_watch_idx >= 0 && cl_vm.stack[dbg_watch_idx] != dbg_watch_orig) {
            DBG_CHECK_WATCH("pre-dispatch");
        }
#endif

        /* Save last dispatched opcode for crash diagnostics */
        dbg_last_op = op;
        dbg_last_ip = ip;
        dbg_last_fp = cl_vm.fp;
        dbg_last_code = code;
        if (op == 0x00) goto unknown_opcode;
        switch (op) {
        case 0x00:
            /* Invalid opcode 0 — explicitly handled to prevent computed-goto
             * jump table from branching to address 0 when reading stale bytecode */
            goto unknown_opcode;

        case OP_HALT: {
            CL_Obj result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
                            ? cl_vm_pop() : CL_NIL;
            /* Restore stack and NLX to before this run */
            cl_vm.sp = frame->bp;
            cl_vm.fp = base_fp;
            cl_nlx_top = base_nlx;
            return result;
        }

        case OP_CONST: {
            uint16_t idx = read_u16(code, &ip);
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_CONST with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_error(CL_ERR_GENERAL, "OP_CONST with NULL constants ptr");
            }
            cl_vm_push(constants[idx]);
            cl_mv_count = 1;
            break;
        }

        case OP_NIL:
            cl_vm_push(CL_NIL);
            cl_mv_count = 1;
            break;

        case OP_T:
            cl_vm_push(SYM_T);
            cl_mv_count = 1;
            break;

        case OP_LOAD: {
            uint8_t slot = code[ip++];
            cl_vm_push(cl_vm.stack[frame->bp + slot]);
            break;
        }

        case OP_STORE: {
            uint8_t slot = code[ip++];
            cl_vm.stack[frame->bp + slot] = cl_vm.stack[cl_vm.sp - 1];
            DBG_CHECK_WATCH("OP_STORE");
            break;
        }

        case OP_GLOAD: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->value == CL_UNBOUND)
                cl_error(CL_ERR_UNBOUND, "Unbound variable: %s",
                         cl_symbol_name(sym));
            cl_vm_push(s->value);
            cl_mv_count = 1;
            break;
        }

        case OP_GSTORE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->value = cl_vm.stack[cl_vm.sp - 1];
            break;
        }

        case OP_FLOAD: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym;
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_FLOAD with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_error(CL_ERR_GENERAL, "OP_FLOAD with NULL constants ptr");
            }
            sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->function != CL_UNBOUND) {
                cl_vm_push(s->function);
            } else if (s->value != CL_UNBOUND) {
                /* Fall back to value slot (for labels/flet value bindings) */
                cl_vm_push(s->value);
            } else {
                cl_error(CL_ERR_UNDEFINED, "Undefined function: %s",
                         cl_symbol_name(sym));
            }
            cl_mv_count = 1;
            break;
        }

        case OP_FSTORE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->function = cl_vm.stack[cl_vm.sp - 1];
            break;
        }

        case OP_MAKE_CELL: {
            CL_Obj val = cl_vm_pop();
            cl_vm_push(cl_make_cell(val));
            break;
        }

        case OP_CELL_REF: {
            CL_Obj cell_obj = cl_vm_pop();
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cl_vm_push(cell->value);
            break;
        }

        case OP_CELL_SET_LOCAL: {
            uint8_t slot = code[ip++];
            CL_Obj cell_obj = cl_vm.stack[frame->bp + slot];
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
            break;
        }

        case OP_CELL_SET_UPVAL: {
            uint8_t index = code[ip++];
            CL_Obj cell_obj;
            CL_Cell *cell;
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cell_obj = cl->upvalues[index];
            } else {
                break; /* shouldn't happen */
            }
            if (!CL_CELL_P(cell_obj)) {
                cl_error(CL_ERR_TYPE, "OP_CELL_SET_UPVAL: upvalue[%u] is not a cell (internal compiler error)", (unsigned)index);
            }
            cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
            break;
        }

        case OP_UPVAL: {
            /* Flat upvalue access: single index into closure's upvalues[] */
            uint8_t index = code[ip++];
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cl_vm_push(cl->upvalues[index]);
            } else {
                cl_vm_push(CL_NIL);
            }
            cl_mv_count = 1;
            break;
        }

        case OP_POP:
            cl_vm_pop();
            break;

        case OP_DUP:
            cl_vm_push(cl_vm.stack[cl_vm.sp - 1]);
            break;

        case OP_CONS: {
            CL_Obj cdr_val = cl_vm_pop();
            CL_Obj car_val = cl_vm_pop();
            cl_vm_push(cl_cons(car_val, cdr_val));
            cl_mv_count = 1;
            break;
        }

        case OP_CAR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_car(obj));
            cl_mv_count = 1;
            break;
        }

        case OP_CDR: {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_cdr(obj));
            cl_mv_count = 1;
            break;
        }

        case OP_ADD: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_add(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_SUB: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_sub(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_MUL: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_mul(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_DIV: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FLOATP(a) || CL_FLOATP(b))
                cl_vm_push(cl_float_div(a, b));
            else
                cl_vm_push(cl_ratio_div(a, b));
            cl_mv_count = 1;
            break;
        }

        case OP_EQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(a == b ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_NUMEQ: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) == 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_LT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) < 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_GT: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(cl_arith_compare(a, b) > 0 ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_LE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, "<=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) <= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_GE: {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (!CL_FIXNUM_P(a) || !CL_FIXNUM_P(b))
                cl_error(CL_ERR_TYPE, ">=: not a number");
            cl_vm_push(CL_FIXNUM_VAL(a) >= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_NOT: {
            CL_Obj a = cl_vm_pop();
            cl_vm_push(CL_NULL_P(a) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_JMP: {
            int16_t offset = read_i16(code, &ip);
            ip += offset;
            break;
        }

        case OP_JNIL: {
            int16_t offset = read_i16(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (CL_NULL_P(val)) ip += offset;
            break;
        }

        case OP_JTRUE: {
            int16_t offset = read_i16(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (!CL_NULL_P(val)) ip += offset;
            break;
        }

        case OP_LIST: {
            uint8_t n = code[ip++];
            CL_Obj list = CL_NIL;
            int i;
            /* Build list from stack (last element is on top) */
            for (i = 0; i < n; i++) {
                list = cl_cons(cl_vm_pop(), list);
            }
            cl_vm_push(list);
            cl_mv_count = 1;
            break;
        }

        case OP_CALL:
        case OP_TAILCALL: {
            uint8_t nargs = code[ip++];
            int is_tail = (op == OP_TAILCALL);
            CL_Obj *arg_base;
            CL_Obj func_obj;

            /* Stack: [func] [arg0] [arg1] ... [argN-1]
               func is below the args */
            arg_base = &cl_vm.stack[cl_vm.sp - nargs];
            func_obj = cl_vm.stack[cl_vm.sp - nargs - 1];

            /* Resolve symbol to its function binding (for funcall/apply) */
            if (CL_SYMBOL_P(func_obj)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(func_obj);
                func_obj = s->function;
                if (CL_NULL_P(func_obj) || func_obj == CL_UNBOUND)
                    cl_error(CL_ERR_TYPE, "Not a function: symbol %s",
                             cl_symbol_name(cl_vm.stack[cl_vm.sp - nargs - 1]));
                cl_vm.stack[cl_vm.sp - nargs - 1] = func_obj;
            }

            if (CL_FUNCTION_P(func_obj)) {
                /* Built-in C function */
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(func_obj);
                int traced = is_func_traced(func_obj);
                CL_Obj result;
                if (traced) {
                    trace_print_entry(f->name, arg_base, nargs);
                    cl_trace_depth++;
                }
                result = call_builtin(f, arg_base, nargs);
                if (traced) {
                    cl_trace_depth--;
                    trace_print_exit(f->name, result);
                }
                cl_vm.sp -= (nargs + 1);
                cl_vm_push(result);
            } else if (CL_BYTECODE_P(func_obj) || CL_CLOSURE_P(func_obj)) {
                CL_Bytecode *callee_bc;
                CL_Frame *new_frame;
                int callee_arity, has_rest, n_opt, has_key, i;
                int min_args, max_args;

                if (CL_CLOSURE_P(func_obj)) {
                    CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
                    if (!CL_BYTECODE_P(cl->bytecode)) {
                        fprintf(stderr, "[VM] CORRUPT: closure 0x%x bytecode field 0x%x type=%u\n",
                                (unsigned)func_obj, (unsigned)cl->bytecode,
                                CL_HEAP_P(cl->bytecode) ?
                                (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(cl->bytecode)) : 999);
                        cl_capture_backtrace();
                        fprintf(stderr, "%s", cl_backtrace_buf);
                        cl_error(CL_ERR_TYPE, "Corrupted closure: bytecode field is type %u",
                                 CL_HEAP_P(cl->bytecode) ?
                                 (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(cl->bytecode)) : 999);
                    }
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
                } else {
                    callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(func_obj);
                }

                /* Validate bytecode has valid code pointer */
                if (!callee_bc->code || callee_bc->code_len == 0) {
                    CL_Obj fname = callee_bc->name;
                    cl_error(CL_ERR_TYPE, "Bytecode %s has NULL/empty code (code=%p len=%u)",
                             (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                 ? cl_symbol_name(fname) : "<anon>",
                             (void *)callee_bc->code, (unsigned)callee_bc->code_len);
                }

                callee_arity = callee_bc->arity & 0x7FFF;
                has_rest = callee_bc->arity & 0x8000;
                n_opt = callee_bc->n_optional;
                has_key = callee_bc->flags & 1;

                /* Arity check */
                min_args = callee_arity;
                max_args = (has_rest || has_key)
                           ? 255 : callee_arity + n_opt;

                if (nargs < min_args) {
                    cl_error(CL_ERR_ARGS, "Too few arguments: expected %s%d, got %d",
                             (n_opt || has_rest || has_key) ? "at least " : "",
                             min_args, nargs);
                }
                if (nargs > max_args) {
                    CL_Obj fname = callee_bc->name;
                    const char *fn = (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                     ? cl_symbol_name(fname) : "<lambda>";
                    cl_error(CL_ERR_ARGS, "Too many arguments to %s: expected %s%d, got %d",
                             fn, n_opt ? "at most " : "",
                             max_args, nargs);
                }

                /* GC-protect func_obj: it gets removed from the VM stack
                 * before &rest processing which calls cl_cons (allocating).
                 * Without this, GC can sweep the closure/bytecode. */
                if (has_rest || has_key)
                    CL_GC_PROTECT(func_obj);

                if (is_tail && cl_vm.fp > base_fp + 1) {
                    CL_Obj *src = arg_base;
                    int n_extra = 0;
                    int n_positional = callee_arity + n_opt;

                    /* Trace: adjust depth for tail call transition */
                    if (cl_trace_count > 0) {
                        CL_Obj cur_name = get_func_name(frame->bytecode);
                        if (!CL_NULL_P(cur_name) && CL_SYMBOL_P(cur_name) &&
                            (((CL_Symbol *)CL_OBJ_TO_PTR(cur_name))->flags & CL_SYM_TRACED))
                            cl_trace_depth--;
                        if (is_func_traced(func_obj)) {
                            trace_print_entry(callee_bc->name, arg_base, nargs);
                            cl_trace_depth++;
                        }
                    }

                    cl_vm.sp = frame->bp;

                    /* Push positional args */
                    for (i = 0; i < nargs && i < n_positional; i++) {
                        cl_vm_push(src[i]);
                        DBG_CHECK_WATCH("TAILCALL-PUSH-ARG");
                    }

                    /* Fill missing optionals with NIL */
                    while (cl_vm.sp < (int)(frame->bp + n_positional))
                        cl_vm_push(CL_NIL);

                    /* Save extra args for keyword processing */
                    if (has_key || has_rest) {
                        for (i = n_positional; i < nargs; i++) {
                            if (n_extra < 256) vm_extra_args[n_extra++] = src[i];
                        }
                    }

                    /* Handle &rest */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(vm_extra_args[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(frame->bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* Check for :allow-other-keys t in caller args */
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (vm_extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(vm_extra_args[ki + 1])) {
                                    allow = 1;
                                    break;
                                }
                            }
                        }
                        for (ki = 0; ki + 1 < n_extra; ki += 2) {
                            CL_Obj key = vm_extra_args[ki];
                            CL_Obj val = vm_extra_args[ki + 1];
                            int j, found = 0;
                            for (j = 0; j < callee_bc->n_keys; j++) {
                                if (key == callee_bc->key_syms[j]) {
                                    cl_vm.stack[frame->bp + callee_bc->key_slots[j]] = val;
                                    if (callee_bc->key_suppliedp_slots)
                                        cl_vm.stack[frame->bp + callee_bc->key_suppliedp_slots[j]] = CL_T;
                                    DBG_CHECK_WATCH("TAILCALL-KEY-SUPPLIEDP");
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                                         cl_symbol_name(key));
                        }
                    }

                    if (has_rest || has_key)
                        CL_GC_UNPROTECT(1);

                    frame->bytecode = func_obj;
                    frame->code = callee_bc->code;
                    frame->constants = callee_bc->constants;
                    frame->ip = 0;
                    frame->n_locals = callee_bc->n_locals;
                    frame->nargs = nargs;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;

                    if (!constants && callee_bc->n_constants > 0) {
                        CL_Obj fname = callee_bc->name;
                        fprintf(stderr, "[VM] BUG: tailcall '%s' has %d constants but NULL constants ptr\n",
                                (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                callee_bc->n_constants);
                        cl_capture_backtrace();
                        fprintf(stderr, "%s", cl_backtrace_buf);
                        cl_error(CL_ERR_GENERAL, "Bytecode %s has NULL constants with n_constants=%d",
                                 (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                 callee_bc->n_constants);
                    }
                } else {
                    /* Normal call: push new frame */
                    uint32_t new_bp;
                    int n_extra = 0;
                    int n_positional = callee_arity + n_opt;

                    /* Trace: entry for normal call */
                    if (is_func_traced(func_obj))  {
                        trace_print_entry(callee_bc->name, arg_base, nargs);
                        cl_trace_depth++;
                    }

                    /* Remove func_obj from under args; shift args down */
                    {
                        int j;
                        for (j = 0; j < nargs; j++)
                            cl_vm.stack[cl_vm.sp - nargs - 1 + j] =
                                cl_vm.stack[cl_vm.sp - nargs + j];
                        cl_vm.sp--;
                    }

                    new_bp = cl_vm.sp - nargs;

                    /* Save extra args for keyword processing */
                    if (has_key || has_rest) {
                        for (i = n_positional; i < nargs; i++) {
                            if (n_extra < 256)
                                vm_extra_args[n_extra++] = cl_vm.stack[new_bp + i];
                        }
                    }

                    /* Truncate stack to positional args */
                    if (nargs > n_positional)
                        cl_vm.sp = new_bp + n_positional;

                    /* Fill missing optionals with NIL */
                    while (cl_vm.sp < (int)(new_bp + n_positional))
                        cl_vm_push(CL_NIL);

                    /* Handle &rest parameter */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(vm_extra_args[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* Check for :allow-other-keys t in caller args */
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (vm_extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(vm_extra_args[ki + 1])) {
                                    allow = 1;
                                    break;
                                }
                            }
                        }
                        for (ki = 0; ki + 1 < n_extra; ki += 2) {
                            CL_Obj key = vm_extra_args[ki];
                            CL_Obj val = vm_extra_args[ki + 1];
                            int j, found = 0;
                            for (j = 0; j < callee_bc->n_keys; j++) {
                                if (key == callee_bc->key_syms[j]) {
                                    cl_vm.stack[new_bp + callee_bc->key_slots[j]] = val;
                                    if (callee_bc->key_suppliedp_slots) {
                                        cl_vm.stack[new_bp + callee_bc->key_suppliedp_slots[j]] = CL_T;
                                        DBG_CHECK_WATCH("CALL-KEY-SUPPLIEDP");
                                    }
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                                cl_error(CL_ERR_ARGS, "Unknown keyword argument: %s",
                                         cl_symbol_name(key));
                        }
                    }

                    if (has_rest || has_key)
                        CL_GC_UNPROTECT(1);

                    /* Save current frame state */
                    frame->ip = ip;

                    if (cl_vm.fp >= cl_vm.frame_size)
                        cl_error(CL_ERR_OVERFLOW, "Call stack overflow");

                    new_frame = &cl_vm.frames[cl_vm.fp++];
                    new_frame->bytecode = func_obj;
                    new_frame->code = callee_bc->code;
                    new_frame->constants = callee_bc->constants;
                    new_frame->ip = 0;
                    new_frame->bp = new_bp;
                    new_frame->n_locals = callee_bc->n_locals;
                    new_frame->nargs = nargs;

                    frame = new_frame;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;

                    /* Validate: bytecode with constants must have non-NULL constants ptr */
                    if (!constants && callee_bc->n_constants > 0) {
                        CL_Obj fname = callee_bc->name;
                        fprintf(stderr, "[VM] BUG: bytecode '%s' has %d constants but NULL constants ptr (code=%p)\n",
                                (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                callee_bc->n_constants, (void *)callee_bc->code);
                        cl_capture_backtrace();
                        fprintf(stderr, "%s", cl_backtrace_buf);
                        cl_error(CL_ERR_GENERAL, "Bytecode %s has NULL constants with n_constants=%d",
                                 (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                 callee_bc->n_constants);
                    }
                }
            } else {
                /* Print what we got for debugging */
                char buf[128];
                if (CL_NULL_P(func_obj)) {
                    cl_error(CL_ERR_TYPE, "Not a function: NIL");
                } else if (CL_SYMBOL_P(func_obj)) {
                    snprintf(buf, sizeof(buf), "Not a function: symbol %s",
                             cl_symbol_name(func_obj));
                    cl_error(CL_ERR_TYPE, buf);
                } else if (CL_HEAP_P(func_obj)) {
                    snprintf(buf, sizeof(buf), "Not a function: heap object type %u",
                             (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(func_obj)));
                    cl_error(CL_ERR_TYPE, buf);
                } else {
                    snprintf(buf, sizeof(buf), "Not a function: raw value 0x%08X",
                             (unsigned)func_obj);
                    cl_error(CL_ERR_TYPE, buf);
                }
            }
            break;
        }

        case OP_RET: {
            CL_Obj result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
                            ? cl_vm_pop() : CL_NIL;

            /* Trace: print return value */
            if (cl_trace_count > 0) {
                CL_Obj ret_name = get_func_name(frame->bytecode);
                if (!CL_NULL_P(ret_name) && CL_SYMBOL_P(ret_name) &&
                    (((CL_Symbol *)CL_OBJ_TO_PTR(ret_name))->flags & CL_SYM_TRACED)) {
                    cl_trace_depth--;
                    trace_print_exit(ret_name, result);
                }
            }

            /* Pop frame */
            cl_vm.sp = frame->bp;
            cl_vm.fp--;

            if (cl_vm.fp <= base_fp) {
                /* Returned from top-level call */
                return result;
            }

            /* Restore caller frame */
            frame = &cl_vm.frames[cl_vm.fp - 1];
            code = frame->code;
            constants = frame->constants;
            ip = frame->ip;

            /* Safety check: validate restored ip is within bytecode bounds */
            if (code && !CL_NULL_P(frame->bytecode)) {
                CL_Bytecode *ret_bc = NULL;
                if (CL_CLOSURE_P(frame->bytecode)) {
                    CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                    ret_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
                } else if (CL_BYTECODE_P(frame->bytecode)) {
                    ret_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
                }
                if (ret_bc && ip > ret_bc->code_len) {
                    fprintf(stderr, "[VM] BUG: OP_RET restored ip=%u but code_len=%u\n",
                            ip, ret_bc->code_len);
                    fprintf(stderr, "[VM]   frame fp=%d bytecode=0x%08x code=%p bc->code=%p\n",
                            cl_vm.fp, (unsigned)frame->bytecode,
                            (void *)code, (void *)ret_bc->code);
                    if (code != ret_bc->code)
                        fprintf(stderr, "[VM]   *** frame->code DIFFERS from bytecode->code! ***\n");
                    if (CL_SYMBOL_P(ret_bc->name))
                        fprintf(stderr, "[VM]   name: %s src=%s:%u\n",
                                cl_symbol_name(ret_bc->name),
                                ret_bc->source_file ? ret_bc->source_file : "?",
                                ret_bc->source_line);
                    vm_trace_dump();
                    cl_capture_backtrace();
                    fprintf(stderr, "%s", cl_backtrace_buf);
                    cl_error(CL_ERR_GENERAL, "OP_RET: restored ip=%u exceeds code_len=%u",
                             ip, ret_bc->code_len);
                }
            }

            /* Safety check: if code is non-null and next instruction uses constants, constants must be valid */
            if (code && !constants) {
                uint8_t next_op = code[ip];
                if (next_op == OP_CONST || next_op == OP_FLOAD || next_op == OP_DYNBIND ||
                    next_op == OP_FSTORE || next_op == OP_CLOSURE || next_op == OP_DEFMACRO ||
                    next_op == OP_DEFTYPE || next_op == OP_DEFSETF || next_op == OP_DEFVAR ||
                    next_op == OP_ASSERT_TYPE || next_op == OP_BLOCK_PUSH ||
                    next_op == OP_TAGBODY_PUSH || next_op == OP_BLOCK_RETURN ||
                    next_op == OP_TAGBODY_GO || next_op == OP_HANDLER_PUSH ||
                    next_op == OP_RESTART_PUSH) {
                    CL_Bytecode *fbc = NULL;
                    if (CL_CLOSURE_P(frame->bytecode)) {
                        CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                        fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
                    } else if (CL_BYTECODE_P(frame->bytecode)) {
                        fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
                    }
                    fprintf(stderr, "[VM] BUG: RET restored frame with NULL constants but next op=0x%02x needs them\n", next_op);
                    fprintf(stderr, "[VM]   frame fp=%d ip=%u bytecode=0x%08x\n",
                            cl_vm.fp, ip, frame->bytecode);
                    if (fbc) {
                        fprintf(stderr, "[VM]   fn=%s src=%s:%u n_constants=%d\n",
                                (fbc->name != CL_NIL && CL_SYMBOL_P(fbc->name))
                                    ? cl_symbol_name(fbc->name) : "<anon>",
                                fbc->source_file ? fbc->source_file : "?",
                                fbc->source_line, fbc->n_constants);
                    }
                    cl_error(CL_ERR_GENERAL, "NULL constants in restored frame");
                }
            }

            cl_vm_push(result);
            break;
        }

        case OP_CLOSURE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj tmpl = constants[idx];
            CL_Bytecode *tmpl_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(tmpl);
            int n_upvals = tmpl_bc->n_upvalues;
            CL_Closure *cl = (CL_Closure *)cl_alloc(TYPE_CLOSURE,
                sizeof(CL_Closure) + n_upvals * sizeof(CL_Obj));
            if (cl) {
                int i;
                cl->bytecode = tmpl;
                /* Read capture descriptors and populate upvalues */
                for (i = 0; i < n_upvals; i++) {
                    uint8_t is_local = code[ip++];
                    uint8_t cap_idx = code[ip++];
                    if (is_local) {
                        /* Capture from current frame's local slot */
                        cl->upvalues[i] = cl_vm.stack[frame->bp + cap_idx];
                    } else {
                        /* Capture from current closure's upvalue slot */
                        if (CL_CLOSURE_P(frame->bytecode)) {
                            CL_Closure *parent_cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                            cl->upvalues[i] = parent_cl->upvalues[cap_idx];
                        } else {
                            cl->upvalues[i] = CL_NIL;
                        }
                    }
                }
                cl_vm_push(CL_PTR_TO_OBJ(cl));
            } else {
                /* Skip capture descriptors even on alloc failure */
                ip += n_upvals * 2;
                cl_vm_push(CL_NIL);
            }
            cl_mv_count = 1;
            break;
        }

        case OP_DEFMACRO: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_macro(name, expander);
            cl_vm_push(name);
            break;
        }

        case OP_DEFTYPE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_type(name, expander);
            cl_vm_push(name);
            break;
        }

        case OP_DEFSETF: {
            extern CL_Obj setf_table;
            uint16_t acc_idx = read_u16(code, &ip);
            uint16_t upd_idx = read_u16(code, &ip);
            CL_Obj accessor = constants[acc_idx];
            CL_Obj updater  = constants[upd_idx];
            setf_table = cl_cons(cl_cons(accessor, updater), setf_table);
            cl_vm_push(accessor);
            break;
        }

        case OP_DEFVAR: {
            uint16_t sym_idx = read_u16(code, &ip);
            CL_Obj sym_obj = constants[sym_idx];
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(sym_obj);
            CL_Obj val = cl_vm_pop();
            sym->flags |= CL_SYM_SPECIAL;
            if (sym->value == CL_UNBOUND)
                sym->value = val;
            break;
        }

        case OP_HANDLER_PUSH: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj type_sym = constants[idx];
            CL_Obj handler = cl_vm_pop();

            if (cl_handler_top >= CL_MAX_HANDLER_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");

            cl_handler_stack[cl_handler_top].type_name = type_sym;
            cl_handler_stack[cl_handler_top].handler = handler;
            cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
            cl_handler_top++;
            break;
        }

        case OP_HANDLER_POP: {
            uint8_t count = code[ip++];
            cl_handler_top -= count;
            if (cl_handler_top < 0) cl_handler_top = 0;
            break;
        }

        case OP_RESTART_PUSH: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name_sym = constants[idx];
            CL_Obj tag = cl_vm_pop();
            CL_Obj handler = cl_vm_pop();

            if (cl_restart_top >= CL_MAX_RESTART_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Restart stack overflow");

            cl_restart_stack[cl_restart_top].name = name_sym;
            cl_restart_stack[cl_restart_top].handler = handler;
            cl_restart_stack[cl_restart_top].tag = tag;
            cl_restart_top++;
            break;
        }

        case OP_RESTART_POP: {
            uint8_t count = code[ip++];
            cl_restart_top -= count;
            if (cl_restart_top < 0) cl_restart_top = 0;
            break;
        }

        case OP_ASSERT_TYPE: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj type_spec = constants[idx];
            CL_Obj val = cl_vm.stack[cl_vm.sp - 1]; /* peek TOS */
            if (!cl_typep(val, type_spec)) {
                /* Build type-error condition with :datum and :expected-type */
                CL_Obj slots = CL_NIL;
                CL_Obj cond;
                CL_GC_PROTECT(slots);
                slots = cl_cons(cl_cons(KW_EXPECTED_TYPE, type_spec), slots);
                slots = cl_cons(cl_cons(KW_DATUM, val), slots);
                CL_GC_UNPROTECT(1);
                cond = cl_make_condition(SYM_TYPE_ERROR, slots, CL_NIL);
                cl_signal_condition(cond);
                /* If no handler transferred control, fall to C error */
                {
                    char buf[128];
                    char tbuf[64];
                    cl_prin1_to_string(val, buf, sizeof(buf));
                    cl_prin1_to_string(type_spec, tbuf, sizeof(tbuf));
                    cl_error(CL_ERR_TYPE, "THE: value %s is not of type %s", buf, tbuf);
                }
            }
            break;
        }

        case OP_BLOCK_PUSH: {
            /* Set up NLX block frame for return-from support */
            uint16_t tag_idx = read_u16(code, &ip);
            int16_t block_offset = read_i16(code, &ip);
            CL_Obj block_tag = constants[tag_idx];
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_BLOCK;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = block_tag;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = block_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->bytecode = frame->bytecode;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;
            nlx->gc_root_mark = gc_root_count;
            nlx->compiler_mark = cl_compiler_mark();

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: block body executes */
                cl_nlx_top++;
            } else {
                /* longjmp from return-from: restore state */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                gc_root_count = nlx->gc_root_mark;
                cl_compiler_restore_to(nlx->compiler_mark);
                {
                    CL_Obj block_result = nlx->result;
                    cl_vm.sp = nlx->vm_sp;
                    cl_vm.fp = nlx->vm_fp;
                    frame = &cl_vm.frames[cl_vm.fp - 1];
                    code = nlx->code;
                    constants = nlx->constants;
                    base_fp = nlx->base_fp;
                    ip = nlx->catch_ip + nlx->offset;
                    /* Sync frame with NLX-restored state — a tail call
                     * between BLOCK_PUSH and longjmp may have changed
                     * frame->code/constants/bytecode to the tail target */
                    frame->code = code;
                    frame->constants = constants;
                    frame->bytecode = nlx->bytecode;
                    cl_mv_count = 1;
                    cl_vm_push(block_result);
                }
            }
            break;
        }

        case OP_BLOCK_POP: {
            /* Normal exit from block body — pop NLX frame.
             * Search for matching BLOCK, as tail calls in called
             * functions may have leaked BLOCK frames above. */
            {
                int bi;
                for (bi = cl_nlx_top - 1; bi >= 0; bi--) {
                    if (cl_nlx_stack[bi].type == CL_NLX_BLOCK) {
                        cl_nlx_top = bi;
                        break;
                    }
                }
                if (bi < 0 && cl_nlx_top > 0)
                    cl_nlx_top--;
            }
            break;
        }

        case OP_BLOCK_RETURN: {
            /* return-from: pop value, find matching block on NLX stack, longjmp */
            uint16_t tag_idx = read_u16(code, &ip);
            CL_Obj block_tag = constants[tag_idx];
            CL_Obj value = cl_vm_pop();
            int i;

            for (i = cl_nlx_top - 1; i >= 0; i--) {
                if (cl_nlx_stack[i].type == CL_NLX_BLOCK &&
                    cl_nlx_stack[i].tag == block_tag) {
                    int j;
                    /* Check for interposing UWPROT frames (skip stale ones) */
                    for (j = cl_nlx_top - 1; j > i; j--) {
                        if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                            !nlx_frame_is_stale(&cl_nlx_stack[j])) {
                            cl_pending_throw = 1;
                            cl_pending_tag = block_tag;
                            cl_pending_value = value;
                            cl_nlx_top = j;
                            longjmp(cl_nlx_stack[j].buf, 1);
                        }
                    }
                    /* No interposing UWPROT — longjmp directly to block */
                    cl_nlx_stack[i].result = value;
                    cl_nlx_top = i;
                    longjmp(cl_nlx_stack[i].buf, 1);
                }
            }
            cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
                     CL_NULL_P(block_tag) ? "NIL" : cl_symbol_name(block_tag));
            break;
        }

        case OP_TAGBODY_PUSH: {
            /* Set up NLX tagbody frame for cross-closure GO support */
            uint16_t id_idx = read_u16(code, &ip);
            int16_t tb_offset = read_i16(code, &ip);
            CL_Obj tagbody_id = constants[id_idx];
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_TAGBODY;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = tagbody_id;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = tb_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->bytecode = frame->bytecode;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;
            nlx->gc_root_mark = gc_root_count;
            nlx->compiler_mark = cl_compiler_mark();

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: tagbody body executes */
                cl_nlx_top++;
            } else {
                /* longjmp from cross-closure GO: restore state */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                gc_root_count = nlx->gc_root_mark;
                cl_compiler_restore_to(nlx->compiler_mark);
                {
                    CL_Obj tag_index = nlx->result;
                    cl_vm.sp = nlx->vm_sp;
                    cl_vm.fp = nlx->vm_fp;
                    frame = &cl_vm.frames[cl_vm.fp - 1];
                    code = nlx->code;
                    constants = nlx->constants;
                    base_fp = nlx->base_fp;
                    ip = nlx->catch_ip + nlx->offset;
                    /* Sync frame with NLX-restored state */
                    frame->code = code;
                    frame->constants = constants;
                    frame->bytecode = nlx->bytecode;
                    cl_mv_count = 1;
                    /* Re-arm: keep NLX frame active for repeated GO */
                    cl_nlx_top++;
                    cl_vm_push(tag_index);
                }
            }
            break;
        }

        case OP_TAGBODY_POP: {
            /* Normal exit from tagbody — pop NLX frame.
             * Search for matching TAGBODY frame. */
            {
                int bi;
                for (bi = cl_nlx_top - 1; bi >= 0; bi--) {
                    if (cl_nlx_stack[bi].type == CL_NLX_TAGBODY) {
                        cl_nlx_top = bi;
                        break;
                    }
                }
                if (bi < 0 && cl_nlx_top > 0)
                    cl_nlx_top--;
            }
            break;
        }

        case OP_TAGBODY_GO: {
            /* Cross-closure GO: pop tag index, find matching tagbody, longjmp */
            uint16_t id_idx = read_u16(code, &ip);
            CL_Obj tagbody_id = constants[id_idx];
            CL_Obj tag_index = cl_vm_pop();
            int i;

            for (i = cl_nlx_top - 1; i >= 0; i--) {
                if (cl_nlx_stack[i].type == CL_NLX_TAGBODY &&
                    cl_nlx_stack[i].tag == tagbody_id) {
                    int j;
                    /* Check for interposing UWPROT frames (skip stale ones) */
                    for (j = cl_nlx_top - 1; j > i; j--) {
                        if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                            !nlx_frame_is_stale(&cl_nlx_stack[j])) {
                            cl_pending_throw = 1;
                            cl_pending_tag = tagbody_id;
                            cl_pending_value = tag_index;
                            cl_nlx_top = j;
                            longjmp(cl_nlx_stack[j].buf, 1);
                        }
                    }
                    /* No interposing UWPROT — longjmp directly to tagbody */
                    cl_nlx_stack[i].result = tag_index;
                    cl_nlx_top = i;
                    longjmp(cl_nlx_stack[i].buf, 1);
                }
            }
            cl_error(CL_ERR_GENERAL, "GO: tagbody frame not found");
            break;
        }

        case OP_PROGV_BIND: {
            /* Pop values-list, pop symbols-list, push dyn_mark, bind all */
            CL_Obj values_list = cl_vm_pop();
            CL_Obj symbols_list = cl_vm_pop();
            int mark = cl_dyn_top;

            /* Push saved dyn_mark as fixnum */
            cl_vm_push(CL_MAKE_FIXNUM(mark));

            /* Iterate symbols, pair with values */
            while (!CL_NULL_P(symbols_list)) {
                CL_Obj sym_obj = cl_car(symbols_list);
                CL_Obj val;
                CL_Symbol *s;

                if (!CL_SYMBOL_P(sym_obj))
                    cl_error(CL_ERR_TYPE, "PROGV: expected symbol, got non-symbol");

                val = !CL_NULL_P(values_list) ? cl_car(values_list) : CL_UNBOUND;

                if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
                    cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");

                s = (CL_Symbol *)CL_OBJ_TO_PTR(sym_obj);
                cl_dyn_stack[cl_dyn_top].symbol = sym_obj;
                cl_dyn_stack[cl_dyn_top].old_value = s->value;
                cl_dyn_top++;
                s->value = val;

                symbols_list = cl_cdr(symbols_list);
                if (!CL_NULL_P(values_list))
                    values_list = cl_cdr(values_list);
            }
            break;
        }

        case OP_PROGV_UNBIND: {
            /* Pop body result, pop dyn_mark, restore bindings, push result */
            CL_Obj result = cl_vm_pop();
            CL_Obj mark_obj = cl_vm_pop();
            int mark = CL_FIXNUM_VAL(mark_obj);
            cl_dynbind_restore_to(mark);
            cl_vm_push(result);
            break;
        }

        case OP_ARGC: {
            cl_vm_push(CL_MAKE_FIXNUM(frame->nargs));
            cl_mv_count = 1;
            break;
        }

        case OP_APPLY: {
            /* (apply func arglist) — inline dispatch to avoid C stack nesting */
            CL_Obj arglist = cl_vm_pop();
            CL_Obj apply_func = cl_vm_pop();
            int nflat = 0;
            int ai;

            /* Flatten arglist into static buffer */
            {
                CL_Obj a = arglist;
                while (!CL_NULL_P(a) && nflat < 64) {
                    vm_flat_args[nflat++] = cl_car(a);
                    a = cl_cdr(a);
                }
            }

            /* Resolve symbol to its function binding */
            if (CL_SYMBOL_P(apply_func)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(apply_func);
                apply_func = s->function;
                if (CL_NULL_P(apply_func) || apply_func == CL_UNBOUND)
                    cl_error(CL_ERR_TYPE, "APPLY: symbol has no function binding");
            }

            if (CL_FUNCTION_P(apply_func)) {
                CL_Function *f = (CL_Function *)CL_OBJ_TO_PTR(apply_func);
                int traced = is_func_traced(apply_func);
                CL_Obj result;
                if (traced) {
                    trace_print_entry(f->name, vm_flat_args, nflat);
                    cl_trace_depth++;
                }
                result = call_builtin(f, vm_flat_args, nflat);
                if (traced) {
                    cl_trace_depth--;
                    trace_print_exit(f->name, result);
                }
#ifdef DEBUG_COMPILER
                /* Validate code pointer after builtin returns */
                if (!code) {
                    fprintf(stderr, "[VM] BUG: OP_APPLY builtin returned but code=NULL! fn=%s ip=%u fp=%d\n",
                            CL_SYMBOL_P(f->name) ? cl_symbol_name(f->name) : "?",
                            ip, cl_vm.fp);
                    fflush(stderr);
                } else {
                    /* Check if next opcode is valid */
                    uint8_t next_op = code[ip];
                    if (next_op == 0x00) {
                        CL_Bytecode *fbc = NULL;
                        if (CL_CLOSURE_P(frame->bytecode)) {
                            CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                            fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
                        } else if (CL_BYTECODE_P(frame->bytecode)) {
                            fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
                        }
                        fprintf(stderr, "[VM] BUG: OP_APPLY builtin returned, next op=0x00 at code[%u]! "
                                "builtin=%s caller=%s code=%p frame->code=%p code_len=%u fp=%d\n",
                                ip,
                                CL_SYMBOL_P(f->name) ? cl_symbol_name(f->name) : "?",
                                (fbc && CL_SYMBOL_P(fbc->name)) ? cl_symbol_name(fbc->name) : "<anon>",
                                (void *)code, (void *)frame->code,
                                fbc ? fbc->code_len : 0, cl_vm.fp);
                        /* Dump bytecodes */
                        if (fbc) {
                            uint32_t j;
                            fprintf(stderr, "[VM]   bytecodes: ");
                            for (j = 0; j < fbc->code_len && j < 32; j++)
                                fprintf(stderr, "%02x ", fbc->code[j]);
                            fprintf(stderr, "\n");
                            fprintf(stderr, "[VM]   code ptr: ");
                            for (j = 0; j < fbc->code_len && j < 32; j++)
                                fprintf(stderr, "%02x ", code[j]);
                            fprintf(stderr, "\n");
                        }
                        fflush(stderr);
                    }
                }
#endif
                cl_vm_push(result);
            } else if (CL_BYTECODE_P(apply_func) || CL_CLOSURE_P(apply_func)) {
                /* Push func + flattened args onto VM stack and dispatch
                 * inline like OP_CALL — avoids cl_vm_apply C stack nesting */
                cl_vm_push(apply_func);
                for (ai = 0; ai < nflat; ai++)
                    cl_vm_push(vm_flat_args[ai]);

                /* Now set up the call frame inline (same as OP_CALL non-tail path) */
                {
                    uint8_t call_nargs = (uint8_t)nflat;
                    CL_Obj call_func = cl_vm.stack[cl_vm.sp - call_nargs - 1];
                    CL_Bytecode *callee_bc;
                    CL_Frame *new_frame;
                    int callee_arity, has_rest, n_opt, has_key;
                    int min_args, max_args, new_bp, n_extra = 0;
                    int n_positional;

                    if (CL_CLOSURE_P(call_func)) {
                        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(call_func);
                        if (!CL_BYTECODE_P(cl->bytecode)) {
                            fprintf(stderr, "[VM] CORRUPT: APPLY closure 0x%x bytecode field 0x%x type=%u\n",
                                    (unsigned)call_func, (unsigned)cl->bytecode,
                                    CL_HEAP_P(cl->bytecode) ?
                                    (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(cl->bytecode)) : 999);
                            cl_capture_backtrace();
                            fprintf(stderr, "%s", cl_backtrace_buf);
                            cl_error(CL_ERR_TYPE, "APPLY: corrupted closure (bytecode type %u)",
                                     CL_HEAP_P(cl->bytecode) ?
                                     (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(cl->bytecode)) : 999);
                        }
                        callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
                    } else {
                        callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(call_func);
                    }

                    /* Validate bytecode has valid code pointer */
                    if (!callee_bc->code || callee_bc->code_len == 0) {
                        CL_Obj fname = callee_bc->name;
                        cl_error(CL_ERR_TYPE, "APPLY: bytecode %s has NULL/empty code (code=%p len=%u)",
                                 (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                     ? cl_symbol_name(fname) : "<anon>",
                                 (void *)callee_bc->code, (unsigned)callee_bc->code_len);
                    }

                    callee_arity = callee_bc->arity & 0x7FFF;
                    has_rest = callee_bc->arity & 0x8000;
                    n_opt = callee_bc->n_optional;
                    has_key = callee_bc->flags & 1;
                    min_args = callee_arity;
                    max_args = (has_rest || has_key) ? 255 : callee_arity + n_opt;
                    n_positional = callee_arity + n_opt;

                    if (call_nargs < min_args)
                        cl_error(CL_ERR_ARGS, "APPLY: too few arguments (got %d, need %d)",
                                 call_nargs, min_args);
                    if (call_nargs > max_args)
                        cl_error(CL_ERR_ARGS, "APPLY: too many arguments (got %d, max %d)",
                                 call_nargs, max_args);

                    if (has_rest || has_key)
                        CL_GC_PROTECT(call_func);

                    /* Remove func slot, shift args down */
                    {
                        int j;
                        for (j = 0; j < call_nargs; j++)
                            cl_vm.stack[cl_vm.sp - call_nargs - 1 + j] =
                                cl_vm.stack[cl_vm.sp - call_nargs + j];
                        cl_vm.sp--;
                    }

                    new_bp = cl_vm.sp - call_nargs;

                    /* Save extra args for &rest/&key */
                    if (has_key || has_rest) {
                        for (ai = n_positional; ai < call_nargs; ai++) {
                            if (n_extra < 256)
                                vm_extra_args[n_extra++] = cl_vm.stack[new_bp + ai];
                        }
                    }

                    /* Truncate to positional */
                    if (call_nargs > n_positional)
                        cl_vm.sp = new_bp + n_positional;

                    /* Fill missing optionals */
                    while (cl_vm.sp < (int)(new_bp + n_positional))
                        cl_vm_push(CL_NIL);

                    /* &rest */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(vm_extra_args[j], rest);
                        cl_vm_push(rest);
                    }

                    /* Fill remaining locals */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (vm_extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(vm_extra_args[ki + 1])) {
                                    allow = 1; break;
                                }
                            }
                        }
                        for (ki = 0; ki + 1 < n_extra; ki += 2) {
                            CL_Obj key = vm_extra_args[ki];
                            CL_Obj val = vm_extra_args[ki + 1];
                            int j, found = 0;
                            for (j = 0; j < callee_bc->n_keys; j++) {
                                if (key == callee_bc->key_syms[j]) {
                                    cl_vm.stack[new_bp + callee_bc->key_slots[j]] = val;
                                    if (callee_bc->key_suppliedp_slots)
                                        cl_vm.stack[new_bp + callee_bc->key_suppliedp_slots[j]] = CL_T;
                                    found = 1; break;
                                }
                            }
                            if (!found && key != KW_ALLOW_OTHER_KEYS && !allow)
                                cl_error(CL_ERR_ARGS, "APPLY: unknown keyword argument: %s",
                                         cl_symbol_name(key));
                        }
                    }

                    if (has_rest || has_key)
                        CL_GC_UNPROTECT(1);

                    /* Save current frame state and push new frame */
                    frame->ip = ip;
                    if (cl_vm.fp >= cl_vm.frame_size)
                        cl_error(CL_ERR_OVERFLOW, "Call stack overflow");

                    new_frame = &cl_vm.frames[cl_vm.fp++];
                    new_frame->bytecode = call_func;
                    new_frame->code = callee_bc->code;
                    new_frame->constants = callee_bc->constants;
                    new_frame->ip = 0;
                    new_frame->bp = new_bp;
                    new_frame->n_locals = callee_bc->n_locals;
                    new_frame->nargs = call_nargs;

                    frame = new_frame;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;
                }
            } else {
                cl_error(CL_ERR_TYPE, "APPLY: not a callable function");
            }
            break;
        }

        case OP_CATCH: {
            int16_t catch_offset = read_i16(code, &ip);
            CL_Obj catch_tag = cl_vm_pop();
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_CATCH;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = catch_tag;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = catch_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->bytecode = frame->bytecode;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;
            nlx->gc_root_mark = gc_root_count;
            nlx->compiler_mark = cl_compiler_mark();

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: body executes */
                cl_nlx_top++;
            } else {
                /* longjmp from throw: restore state from NLX frame.
                 * Recompute nlx from global — the local pointer may be
                 * indeterminate after longjmp (C99 7.13.2.1). */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                gc_root_count = nlx->gc_root_mark;
                cl_compiler_restore_to(nlx->compiler_mark);
                {
                    CL_Obj throw_result = nlx->result;
                    cl_vm.sp = nlx->vm_sp;
                    cl_vm.fp = nlx->vm_fp;
                    frame = &cl_vm.frames[cl_vm.fp - 1];
                    code = nlx->code;
                    constants = nlx->constants;
                    base_fp = nlx->base_fp;
                    ip = nlx->catch_ip + nlx->offset;
                    /* Sync frame with NLX-restored state */
                    frame->code = code;
                    frame->constants = constants;
                    frame->bytecode = nlx->bytecode;
                    cl_mv_count = 1;  /* throw delivers single value */
                    cl_vm_push(throw_result);
                }
            }
            break;
        }

        case OP_UNCATCH: {
            /* Normal exit from catch body — pop NLX frame.
             * Search for matching CATCH, as tail calls may have
             * leaked BLOCK frames above it. */
            {
                int ci;
                for (ci = cl_nlx_top - 1; ci >= 0; ci--) {
                    if (cl_nlx_stack[ci].type == CL_NLX_CATCH) {
                        cl_nlx_top = ci;
                        break;
                    }
                }
                if (ci < 0 && cl_nlx_top > 0)
                    cl_nlx_top--;
            }
            break;
        }

        case OP_UWPROT: {
            int16_t uwp_offset = read_i16(code, &ip);
            CL_NLXFrame *nlx;

            if (cl_nlx_top >= CL_MAX_NLX_FRAMES)
                cl_error(CL_ERR_OVERFLOW, "NLX stack overflow");

            nlx = &cl_nlx_stack[cl_nlx_top];
            nlx->type = CL_NLX_UWPROT;
            nlx->vm_sp = cl_vm.sp;
            nlx->vm_fp = cl_vm.fp;
            nlx->tag = CL_NIL;
            nlx->result = CL_NIL;
            nlx->catch_ip = ip;
            nlx->offset = uwp_offset;
            nlx->code = code;
            nlx->constants = constants;
            nlx->bytecode = frame->bytecode;
            nlx->base_fp = base_fp;
            nlx->dyn_mark = cl_dyn_top;
            nlx->handler_mark = cl_handler_top;
            nlx->restart_mark = cl_restart_top;
            nlx->gc_root_mark = gc_root_count;
            nlx->compiler_mark = cl_compiler_mark();

            if (setjmp(nlx->buf) == 0) {
                /* Normal path: protected form executes */
                cl_nlx_top++;


#ifdef CL_DEBUG_UWP
                if (frame->n_locals >= 1 &&
                    cl_vm.stack[frame->bp] != CL_NIL &&
                    cl_vm.stack[frame->bp] != CL_T &&
                    cl_vm.fp >= 88 && dbg_watch_idx < 0) {
                    dbg_watch_idx = frame->bp;
                    dbg_watch_orig = cl_vm.stack[frame->bp];
                    dbg_watch_nlx = cl_nlx_top - 1;
                    fprintf(stderr, "[UWP-WATCH] armed on stack[%d]=0x%x fp=%d nlx=%d\n",
                            frame->bp, (unsigned)cl_vm.stack[frame->bp], cl_vm.fp, dbg_watch_nlx);
                }
#endif
            } else {
                /* longjmp from throw/error through UWP: restore state, jump to cleanup.
                 * Recompute nlx — local pointer may be indeterminate after longjmp. */
                nlx = &cl_nlx_stack[cl_nlx_top];
                cl_dynbind_restore_to(nlx->dyn_mark);
                cl_handler_top = nlx->handler_mark;
                cl_restart_top = nlx->restart_mark;
                gc_root_count = nlx->gc_root_mark;
                cl_compiler_restore_to(nlx->compiler_mark);
                cl_vm.sp = nlx->vm_sp;
                cl_vm.fp = nlx->vm_fp;
                frame = &cl_vm.frames[cl_vm.fp - 1];
                code = nlx->code;
                constants = nlx->constants;
                base_fp = nlx->base_fp;
                ip = nlx->catch_ip + nlx->offset;
                /* Sync frame with NLX-restored state */
                frame->code = code;
                frame->constants = constants;
                frame->bytecode = nlx->bytecode;

#ifdef CL_DEBUG_UWP
                /* Detect slot corruption after UWP longjmp */
                {
                    int di;
                    int has_corruption = 0;
                    for (di = 0; di < frame->n_locals && di < 8; di++) {
                        if (cl_vm.stack[frame->bp + di] == CL_T) {
                            has_corruption = 1;
                            break;
                        }
                    }
                    if (has_corruption) {
                        int fi;
                        fprintf(stderr, "[UWP-LONGJMP] CORRUPTION at bp=%d sp=%d fp=%d n_locals=%d\n",
                                frame->bp, cl_vm.sp, cl_vm.fp, frame->n_locals);
                        fprintf(stderr, "[UWP-LONGJMP] slots:");
                        for (di = 0; di < frame->n_locals && di < 16; di++)
                            fprintf(stderr, " [%d]=0x%x", di, (unsigned)cl_vm.stack[frame->bp + di]);
                        fprintf(stderr, "\n");
                        fprintf(stderr, "[UWP-LONGJMP] nlx_top=%d, pending_throw=%d\n",
                                cl_nlx_top, cl_pending_throw);
                        /* Print NLX stack and frame stack */
                        for (fi = cl_nlx_top; fi >= 0 && fi >= cl_nlx_top - 5; fi--)
                            fprintf(stderr, "[UWP-LONGJMP] nlx[%d] type=%d vm_fp=%d vm_sp=%d\n",
                                    fi, cl_nlx_stack[fi].type, cl_nlx_stack[fi].vm_fp, cl_nlx_stack[fi].vm_sp);
                        for (fi = cl_vm.fp - 1; fi >= 0 && fi >= cl_vm.fp - 15; fi--) {
                            CL_Frame *f = &cl_vm.frames[fi];
                            const char *fn = "?";
                            CL_Bytecode *fbc = NULL;
                            if (CL_CLOSURE_P(f->bytecode)) {
                                CL_Closure *cc = (CL_Closure *)CL_OBJ_TO_PTR(f->bytecode);
                                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(cc->bytecode);
                            } else if (CL_BYTECODE_P(f->bytecode)) {
                                fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(f->bytecode);
                            }
                            if (fbc && fbc->name != CL_NIL) fn = cl_symbol_name(fbc->name);
                            else if (fbc) fn = "<anon>";
                            fprintf(stderr, "  frame[%d] fn=%s bp=%d n_locals=%d nargs=%d\n",
                                    fi, fn, f->bp, f->n_locals, f->nargs);
                        }
                    }
                }
#endif
            }
            break;
        }

        case OP_UWPOP: {
            /* Normal exit from protected form — pop NLX frame, clear pending.
             * Must find the matching UWPROT frame, as tail calls in called
             * functions may have leaked BLOCK/CATCH frames above it. */
            {
                int uwi;
                for (uwi = cl_nlx_top - 1; uwi >= 0; uwi--) {
                    if (cl_nlx_stack[uwi].type == CL_NLX_UWPROT) {
                        cl_nlx_top = uwi;
                        break;
                    }
                }
                if (uwi < 0 && cl_nlx_top > 0)
                    cl_nlx_top--;  /* fallback: pop whatever is on top */
            }
            cl_pending_throw = 0;
            break;
        }

        case OP_UWRETHROW: {
            /* After cleanup forms: re-initiate pending throw/error if any */
            if (cl_pending_throw == 1) {
                /* Re-throw: find matching catch or block */
                CL_Obj ptag = cl_pending_tag;
                CL_Obj pval = cl_pending_value;
                int i;

                for (i = cl_nlx_top - 1; i >= 0; i--) {
                    if ((cl_nlx_stack[i].type == CL_NLX_CATCH ||
                         cl_nlx_stack[i].type == CL_NLX_BLOCK ||
                         cl_nlx_stack[i].type == CL_NLX_TAGBODY) &&
                        cl_nlx_stack[i].tag == ptag) {
                        /* Check for interposing UWPROT (skip stale ones) */
                        int j;
                        for (j = cl_nlx_top - 1; j > i; j--) {
                            if (cl_nlx_stack[j].type == CL_NLX_UWPROT &&
                                !nlx_frame_is_stale(&cl_nlx_stack[j])) {
                                /* Jump to interposing UWPROT first */
                                cl_nlx_top = j;
                                longjmp(cl_nlx_stack[j].buf, 1);
                            }
                        }
                        /* No interposing UWPROT, go directly to target */
                        cl_pending_throw = 0;
                        cl_nlx_stack[i].result = pval;
                        cl_nlx_top = i;
                        longjmp(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No catch found — signal error */
                cl_pending_throw = 0;
                cl_error(CL_ERR_GENERAL, "No catch for tag during re-throw");
            } else if (cl_pending_throw == 2) {
                /* Re-throw error: find interposing UWPROT or error frame (skip stale) */
                int i;
                for (i = cl_nlx_top - 1; i >= 0; i--) {
                    if (cl_nlx_stack[i].type == CL_NLX_UWPROT &&
                        !nlx_frame_is_stale(&cl_nlx_stack[i])) {
                        cl_nlx_top = i;
                        longjmp(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No more UWPROT — propagate to error handler.
                 * Restore VM state to before this cl_vm_eval call
                 * so subsequent evaluations start from a clean state. */
                {
                    int err_code = cl_pending_error_code;
                    cl_pending_throw = 0;
                    cl_nlx_top = 0;
                    cl_dynbind_restore_to(0);
                    cl_handler_top = 0;
                    cl_restart_top = 0;
                    cl_vm.fp = base_fp;
                    cl_vm.sp = cl_vm.frames[base_fp].bp;
                    cl_error_code = err_code;
                    strncpy(cl_error_msg, cl_pending_error_msg, sizeof(cl_error_msg) - 1);
                    cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';
                    if (cl_error_frame_top > 0) {
                        cl_error_frame_top--;
                        cl_error_frames[cl_error_frame_top].active = 0;
                        longjmp(cl_error_frames[cl_error_frame_top].buf, err_code);
                    }
                    platform_write_string("FATAL ERROR: ");
                    platform_write_string(cl_error_msg);
                    platform_write_string("\n");
                    exit(1);
                }
            }
            /* cl_pending_throw == 0: nop */
            break;
        }

        case OP_MV_LOAD: {
            uint8_t index = code[ip++];
            cl_vm_push((int)index < cl_mv_count ? cl_mv_values[index] : CL_NIL);
            break;
        }

        case OP_MV_TO_LIST: {
            /* Pops primary from stack, builds list from MV buffer.
             * For single values (inline opcodes), uses the popped primary. */
            CL_Obj primary = cl_vm_pop();
            CL_Obj list = CL_NIL;
            if (cl_mv_count == 0) {
                /* (values) — no values, empty list */
            } else if (cl_mv_count == 1) {
                list = cl_cons(primary, CL_NIL);
            } else {
                int i;
                for (i = cl_mv_count - 1; i >= 0; i--)
                    list = cl_cons(cl_mv_values[i], list);
            }
            cl_vm_push(list);
            cl_mv_count = 1;
            break;
        }

        case OP_NTH_VALUE: {
            /* Stack: [index] [primary]  (primary on top)
             * Pops primary, pops index, pushes result. */
            CL_Obj primary = cl_vm_pop();
            CL_Obj idx_obj = cl_vm_pop();
            int idx;
            if (!CL_FIXNUM_P(idx_obj))
                cl_error(CL_ERR_TYPE, "NTH-VALUE: index must be a number");
            idx = CL_FIXNUM_VAL(idx_obj);
            if (idx == 0)
                cl_vm_push(primary);
            else
                cl_vm_push(idx > 0 && idx < cl_mv_count ? cl_mv_values[idx] : CL_NIL);
            cl_mv_count = 1;
            break;
        }

        case OP_DYNBIND: {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Obj new_val = cl_vm_pop();
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");
            cl_dyn_stack[cl_dyn_top].symbol = sym;
            cl_dyn_stack[cl_dyn_top].old_value = s->value;
            cl_dyn_top++;
            s->value = new_val;
            break;
        }

        case OP_DYNUNBIND: {
            uint8_t count = code[ip++];
            cl_dynbind_restore_to(cl_dyn_top - count);
            break;
        }

        case OP_MV_RESET:
            cl_mv_count = 1;
            break;

        case OP_RPLACA: {
            CL_Obj new_car = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACA: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->car = new_car;
            cl_vm_push(new_car);
            cl_mv_count = 1;
            break;
        }

        case OP_RPLACD: {
            CL_Obj new_cdr = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACD: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->cdr = new_cdr;
            cl_vm_push(new_cdr);
            cl_mv_count = 1;
            break;
        }

        case OP_ASET: {
            CL_Obj val = cl_vm_pop();
            CL_Obj idx_obj = cl_vm_pop();
            CL_Obj vec_obj = cl_vm_pop();
            int32_t idx;
            if (!CL_FIXNUM_P(idx_obj))
                cl_error(CL_ERR_TYPE, "ASET: index must be a number");
            idx = CL_FIXNUM_VAL(idx_obj);
            if (CL_BIT_VECTOR_P(vec_obj)) {
                CL_BitVector *bv = (CL_BitVector *)CL_OBJ_TO_PTR(vec_obj);
                int32_t v;
                if (idx < 0 || (uint32_t)idx >= cl_bv_active_length(bv))
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range", (int)idx);
                if (!CL_FIXNUM_P(val))
                    cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
                v = CL_FIXNUM_VAL(val);
                if (v != 0 && v != 1)
                    cl_error(CL_ERR_TYPE, "ASET: value must be 0 or 1 for bit vector");
                cl_bv_set_bit(bv, (uint32_t)idx, v);
            } else if (CL_STRING_P(vec_obj)) {
                CL_String *str = (CL_String *)CL_OBJ_TO_PTR(vec_obj);
                if (idx < 0 || (uint32_t)idx >= str->length)
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                             (int)idx, (unsigned long)(str->length - 1));
                if (!CL_CHAR_P(val))
                    cl_error(CL_ERR_TYPE, "ASET: value must be a character for string");
                str->data[idx] = (char)CL_CHAR_VAL(val);
            } else if (CL_VECTOR_P(vec_obj)) {
                CL_Vector *vec = (CL_Vector *)CL_OBJ_TO_PTR(vec_obj);
                if (idx < 0 || (uint32_t)idx >= vec->length)
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                             (int)idx, (unsigned long)(vec->length - 1));
                cl_vector_data(vec)[idx] = val;
            } else {
                cl_error(CL_ERR_TYPE, "ASET: not a vector");
            }
            cl_vm_push(val);
            cl_mv_count = 1;
            break;
        }

        default:
        unknown_opcode:
        {
            CL_Bytecode *dbg_bc;
            uint8_t dbg_type = CL_HDR_TYPE(CL_OBJ_TO_PTR(frame->bytecode));
            if (dbg_type == TYPE_CLOSURE) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                dbg_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
                fprintf(stderr, "[VM] Unknown opcode 0x%02x at ip=%u (CLOSURE 0x%x -> BC 0x%x)\n",
                        op, ip - 1, (unsigned)frame->bytecode,
                        (unsigned)cl->bytecode);
            } else {
                dbg_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
                fprintf(stderr, "[VM] Unknown opcode 0x%02x at ip=%u (BYTECODE 0x%x)\n",
                        op, ip - 1, (unsigned)frame->bytecode);
            }
            fprintf(stderr, "[VM]   code_len=%u, code=%p\n",
                    dbg_bc->code_len, (void *)dbg_bc->code);
            if (CL_SYMBOL_P(dbg_bc->name))
                fprintf(stderr, "[VM]   name: %s\n",
                        cl_symbol_name(dbg_bc->name));
            if (dbg_bc->source_file)
                fprintf(stderr, "[VM]   source: %s:%u\n",
                        dbg_bc->source_file, dbg_bc->source_line);
            /* Print surrounding bytes from frame->code using code_len as limit */
            if (dbg_bc->code_len > 0) {
                uint32_t start = (ip > 10) ? ip - 10 : 0;
                uint32_t end = (ip + 10 < dbg_bc->code_len) ? ip + 10 : dbg_bc->code_len;
                uint32_t j;
                fprintf(stderr, "[VM]   bytes around ip=%u:", ip - 1);
                for (j = start; j < end; j++)
                    fprintf(stderr, " %02x", frame->code[j]);
                fprintf(stderr, "\n");
            }
            /* Dump all VM frames */
            {
                int fi;
                fprintf(stderr, "[VM]   fp=%d sp=%d base_fp=%d\n",
                        cl_vm.fp, cl_vm.sp, base_fp);
                for (fi = cl_vm.fp - 1; fi >= 0; fi--) {
                    CL_Frame *df = &cl_vm.frames[fi];
                    CL_Bytecode *dbc;
                    uint8_t ft = CL_HDR_TYPE(CL_OBJ_TO_PTR(df->bytecode));
                    if (ft == TYPE_CLOSURE) {
                        CL_Closure *dcl = (CL_Closure *)CL_OBJ_TO_PTR(df->bytecode);
                        dbc = (CL_Bytecode *)CL_OBJ_TO_PTR(dcl->bytecode);
                    } else {
                        dbc = (CL_Bytecode *)CL_OBJ_TO_PTR(df->bytecode);
                    }
                    fprintf(stderr, "[VM]   frame[%d] ip=%u/%u bp=%u",
                            fi, df->ip, dbc->code_len, df->bp);
                    if (CL_SYMBOL_P(dbc->name))
                        fprintf(stderr, " %s", cl_symbol_name(dbc->name));
                    fprintf(stderr, "\n");
                }
            }
            vm_trace_dump();
            cl_error(CL_ERR_GENERAL, "Unknown opcode: 0x%02x at ip=%u",
                     op, ip - 1);
            return CL_NIL;
        }
        }
    }
}

/*
 * cl_vm_eval — execute a bytecode object.
 * Thin wrapper: pushes initial frame, calls cl_vm_run.
 */
CL_Obj cl_vm_eval(CL_Obj bytecode_obj)
{
    CL_Bytecode *bc;
    CL_Frame *frame;
    int base_fp, base_nlx;
    CL_Obj result;

#ifdef DEBUG_VM
    vm_eval_depth++;
    if (vm_eval_depth > vm_eval_max_depth)
        vm_eval_max_depth = vm_eval_depth;
    cl_check_c_stack("cl_vm_eval");
#endif

    if (CL_NULL_P(bytecode_obj)) return CL_NIL;

    /* Handle both bytecode and closure */
    if (CL_CLOSURE_P(bytecode_obj)) {
        CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(bytecode_obj);
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl->bytecode);
    } else {
        bc = (CL_Bytecode *)CL_OBJ_TO_PTR(bytecode_obj);
    }

    if (!bc || !bc->code) return CL_NIL;

    /* Set up initial frame */
    base_fp = cl_vm.fp;
    base_nlx = cl_nlx_top;
    if (cl_vm.fp >= cl_vm.frame_size)
        cl_error(CL_ERR_OVERFLOW, "VM frame stack overflow");
    frame = &cl_vm.frames[cl_vm.fp++];
    frame->bytecode = bytecode_obj;
    frame->code = bc->code;
    frame->constants = bc->constants;
    frame->ip = 0;
    frame->bp = cl_vm.sp;
    frame->n_locals = bc->n_locals;
    frame->nargs = 0;

    /* Allocate space for locals */
    {
        int i;
        for (i = 0; i < bc->n_locals; i++)
            cl_vm_push(CL_NIL);
    }

    result = cl_vm_run(base_fp, base_nlx);
#ifdef DEBUG_VM
    vm_eval_depth--;
#endif
    return result;
}
