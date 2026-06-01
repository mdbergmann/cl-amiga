#include "vm.h"
#include "opcodes.h"
#include "symbol.h"
#include "package.h"
#include "mem.h"
#include "bignum.h"
#include "ratio.h"
#include "float.h"
#include "error.h"
#include "compiler.h"
#include "printer.h"
#include "string_utils.h"
#include "stream.h"
#include "../platform/platform.h"
#include "../platform/platform_thread.h"
#include "../jit/jit.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Local macros for statics moved into CL_Thread */
#define vm_extra_args       (CT->vm_extra_args_buf)
#define vm_extra_args_count (CT->vm_extra_count)
#define vm_flat_args        (CT->vm_flat_args_buf)
#define vm_trace            (CT->vm_trace_buf)
#define vm_trace_idx        (CT->vm_trace_pos)
#define vm_eval_max_depth   (CT->vm_max_eval_depth)
#define VM_TRACE_SIZE       CL_VM_TRACE_SIZE

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

/* Opcode-frequency profiler — build with -DPROFILE_OPCODES.
 * Counters and reset/dump are always defined (no-op when disabled)
 * so the (clamiga::%op-counts-*) builtins are present unconditionally
 * and Lisp profiling scripts stay portable. */
#ifdef PROFILE_OPCODES
static uint32_t cl_op_counts_arr[256];

static const char *cl_opcode_name(uint8_t op)
{
    switch (op) {
    case OP_CONST: return "CONST";
    case OP_LOAD: return "LOAD";
    case OP_STORE: return "STORE";
    case OP_GLOAD: return "GLOAD";
    case OP_GSTORE: return "GSTORE";
    case OP_UPVAL: return "UPVAL";
    case OP_POP: return "POP";
    case OP_DUP: return "DUP";
    case OP_CONS: return "CONS";
    case OP_CAR: return "CAR";
    case OP_CDR: return "CDR";
    case OP_ADD: return "ADD";
    case OP_SUB: return "SUB";
    case OP_MUL: return "MUL";
    case OP_DIV: return "DIV";
    case OP_EQ: return "EQ";
    case OP_LT: return "LT";
    case OP_GT: return "GT";
    case OP_LE: return "LE";
    case OP_GE: return "GE";
    case OP_NUMEQ: return "NUMEQ";
    case OP_NOT: return "NOT";
    case OP_JMP: return "JMP";
    case OP_JNIL: return "JNIL";
    case OP_JTRUE: return "JTRUE";
    case OP_CALL: return "CALL";
    case OP_TAILCALL: return "TAILCALL";
    case OP_RET: return "RET";
    case OP_CLOSURE: return "CLOSURE";
    case OP_APPLY: return "APPLY";
    case OP_LIST: return "LIST";
    case OP_NIL: return "NIL";
    case OP_T: return "T";
    case OP_FLOAD: return "FLOAD";
    case OP_DEFMACRO: return "DEFMACRO";
    case OP_ARGC: return "ARGC";
    case OP_CATCH: return "CATCH";
    case OP_UNCATCH: return "UNCATCH";
    case OP_UWPROT: return "UWPROT";
    case OP_UWPOP: return "UWPOP";
    case OP_UWRETHROW: return "UWRETHROW";
    case OP_MV_LOAD: return "MV_LOAD";
    case OP_MV_TO_LIST: return "MV_TO_LIST";
    case OP_NTH_VALUE: return "NTH_VALUE";
    case OP_DYNBIND: return "DYNBIND";
    case OP_DYNUNBIND: return "DYNUNBIND";
    case OP_RPLACA: return "RPLACA";
    case OP_RPLACD: return "RPLACD";
    case OP_ASET: return "ASET";
    case OP_DEFTYPE: return "DEFTYPE";
    case OP_HANDLER_PUSH: return "HANDLER_PUSH";
    case OP_HANDLER_POP: return "HANDLER_POP";
    case OP_RESTART_PUSH: return "RESTART_PUSH";
    case OP_RESTART_POP: return "RESTART_POP";
    case OP_ASSERT_TYPE: return "ASSERT_TYPE";
    case OP_BLOCK_PUSH: return "BLOCK_PUSH";
    case OP_BLOCK_POP: return "BLOCK_POP";
    case OP_BLOCK_RETURN: return "BLOCK_RETURN";
    case OP_FSTORE: return "FSTORE";
    case OP_MAKE_CELL: return "MAKE_CELL";
    case OP_CELL_REF: return "CELL_REF";
    case OP_CELL_SET_LOCAL: return "CELL_SET_LOCAL";
    case OP_CELL_SET_UPVAL: return "CELL_SET_UPVAL";
    case OP_TAGBODY_PUSH: return "TAGBODY_PUSH";
    case OP_TAGBODY_POP: return "TAGBODY_POP";
    case OP_TAGBODY_GO: return "TAGBODY_GO";
    case OP_PROGV_BIND: return "PROGV_BIND";
    case OP_PROGV_UNBIND: return "PROGV_UNBIND";
    case OP_DEFSETF: return "DEFSETF";
    case OP_DEFVAR: return "DEFVAR";
    case OP_MV_RESET: return "MV_RESET";
    case OP_AMIGA_CALL: return "AMIGA_CALL";
    case OP_STRUCT_REF: return "STRUCT_REF";
    case OP_STRUCT_SET: return "STRUCT_SET";
    case OP_HALT: return "HALT";
    default: return "?";
    }
}
#endif /* PROFILE_OPCODES */

void cl_op_counts_reset(void)
{
#ifdef PROFILE_OPCODES
    uint32_t i;
    for (i = 0; i < 256; i++) cl_op_counts_arr[i] = 0;
#endif
}

void cl_op_counts_dump(FILE *out)
{
#ifdef PROFILE_OPCODES
    uint32_t i, j;
    uint32_t total = 0;
    /* indices sorted by count, desc */
    uint8_t idx[256];
    for (i = 0; i < 256; i++) { idx[i] = (uint8_t)i; total += cl_op_counts_arr[i]; }
    /* simple insertion sort by count desc — only ~80 non-zero entries */
    for (i = 1; i < 256; i++) {
        uint8_t k = idx[i];
        uint32_t kv = cl_op_counts_arr[k];
        j = i;
        while (j > 0 && cl_op_counts_arr[idx[j-1]] < kv) {
            idx[j] = idx[j-1]; j--;
        }
        idx[j] = k;
    }
    fprintf(out, "=== opcode counts (total=%lu) ===\n", (unsigned long)total);
    fprintf(out, "  %-18s %12s %8s\n", "op", "count", "pct");
    for (i = 0; i < 256; i++) {
        uint32_t c = cl_op_counts_arr[idx[i]];
        if (c == 0) break;
        fprintf(out, "  0x%02x %-13s %12lu %7.2f%%\n",
                idx[i], cl_opcode_name(idx[i]),
                (unsigned long)c,
                total ? (100.0 * (double)c / (double)total) : 0.0);
    }
    fflush(out);
#else
    fprintf(out, "PROFILE_OPCODES not compiled in\n");
    fflush(out);
#endif
}

/* All per-thread state (VM, NLX, dyn/handler/restart stacks, MV,
 * trace, backtrace, pending throw) now lives in CL_Thread.
 * Compatibility macros in thread.h redirect the old names. */

void cl_dynbind_restore_to(int mark)
{
    CL_Thread *t = CT;
    int touched_package = 0;
    while (cl_dyn_top > mark) {
        cl_dyn_top--;
        {
            CL_Obj old_val = cl_dyn_stack[cl_dyn_top].old_value;
            CL_Obj sym = cl_dyn_stack[cl_dyn_top].symbol;
            if (sym == SYM_STAR_PACKAGE)
                touched_package = 1;
            if (old_val == CL_TLV_ABSENT)
                cl_tlv_remove(t, sym);
            else
                cl_tlv_set(t, sym, old_val);
        }
    }
    if (touched_package)
        cl_sync_current_package_from_dynamic();
}

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
    cl_saved_pending_top = 0;
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
#ifdef DEBUG_VM_PUSH_GUARD
    /* Catch the bad write at its source: if a "heap-tagged" CL_Obj points
     * past the arena, something just used a raw pointer where an arena
     * offset belongs.  Backtrace + abort on first occurrence. */
    if (CL_HEAP_P(val) && val >= cl_heap.arena_size) {
        fprintf(stderr,
                "[VM-PUSH-GUARD] cl_vm_push: val=0x%08x out of arena (size=0x%08x) sp=%d fp=%d\n",
                (unsigned)val, (unsigned)cl_heap.arena_size, cl_vm.sp, cl_vm.fp);
        cl_capture_backtrace();
        fprintf(stderr, "%s", cl_backtrace_buf);
        fflush(stderr);
        abort();
    }
#endif
    cl_vm.stack[cl_vm.sp++] = val;
#ifdef CL_DEBUG_UWP
    if (dbg_watch_idx >= 0 && (cl_vm.sp - 1) == dbg_watch_idx && val != dbg_watch_orig)
        DBG_CHECK_WATCH("cl_vm_push");
#endif
#ifdef DEBUG_GC
    /* Detect push of a freed (poison-filled) heap object */
    if (CL_HEAP_P(val) && val < cl_heap.arena_size) {
        uint8_t *p = (uint8_t *)CL_OBJ_TO_PTR(val);
        /* Check if bytes 8-11 (after CL_FreeBlock header) are poison 0xDE */
        if (p[8] == 0xDE && p[9] == 0xDE && p[10] == 0xDE && p[11] == 0xDE) {
            char buf[256];
            CL_Obj cur_bc = (cl_vm.fp > 0) ? cl_vm.frames[cl_vm.fp - 1].bytecode : CL_NIL;
            snprintf(buf, sizeof(buf),
                     "VM-PUSH-FREED: sp=%d val=0x%08x hdr=0x%08x bc=0x%08x fp=%d\n",
                     cl_vm.sp - 1, (unsigned)val,
                     (unsigned)*(uint32_t *)p,
                     (unsigned)cur_bc, cl_vm.fp);
            platform_write_string(buf);
            cl_capture_backtrace();
            platform_write_string(cl_backtrace_buf);
            platform_write_string("\n");
        }
    }
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

/* Funcallable instance support: a standard-generic-function struct is a
 * callable object whose "discriminating function" lives at slot 3.  The
 * VM + funcall/apply treat it as transparent — they unwrap to slot 3 and
 * dispatch normally.  Slot 3 may itself be any callable (closure, bytecode,
 * builtin), including one installed by SET-FUNCALLABLE-INSTANCE-FUNCTION. */
int cl_funcallable_instance_p(CL_Obj obj)
{
    static CL_Obj sym_sgf = CL_NIL;
    if (!CL_STRUCT_P(obj)) return 0;
    if (sym_sgf == CL_NIL)
        sym_sgf = cl_intern("STANDARD-GENERIC-FUNCTION", 25);
    return ((CL_Struct *)CL_OBJ_TO_PTR(obj))->type_desc == sym_sgf;
}

CL_Obj cl_unwrap_funcallable(CL_Obj obj)
{
    if (cl_funcallable_instance_p(obj)) {
        CL_Struct *st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
        if (st->n_slots > 3) return st->slots[3];
    }
    return obj;
}

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

    /* Unwrap funcallable instances (e.g. a generic-function struct) so the
     * standard call path sees the underlying discriminating function. */
    func = cl_unwrap_funcallable(func);

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
        int base_fp, base_nlx, saved_sp;
        CL_Obj result;

        cl_check_c_stack("cl_vm_apply");

        /* Push a minimal stub frame BEFORE pushing func+args.
         * bp = current sp, n_locals = 0.  After OP_CALL consumes
         * func+args and pushes the result, sp = bp+1 so OP_HALT
         * sees the result. */
        base_fp = cl_vm.fp;
        saved_sp = cl_vm.sp;
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
        frame->nlx_level = cl_nlx_top;

        /* Push function and arguments onto VM stack */
        cl_vm_push(func);
        for (i = 0; i < nargs; i++)
            cl_vm_push(args[i]);

        result = cl_vm_run(base_fp, base_nlx);

        /* Restore fp/sp: OP_HALT doesn't decrement fp, so the stub
         * frame would leak.  Without this restore, each cl_vm_apply
         * call leaves fp one higher than it should be, causing OP_RET
         * in the caller's cl_vm_run to restore the wrong frame. */
        cl_vm.fp = base_fp;
        cl_vm.sp = saved_sp;
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

static int32_t read_i32(uint8_t *code, uint32_t *ip)
{
    int32_t val = (int32_t)(((uint32_t)code[*ip] << 24) |
                            ((uint32_t)code[*ip + 1] << 16) |
                            ((uint32_t)code[*ip + 2] << 8) |
                            (uint32_t)code[*ip + 3]);
    *ip += 4;
    return val;
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
        cl_write_cstring_to_trace("  ");
    snprintf(buf, sizeof(buf), "%d: (", cl_trace_depth);
    cl_write_cstring_to_trace(buf);
    cl_write_cstring_to_trace(cl_symbol_name(name_sym));
    for (i = 0; i < nargs; i++) {
        cl_write_cstring_to_trace(" ");
        cl_prin1_to_string(args[i], buf, sizeof(buf));
        cl_write_cstring_to_trace(buf);
    }
    cl_write_cstring_to_trace(")\n");
}

static void trace_print_exit(CL_Obj name_sym, CL_Obj result)
{
    char buf[128];
    int i;
    for (i = 0; i < cl_trace_depth; i++)
        cl_write_cstring_to_trace("  ");
    snprintf(buf, sizeof(buf), "%d: ", cl_trace_depth);
    cl_write_cstring_to_trace(buf);
    cl_write_cstring_to_trace(cl_symbol_name(name_sym));
    cl_write_cstring_to_trace(" returned ");
    cl_prin1_to_string(result, buf, sizeof(buf));
    cl_write_cstring_to_trace(buf);
    cl_write_cstring_to_trace("\n");
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

/* A cl_vm_apply stub frame carries no user function — its code points at
 * the frame's own embedded stub_code (the OP_CALL/OP_HALT trampoline set up
 * in cl_vm_apply).  JIT'd calls route through cl_vm_apply, so these stubs
 * sit between the JIT shadow frames and would otherwise pollute the
 * backtrace with anonymous entries; the backtrace readers skip them.  On
 * the bytecode-only host build there are no JIT shadow frames and ordinary
 * calls push real frames, so nothing is skipped there. */
static int frame_is_stub(CL_Frame *f)
{
    return f->code == f->stub_code;
}

/* Format a single backtrace frame into buf+pos, return new pos */
static int bt_format_frame(int pos, int depth, CL_Obj name,
                           const char *file, int line)
{
    int n;
    n = snprintf(cl_backtrace_buf + pos,
                 CL_BACKTRACE_BUF_SIZE - pos, "  %d: ", depth);
    pos += n;
    if (pos >= CL_BACKTRACE_BUF_SIZE - 1) return pos;

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
    return pos;
}

/* Detect repeating frame pattern starting at frame index i (descending).
 * Returns the cycle length (2..8) if found, 0 otherwise.
 * A cycle is detected when the same sequence of (name, source_line) repeats
 * at least 3 times consecutively. */
static int bt_detect_cycle(int start, int *repeat_count)
{
    int total = start + 1;  /* number of frames available */
    int cycle_len;

    for (cycle_len = 2; cycle_len <= 8 && cycle_len * 3 <= total; cycle_len++) {
        int repeats = 1;
        int k;
        /* Check if the pattern at [start..start-cycle_len+1] repeats */
        for (k = 1; k * cycle_len <= total - cycle_len; k++) {
            int j, match = 1;
            for (j = 0; j < cycle_len; j++) {
                CL_Frame *f1 = &cl_vm.frames[start - j];
                CL_Frame *f2 = &cl_vm.frames[start - j - k * cycle_len];
                CL_Obj n1 = get_func_name(f1->bytecode);
                CL_Obj n2 = get_func_name(f2->bytecode);
                CL_Bytecode *bc1 = get_frame_bytecode(f1);
                CL_Bytecode *bc2 = get_frame_bytecode(f2);
                int line1 = bc1 ? lookup_source_line(bc1, f1->ip) : 0;
                int line2 = bc2 ? lookup_source_line(bc2, f2->ip) : 0;
                if (n1 != n2 || line1 != line2) { match = 0; break; }
            }
            if (match) repeats++; else break;
        }
        if (repeats >= 3) {
            *repeat_count = repeats;
            return cycle_len;
        }
    }
    return 0;
}

void cl_capture_backtrace(void)
{
    int i, pos = 0;
    int max_show = 20;
    int depth = 0;

    cl_backtrace_buf[0] = '\0';
    /* Snapshot the frame depth so a debugger-hook (SLDB) can introspect the
     * error-time backtrace via cl_vm_backtrace_list / cl_vm_frame_locals even
     * after the hook has pushed its own frames on top. */
    cl_debug_base_fp = cl_vm.fp;
    if (cl_vm.fp <= 0) return;

    /* Check for repeating patterns (likely infinite recursion).
     * Try starting from each of the first few frames to find the cycle. */
    for (i = cl_vm.fp - 1; i >= cl_vm.fp - 5 && i >= 0; i--) {
        int repeat_count = 0;
        int cycle_len = bt_detect_cycle(i, &repeat_count);
        if (cycle_len > 0 && repeat_count >= 5) {
            int j, n;
            int prefix_frames = cl_vm.fp - 1 - i;
            /* Show prefix frames (before the cycle starts) */
            for (j = 0; j < prefix_frames && pos < CL_BACKTRACE_BUF_SIZE - 64; j++) {
                CL_Frame *f = &cl_vm.frames[cl_vm.fp - 1 - j];
                CL_Obj nm = get_func_name(f->bytecode);
                CL_Bytecode *bc = get_frame_bytecode(f);
                int line = bc ? lookup_source_line(bc, f->ip) : 0;
                const char *file = bc ? bc->source_file : NULL;
                pos = bt_format_frame(pos, j, nm, file, line);
            }
            depth = prefix_frames;
            /* Show one cycle */
            for (j = 0; j < cycle_len && pos < CL_BACKTRACE_BUF_SIZE - 64; j++, depth++) {
                CL_Frame *f = &cl_vm.frames[i - j];
                CL_Obj nm = get_func_name(f->bytecode);
                CL_Bytecode *bc = get_frame_bytecode(f);
                int line = bc ? lookup_source_line(bc, f->ip) : 0;
                const char *file = bc ? bc->source_file : NULL;
                pos = bt_format_frame(pos, depth, nm, file, line);
            }
            n = snprintf(cl_backtrace_buf + pos,
                         CL_BACKTRACE_BUF_SIZE - pos,
                         "  --- above %d frames repeat %d times (%d frames total) ---\n",
                         cycle_len, repeat_count, cycle_len * repeat_count);
            pos += n;
            /* Skip past the repeated frames, show remaining */
            {
                int skip_to = i - cycle_len * repeat_count;
                for (j = skip_to; j >= 0 && depth < max_show + 5; j--, depth++) {
                    CL_Frame *f = &cl_vm.frames[j];
                    CL_Obj nm = get_func_name(f->bytecode);
                    CL_Bytecode *bc = get_frame_bytecode(f);
                    int line = bc ? lookup_source_line(bc, f->ip) : 0;
                    const char *file = bc ? bc->source_file : NULL;
                    pos = bt_format_frame(pos, depth, nm, file, line);
                    if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;
                }
                if (j >= 0) {
                    snprintf(cl_backtrace_buf + pos,
                             CL_BACKTRACE_BUF_SIZE - pos,
                             "  ... %d more frames\n", j + 1);
                }
            }
            return;
        }
    }

    /* Normal backtrace (no detected cycle) */
    for (i = cl_vm.fp - 1; i >= 0 && depth < max_show; i--) {
        CL_Frame *f = &cl_vm.frames[i];
        CL_Obj name;
        CL_Bytecode *bc;
        int line;
        const char *file;
        if (frame_is_stub(f)) continue;
        name = get_func_name(f->bytecode);
        bc = get_frame_bytecode(f);
        line = bc ? lookup_source_line(bc, f->ip) : 0;
        file = bc ? bc->source_file : NULL;
        pos = bt_format_frame(pos, depth, name, file, line);
        depth++;
        if (pos >= CL_BACKTRACE_BUF_SIZE - 1) break;
    }

    if (cl_vm.fp > max_show) {
        snprintf(cl_backtrace_buf + pos,
                 CL_BACKTRACE_BUF_SIZE - pos,
                 "  ... %d more frames\n", cl_vm.fp - max_show);
    }
}

/* --- Structured backtrace introspection (Sly/SLYNK SLDB backend) --- */

/* Resolve the number of frames to expose.  Inside the debugger this is the
 * depth captured at error time (cl_debug_base_fp); the live fp is higher
 * because the hook pushed its own frames.  Outside an error (stale or unset
 * snapshot) we fall back to the live frame pointer so an ad-hoc
 * (ext:backtrace) still reports the current stack. */
static int bt_resolve_base(void)
{
    int base = cl_debug_base_fp;
    if (base <= 0 || base > cl_vm.fp)
        base = cl_vm.fp;
    return base;
}

/* Count the user-visible (non-stub) frames in [0, base). */
static int bt_count_real(int base)
{
    int i, n = 0;
    for (i = 0; i < base; i++)
        if (!frame_is_stub(&cl_vm.frames[i]))
            n++;
    return n;
}

/* Map a logical backtrace index (0 = innermost non-stub frame) to a physical
 * cl_vm.frames index, or -1 when out of range.  Lets the structured readers
 * present a contiguous, stub-free frame numbering. */
static int bt_physical_frame(int base, int logical)
{
    int i, seen = 0;
    for (i = base - 1; i >= 0; i--) {
        if (frame_is_stub(&cl_vm.frames[i]))
            continue;
        if (seen == logical)
            return i;
        seen++;
    }
    return -1;
}

/* Build an uninterned placeholder symbol named ARG<idx> or LOCAL<idx>. */
static CL_Obj bt_slot_placeholder(int idx, int is_arg)
{
    char buf[24];
    int len = snprintf(buf, sizeof(buf), is_arg ? "ARG%d" : "LOCAL%d", idx);
    CL_Obj str = cl_make_string(buf, (uint32_t)len);
    CL_Obj sym;
    CL_GC_PROTECT(str);
    sym = cl_make_uninterned_symbol(str);
    CL_GC_UNPROTECT(1);
    return sym;
}

CL_Obj cl_vm_backtrace_list(int max_frames)
{
    int base = bt_resolve_base();
    int count = bt_count_real(base);
    int li;
    CL_Obj result = CL_NIL, entry = CL_NIL, name = CL_NIL, file_str = CL_NIL;

    if (max_frames > 0 && count > max_frames)
        count = max_frames;
    if (count <= 0)
        return CL_NIL;

    /* result/entry/name/file_str all hold heap refs across cl_cons /
     * cl_make_string allocations below — protect all four. */
    CL_GC_PROTECT(result);
    CL_GC_PROTECT(entry);
    CL_GC_PROTECT(name);
    CL_GC_PROTECT(file_str);

    /* Walk logical indices outermost→innermost and prepend, so the final
     * list is innermost-first with index 0 = innermost non-stub frame.
     * Stub frames (cl_vm_apply trampolines, interleaved with JIT shadow
     * frames) are skipped via the logical→physical mapping. */
    for (li = count - 1; li >= 0; li--) {
        int phys;
        CL_Frame *f;
        CL_Bytecode *bc;
        int line;
        const char *file;
        CL_Obj line_obj;

        phys = bt_physical_frame(base, li);
        if (phys < 0) continue;
        f = &cl_vm.frames[phys];

        /* Read everything off the frame before allocating. */
        name = get_func_name(f->bytecode);
        bc = get_frame_bytecode(f);
        line = bc ? lookup_source_line(bc, f->ip) : 0;
        file = (bc && bc->source_file) ? bc->source_file : NULL;
        line_obj = (line > 0) ? CL_MAKE_FIXNUM(line) : CL_NIL;
        file_str = file ? cl_make_string(file, (uint32_t)strlen(file)) : CL_NIL;

        /* entry = (index name file line) */
        entry = cl_cons(line_obj, CL_NIL);
        entry = cl_cons(file_str, entry);
        entry = cl_cons(name, entry);
        entry = cl_cons(CL_MAKE_FIXNUM(li), entry);
        result = cl_cons(entry, result);
    }

    CL_GC_UNPROTECT(4);
    return result;
}

CL_Obj cl_vm_frame_locals(int index)
{
    int base = bt_resolve_base();
    int phys = bt_physical_frame(base, index);
    CL_Frame *f;
    int bp, n_locals, nargs, i;
    CL_Obj result = CL_NIL, pair = CL_NIL, nm = CL_NIL, val = CL_NIL;

    if (index < 0 || phys < 0)
        return cl_intern_in("NOT-AVAILABLE", 13, cl_package_keyword);

    f = &cl_vm.frames[phys];
    bp = (int)f->bp;
    n_locals = f->n_locals;
    nargs = f->nargs;

    CL_GC_PROTECT(result);
    CL_GC_PROTECT(pair);
    CL_GC_PROTECT(nm);
    CL_GC_PROTECT(val);

    /* Prepend slots in reverse so the list reads slot 0 first.  The slot's
     * value is read AFTER bt_slot_placeholder (which allocates) so it stays
     * consistent with any GC the allocation triggered; the VM value stack is
     * GC-rooted, so the slot itself is always a valid object (NIL-filled at
     * frame entry — never garbage). */
    for (i = n_locals - 1; i >= 0; i--) {
        nm = bt_slot_placeholder(i, i < nargs);
        val = cl_vm.stack[bp + i];
        pair = cl_cons(nm, val);
        result = cl_cons(pair, result);
    }

    CL_GC_UNPROTECT(4);
    return result;
}

/* Check if an NLX frame is stale (its target frame was reused by a different function).
 * This happens when a tail call or return reuses a frame after an NLX frame was established. */
static int nlx_frame_is_stale(CL_NLXFrame *nlx)
{
    CL_Frame *target = &cl_vm.frames[nlx->vm_fp - 1];
    return target->code != nlx->code;
}

/* Validate builtin call arguments — called before the actual fptr dispatch.
 * Separated from call_builtin to keep call_builtin's stack frame tiny
 * (minimizes surface area for stack corruption). */
static void validate_builtin(CL_Function *func, int nargs)
{
    CL_CFunc fptr;
    cl_check_c_stack("call_builtin");
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
    fptr = func->func;
    if (!fptr) {
        cl_error(CL_ERR_TYPE, "NULL C function pointer in %s",
                 CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name));
    }
    {
        uintptr_t fp_val = (uintptr_t)fptr;
#ifdef PLATFORM_AMIGA
        if (fp_val < 0x1000 || (fp_val & 0x1) != 0) {
#else
        if (fp_val < 0x1000 || (fp_val & 0x3) != 0) {
#endif
            fprintf(stderr, "[VM] call_builtin: CORRUPT func ptr %p in %s (obj=0x%08x)\n",
                    (void *)fptr,
                    CL_NULL_P(func->name) ? "?" : cl_symbol_name(func->name),
                    (unsigned)CL_PTR_TO_OBJ(func));
            cl_capture_backtrace();
            fprintf(stderr, "%s", cl_backtrace_buf);
            cl_error(CL_ERR_GENERAL, "Corrupted C function pointer");
        }
    }
}

/* Call a built-in C function.
 * MUST NOT be inlined: fptr calls may use setjmp/longjmp internally
 * (e.g. bi_compile_file), and inlining would put them in cl_vm_run's
 * frame, corrupting its register-cached locals after longjmp.
 *
 * Kept deliberately minimal (no large locals, no cached pointers that
 * survive across the fptr call) to avoid stack-corruption exposure.
 * All validation is done in validate_builtin() before we get here. */
/* Crash diagnostics: last builtin called (survives crashes) */
volatile const char *last_builtin_name = "(none)";
volatile void *last_builtin_fptr = NULL;
volatile CL_Obj last_builtin_obj = 0;

static CL_Obj call_builtin(CL_Function *func, CL_Obj *args, int nargs)
{
    CL_Obj result;
    validate_builtin(func, nargs);
    /* Record for crash diagnostics */
    last_builtin_name = (!CL_NULL_P(func->name) && CL_SYMBOL_P(func->name))
                        ? cl_symbol_name(func->name) : "?";
    last_builtin_fptr = (void *)func->func;
    last_builtin_obj = CL_PTR_TO_OBJ(func);
    /* Save pre-reset mv state so NLX builtins (THROW) can see the
     * multiple values of their last argument after call_builtin's reset. */
    { CL_Thread *t = cl_get_current_thread();
      int mi;
      t->pre_call_mv_count = t->mv_count;
      for (mi = 0; mi < t->mv_count && mi < CL_MAX_MV; mi++)
          t->pre_call_mv_values[mi] = t->mv_values[mi];
      t->mv_count = 1; }
    result = func->func(args, nargs);
    cl_get_current_thread()->mv_values[0] = result;
    return result;
}

#if defined(CL_ASAN_BUILD) || defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
#define C_STACK_LIMIT (64 * 1024 * 1024)  /* ASAN frames are huge — bump cap */
#else
#define C_STACK_LIMIT (3 * 1024 * 1024)  /* 3MB of 8MB, leave 5MB margin */
#endif

void cl_check_c_stack(const char *context)
{
    volatile char probe;
    long used;
#if defined(CL_ASAN_BUILD) || defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
    /* ASan's fake-stack puts each function's locals in a heap-allocated
     * region.  &probe and cl_c_stack_base end up in unrelated mappings,
     * so the simple subtraction returns nonsense values (often >1GB).
     * The check is meaningless under ASan — skip it. */
    (void)probe; (void)used; (void)context;
    return;
#else
    if (!cl_c_stack_base) cl_c_stack_base = (char *)&probe;
    used = (long)(cl_c_stack_base - (char *)&probe);
    if (used < 0) used = -used;  /* Handle stack growing up or down */
#ifdef DEBUG_VM
    if (used > c_stack_max_seen) {
        c_stack_max_seen = used;
        if (c_stack_max_seen > 1024 * 1024)
            fprintf(stderr, "[STACK] %s: %ldKB\n", context, used / 1024);
    }
#endif
    if (used > C_STACK_LIMIT) {
        cl_error(CL_ERR_OVERFLOW,
                 "C stack overflow in %s (used %ldKB, limit %ldKB)",
                 context, used / 1024, (long)C_STACK_LIMIT / 1024);
    }
#endif
}

/* Shared buffers for OP_CALL keyword processing and OP_APPLY argument flattening.
 * Moved out of cl_vm_eval to keep the per-recursion stack frame small.
 *
 * IMPORTANT: vm_extra_args holds CL_Obj values removed from the VM stack
 * during &rest/&key processing.  cl_cons (called to build the &rest list)
 * can trigger GC.  These arrays MUST be GC-rooted — see cl_vm_gc_mark_extra. */
/* vm_extra_args, vm_extra_args_count, vm_flat_args are now in CL_Thread.
 * Local macros above redirect the old names. */

/* GC root marking for VM-internal buffers.
 * Called from gc_mark() in mem.c to mark CL_Obj values held in static
 * arrays that are not on the VM stack (e.g., during &rest list building). */
/* Mark VM extra args for a specific thread (multi-thread GC) */
void cl_vm_gc_mark_extra_thread(CL_Thread *t)
{
    extern void gc_mark_obj(CL_Obj obj);
    int i;
    for (i = 0; i < t->vm_extra_count; i++)
        gc_mark_obj(t->vm_extra_args_buf[i]);
}

/* Legacy wrapper — marks current thread's extra args */
void cl_vm_gc_mark_extra(void)
{
    cl_vm_gc_mark_extra_thread(cl_get_current_thread());
}

/* Update VM extra args during compaction (mirrors gc_mark_extra_thread) */
void cl_vm_gc_update_extra_thread(CL_Thread *t, void (*update)(CL_Obj *))
{
    int i;
    for (i = 0; i < t->vm_extra_count; i++)
        update(&t->vm_extra_args_buf[i]);
}

/* dbg_last_*, vm_trace[], vm_trace_idx are now in CL_Thread.
 * Local macros above redirect the old names. */

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
    cl_check_c_stack("cl_vm_run");

    /* Cache thread pointer once — push/pop/mv_count use it directly,
     * eliminating the cl_get_current_thread() call on each access.
     * CT itself is NOT redefined — only the hot-path accessors are shadowed. */
    CL_Thread *thr = cl_get_current_thread();

    /* Inline push/pop: ~2 function calls + 4 CT lookups saved per opcode.
     * VM_PUSH evaluates (v) into a temp to prevent UB when (v) contains
     * a pop (double sp modification without sequence point). */
#ifdef DEBUG_VM_PUSH_GUARD
#define cl_vm_push(v) do { \
        CL_Obj _pv = (v); \
        if (thr->vm.sp >= (int)thr->vm.stack_size) \
            cl_error(CL_ERR_OVERFLOW, "VM stack overflow"); \
        if (CL_HEAP_P(_pv) && _pv >= cl_heap.arena_size) { \
            fprintf(stderr, \
                "[VM-PUSH-GUARD] dispatch: val=0x%08x out of arena (size=0x%08x) sp=%d fp=%d ip=%u\n", \
                (unsigned)_pv, (unsigned)cl_heap.arena_size, thr->vm.sp, thr->vm.fp, ip); \
            cl_capture_backtrace(); \
            fprintf(stderr, "%s", cl_backtrace_buf); \
            fflush(stderr); \
            abort(); \
        } \
        thr->vm.stack[thr->vm.sp++] = _pv; \
    } while(0)
#else
#define cl_vm_push(v) do { \
        CL_Obj _pv = (v); \
        if (thr->vm.sp >= (int)thr->vm.stack_size) \
            cl_error(CL_ERR_OVERFLOW, "VM stack overflow"); \
        thr->vm.stack[thr->vm.sp++] = _pv; \
    } while(0)
#endif
#define cl_vm_pop() (thr->vm.stack[--thr->vm.sp])

    /* Cache all hot-path thread accessors through local thr pointer.
     * Without this, cl_vm/cl_mv_values/cl_mv_count each call
     * cl_get_current_thread() on every access, which at -O0 is a
     * full function call whose return value can be corrupted by
     * stack memory reuse between the callee's ret and the caller's
     * use of the return register. */
#undef cl_vm
#define cl_vm (thr->vm)
#undef cl_mv_count
#define cl_mv_count (thr->mv_count)
#undef cl_mv_values
#define cl_mv_values (thr->mv_values)

    CL_Frame *frame = &cl_vm.frames[cl_vm.fp - 1];
    uint8_t *code = frame->code;
    CL_Obj *constants = frame->constants;
    uint32_t ip = frame->ip;
    uint8_t op;

    /* ---- Computed goto dispatch (GCC/Clang) ----
     * Eliminates the switch overhead: each opcode handler jumps directly
     * to the next via the dispatch table.  5-15% VM throughput gain on
     * 68020 (no branch-prediction bottleneck through a single indirect). */
#if defined(__GNUC__) && !defined(CL_NO_COMPUTED_GOTO)
#define USE_COMPUTED_GOTO 1
    static void *dispatch_table[256] = {
        [0x00] = &&vm_op_invalid,
        [OP_CONST]       = &&vm_op_OP_CONST,
        [OP_LOAD]        = &&vm_op_OP_LOAD,
        [OP_STORE]       = &&vm_op_OP_STORE,
        [OP_GLOAD]       = &&vm_op_OP_GLOAD,
        [OP_GSTORE]      = &&vm_op_OP_GSTORE,
        [OP_UPVAL]       = &&vm_op_OP_UPVAL,
        [OP_POP]         = &&vm_op_OP_POP,
        [OP_DUP]         = &&vm_op_OP_DUP,
        [OP_CONS]        = &&vm_op_OP_CONS,
        [OP_CAR]         = &&vm_op_OP_CAR,
        [OP_CDR]         = &&vm_op_OP_CDR,
        [OP_ADD]         = &&vm_op_OP_ADD,
        [OP_SUB]         = &&vm_op_OP_SUB,
        [OP_MUL]         = &&vm_op_OP_MUL,
        [OP_DIV]         = &&vm_op_OP_DIV,
        [OP_EQ]          = &&vm_op_OP_EQ,
        [OP_LT]          = &&vm_op_OP_LT,
        [OP_GT]          = &&vm_op_OP_GT,
        [OP_LE]          = &&vm_op_OP_LE,
        [OP_GE]          = &&vm_op_OP_GE,
        [OP_NUMEQ]       = &&vm_op_OP_NUMEQ,
        [OP_NOT]         = &&vm_op_OP_NOT,
        [OP_JMP]         = &&vm_op_OP_JMP,
        [OP_JNIL]        = &&vm_op_OP_JNIL,
        [OP_JTRUE]       = &&vm_op_OP_JTRUE,
        [OP_CALL]        = &&vm_op_OP_CALL,
        [OP_TAILCALL]    = &&vm_op_OP_TAILCALL,
        [OP_RET]         = &&vm_op_OP_RET,
        [OP_CLOSURE]     = &&vm_op_OP_CLOSURE,
        [OP_APPLY]       = &&vm_op_OP_APPLY,
        [OP_LIST]        = &&vm_op_OP_LIST,
        [OP_NIL]         = &&vm_op_OP_NIL,
        [OP_T]           = &&vm_op_OP_T,
        [OP_FLOAD]       = &&vm_op_OP_FLOAD,
        [OP_DEFMACRO]    = &&vm_op_OP_DEFMACRO,
        [OP_CATCH]       = &&vm_op_OP_CATCH,
        [OP_ARGC]        = &&vm_op_OP_ARGC,
        [OP_UNCATCH]     = &&vm_op_OP_UNCATCH,
        [OP_UWPROT]      = &&vm_op_OP_UWPROT,
        [OP_UWPOP]       = &&vm_op_OP_UWPOP,
        [OP_UWRETHROW]   = &&vm_op_OP_UWRETHROW,
        [OP_MV_LOAD]     = &&vm_op_OP_MV_LOAD,
        [OP_MV_TO_LIST]  = &&vm_op_OP_MV_TO_LIST,
        [OP_NTH_VALUE]   = &&vm_op_OP_NTH_VALUE,
        [OP_DYNBIND]     = &&vm_op_OP_DYNBIND,
        [OP_DYNUNBIND]   = &&vm_op_OP_DYNUNBIND,
        [OP_RPLACA]      = &&vm_op_OP_RPLACA,
        [OP_RPLACD]      = &&vm_op_OP_RPLACD,
        [OP_ASET]        = &&vm_op_OP_ASET,
        [OP_DEFTYPE]     = &&vm_op_OP_DEFTYPE,
        [OP_HANDLER_PUSH] = &&vm_op_OP_HANDLER_PUSH,
        [OP_HANDLER_POP]  = &&vm_op_OP_HANDLER_POP,
        [OP_RESTART_PUSH] = &&vm_op_OP_RESTART_PUSH,
        [OP_RESTART_POP]  = &&vm_op_OP_RESTART_POP,
        [OP_ASSERT_TYPE]  = &&vm_op_OP_ASSERT_TYPE,
        [OP_BLOCK_PUSH]   = &&vm_op_OP_BLOCK_PUSH,
        [OP_BLOCK_POP]    = &&vm_op_OP_BLOCK_POP,
        [OP_BLOCK_RETURN] = &&vm_op_OP_BLOCK_RETURN,
        [OP_FSTORE]       = &&vm_op_OP_FSTORE,
        [OP_MAKE_CELL]    = &&vm_op_OP_MAKE_CELL,
        [OP_CELL_REF]     = &&vm_op_OP_CELL_REF,
        [OP_CELL_SET_LOCAL]  = &&vm_op_OP_CELL_SET_LOCAL,
        [OP_CELL_SET_UPVAL]  = &&vm_op_OP_CELL_SET_UPVAL,
        [OP_TAGBODY_PUSH] = &&vm_op_OP_TAGBODY_PUSH,
        [OP_TAGBODY_POP]  = &&vm_op_OP_TAGBODY_POP,
        [OP_TAGBODY_GO]   = &&vm_op_OP_TAGBODY_GO,
        [OP_PROGV_BIND]   = &&vm_op_OP_PROGV_BIND,
        [OP_PROGV_UNBIND] = &&vm_op_OP_PROGV_UNBIND,
        [OP_DEFSETF]      = &&vm_op_OP_DEFSETF,
        [OP_DEFVAR]       = &&vm_op_OP_DEFVAR,
        [OP_MV_RESET]     = &&vm_op_OP_MV_RESET,
        [OP_AMIGA_CALL]   = &&vm_op_OP_AMIGA_CALL,
        [OP_STRUCT_REF]   = &&vm_op_OP_STRUCT_REF,
        [OP_STRUCT_SET]   = &&vm_op_OP_STRUCT_SET,
        [OP_HALT]         = &&vm_op_OP_HALT,
    };

    /* Fill uninitialized slots with unknown handler */
    {
        static int table_initialized = 0;
        if (!table_initialized) {
            int ti;
            for (ti = 0; ti < 256; ti++)
                if (!dispatch_table[ti])
                    dispatch_table[ti] = &&vm_op_unknown;
            table_initialized = 1;
        }
    }

#ifdef PROFILE_OPCODES
#define CL_OPCOUNT_TICK(op) (cl_op_counts_arr[(uint8_t)(op)]++)
#else
#define CL_OPCOUNT_TICK(op) ((void)0)
#endif

#define VM_DISPATCH() do { \
        extern int gc_compact_pending; \
        if (gc_compact_pending) { \
            frame->ip = ip; \
            cl_gc_compact_if_pending(); \
        } \
        op = code[ip++]; \
        CL_OPCOUNT_TICK(op); \
        if (__builtin_expect(!dispatch_table[op], 0)) { \
            fprintf(stderr, "[VM] NULL dispatch for op=0x%02x ip=%u fp=%d\n", op, ip-1, cl_vm.fp); \
            cl_capture_backtrace(); fprintf(stderr, "%s", cl_backtrace_buf); fflush(stderr); abort(); \
        } \
        goto *dispatch_table[op]; \
    } while(0)
#define VM_CASE(opname) vm_op_##opname
#define VM_BREAK VM_DISPATCH()
#define VM_DEFAULT vm_op_unknown

    /* Initial dispatch (skip compaction check on first op) */
    op = code[ip++];
    CL_OPCOUNT_TICK(op);
    if (__builtin_expect(!dispatch_table[op], 0)) {
        fprintf(stderr, "[VM] NULL initial dispatch for op=0x%02x ip=%u fp=%d\n", op, ip-1, cl_vm.fp);
        cl_capture_backtrace(); fprintf(stderr, "%s", cl_backtrace_buf); fflush(stderr); abort();
    }
    goto *dispatch_table[op];

#else /* Standard switch dispatch */

#define VM_DISPATCH() break
#define VM_CASE(opname) case opname
#define VM_BREAK break
#define VM_DEFAULT default

    for (;;) {

        /* Safe point: run pending compaction between opcodes.
         * All CL_Obj values are on the VM stack (updated during compaction).
         * code/constants point to platform_alloc'd memory (unaffected). */
        {
            extern int gc_compact_pending;
            if (gc_compact_pending) {
                frame->ip = ip;
                cl_gc_compact_if_pending();
            }
        }

        /* Record trace for crash diagnostics (hot path — debug only) */
#ifdef DEBUG_VM
        vm_trace[vm_trace_idx].op = code ? code[ip] : 0;
        vm_trace[vm_trace_idx].ip = ip;
        vm_trace[vm_trace_idx].fp = cl_vm.fp;
        vm_trace[vm_trace_idx].sp = cl_vm.sp;
        vm_trace[vm_trace_idx].code = code;
        vm_trace_idx = (vm_trace_idx + 1) % VM_TRACE_SIZE;
#endif

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
        CL_OPCOUNT_TICK(op);

        /* Trap opcode 0 explicitly */
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

        /* CRASH HUNT: check if we're about to dispatch CLASS-SLOT-INDEX-TABLE at fp=45 */
        if (cl_vm.fp == 45 && ip == 0) {
            CL_Bytecode *_chk = NULL;
            if (CL_BYTECODE_P(frame->bytecode))
                _chk = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
            else if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *_cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                if (CL_BYTECODE_P(_cc->bytecode))
                    _chk = (CL_Bytecode *)CL_OBJ_TO_PTR(_cc->bytecode);
            }
            if (_chk && _chk->name != CL_NIL && CL_SYMBOL_P(_chk->name)) {
                const char *_fn = cl_symbol_name(_chk->name);
                if (_fn && _fn[0] == 'C' && _fn[5] == '-' && _fn[6] == 'S') {
                    fprintf(stderr, "[CRASH-HUNT] About to dispatch fp=%d ip=0 fn=%s code=%p code[0]=0x%02x thr=%p\n",
                            cl_vm.fp, _fn, (void *)code, (unsigned)code[0], (void *)thr);
                    fprintf(stderr, "[CRASH-HUNT] frame=%p frame->code=%p frame->bytecode=0x%08x base_fp=%d\n",
                            (void *)frame, (void *)frame->code, (unsigned)frame->bytecode, base_fp);
                    fflush(stderr);
                }
            }
        }

        /* Save last dispatched opcode for crash diagnostics (debug only) */
#ifdef DEBUG_VM
        dbg_last_op = op;
        dbg_last_ip = ip;
        dbg_last_fp = cl_vm.fp;
        dbg_last_code = code;

        /* Targeted: dump state when dispatching CLASS-SLOT-INDEX-TABLE */
        {
            CL_Bytecode *_fbc = NULL;
            if (CL_BYTECODE_P(frame->bytecode))
                _fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(frame->bytecode);
            else if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *_cc = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                if (CL_BYTECODE_P(_cc->bytecode))
                    _fbc = (CL_Bytecode *)CL_OBJ_TO_PTR(_cc->bytecode);
            }
            if (_fbc && _fbc->name != CL_NIL && CL_SYMBOL_P(_fbc->name)) {
                const char *_fn = cl_symbol_name(_fbc->name);
                if (_fn && _fn[0] == 'C' && _fn[5] == '-' && _fn[6] == 'S' && _fn[10] == '-') {
                    fprintf(stderr, "[CSIT-DISP] op=0x%02x ip=%u fp=%d fn=%s code=%p frame_code=%p base_fp=%d\n",
                            op, ip-1, cl_vm.fp, _fn, (void *)code, (void *)frame->code, base_fp);
                    fflush(stderr);
                }
            }
        }
#endif
        /* [TRACE] suppressed */

        if (op == 0x00) goto unknown_opcode;
        switch (op) {
        case 0x00:
            goto unknown_opcode;

#endif /* USE_COMPUTED_GOTO */

    /* ---- Invalid opcode 0 (computed goto target) ---- */
#ifdef USE_COMPUTED_GOTO
    vm_op_invalid:
        op = code[ip - 1];
        goto vm_op_unknown;
#endif

        VM_CASE(OP_HALT): {
            CL_Obj result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
                            ? cl_vm_pop() : CL_NIL;
            /* Restore stack and NLX to before this run */
            cl_vm.sp = frame->bp;
            cl_vm.fp = base_fp;
            cl_nlx_top = base_nlx;
            return result;
        }

        VM_CASE(OP_CONST): {
            uint16_t idx = read_u16(code, &ip);
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_CONST with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_error(CL_ERR_GENERAL, "OP_CONST with NULL constants ptr");
            }
            cl_vm_push(constants[idx]);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_NIL):
            cl_vm_push(CL_NIL);
            cl_mv_count = 1;
            VM_BREAK;

        VM_CASE(OP_T):
            cl_vm_push(SYM_T);
            cl_mv_count = 1;
            VM_BREAK;

        VM_CASE(OP_LOAD): {
            uint8_t slot = code[ip++];
            cl_vm_push(cl_vm.stack[frame->bp + slot]);
            VM_BREAK;
        }

        VM_CASE(OP_STORE): {
            uint8_t slot = code[ip++];
            cl_vm.stack[frame->bp + slot] = cl_vm.stack[cl_vm.sp - 1];
            DBG_CHECK_WATCH("OP_STORE");
            VM_BREAK;
        }

        VM_CASE(OP_GLOAD): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym;
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_GLOAD with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_capture_backtrace();
                fprintf(stderr, "%s", cl_backtrace_buf);
                cl_error(CL_ERR_GENERAL, "OP_GLOAD with NULL constants ptr");
            }
            sym = constants[idx];
            CL_Obj val = cl_symbol_value(sym);
            if (val == CL_UNBOUND)
                cl_error(CL_ERR_UNBOUND, "Unbound variable: %s",
                         cl_symbol_name(sym));
            cl_vm_push(val);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_GSTORE): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym;
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_GSTORE with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_error(CL_ERR_GENERAL, "OP_GSTORE with NULL constants ptr");
            }
            sym = constants[idx];
            cl_set_symbol_value(sym, cl_vm.stack[cl_vm.sp - 1]);
            if (sym == SYM_STAR_PACKAGE)
                cl_sync_current_package_from_dynamic();
            VM_BREAK;
        }

        VM_CASE(OP_FLOAD): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym;
            if (!constants) {
                fprintf(stderr, "[VM] BUG: OP_FLOAD with NULL constants (idx=%u fp=%d ip=%u)\n",
                        idx, cl_vm.fp, ip);
                cl_error(CL_ERR_GENERAL, "OP_FLOAD with NULL constants ptr");
            }
            sym = constants[idx];
            /* Validate sym is a valid symbol before dereferencing */
            if (!CL_HEAP_P(sym) || sym >= cl_heap.arena_size ||
                CL_HDR_TYPE(CL_OBJ_TO_PTR(sym)) != TYPE_SYMBOL) {
                fprintf(stderr, "[VM] FLOAD: constant[%d] = 0x%08x is NOT a symbol "
                        "(heap=%d, arena_bound=%d, type=%d) fp=%d ip=%u bc=0x%08x\n",
                        idx, (unsigned)sym, CL_HEAP_P(sym),
                        (sym < cl_heap.arena_size),
                        (CL_HEAP_P(sym) && sym < cl_heap.arena_size)
                            ? CL_HDR_TYPE(CL_OBJ_TO_PTR(sym)) : -1,
                        cl_vm.fp, ip, (unsigned)frame->bytecode);
                cl_capture_backtrace();
                fprintf(stderr, "%s", cl_backtrace_buf);
                cl_error(CL_ERR_GENERAL, "OP_FLOAD: corrupted constant pool entry");
            }
            {
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            if (s->function != CL_UNBOUND) {
                CL_Obj fval = s->function;
                /* Validate function value is a sane heap object */
                if (CL_HEAP_P(fval) && fval >= cl_heap.arena_size) {
                    fprintf(stderr, "[VM] FLOAD: symbol %s function=0x%08x out of arena (size=0x%08x)\n",
                            cl_symbol_name(sym), (unsigned)fval, (unsigned)cl_heap.arena_size);
                    cl_capture_backtrace();
                    fprintf(stderr, "%s", cl_backtrace_buf);
                    cl_error(CL_ERR_GENERAL, "OP_FLOAD: corrupted function binding");
                }
                cl_vm_push(fval);
            } else if (cl_symbol_value(sym) != CL_UNBOUND) {
                /* Fall back to value slot (for labels/flet value bindings) */
                cl_vm_push(cl_symbol_value(sym));
            } else {
                cl_error(CL_ERR_UNDEFINED, "Undefined function: %s",
                         cl_symbol_name(sym));
            }
            cl_mv_count = 1;
            }
            VM_BREAK;
        }

        VM_CASE(OP_FSTORE): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(sym);
            s->function = cl_vm.stack[cl_vm.sp - 1];
            VM_BREAK;
        }

        VM_CASE(OP_MAKE_CELL): {
            CL_Obj val = cl_vm_pop();
            cl_vm_push(cl_make_cell(val));
            VM_BREAK;
        }

        VM_CASE(OP_CELL_REF): {
            CL_Obj cell_obj = cl_vm_pop();
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cl_vm_push(cell->value);
            VM_BREAK;
        }

        VM_CASE(OP_CELL_SET_LOCAL): {
            uint8_t slot = code[ip++];
            CL_Obj cell_obj = cl_vm.stack[frame->bp + slot];
            CL_Cell *cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
            VM_BREAK;
        }

        VM_CASE(OP_CELL_SET_UPVAL): {
            uint8_t index = code[ip++];
            CL_Obj cell_obj;
            CL_Cell *cell;
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cell_obj = cl->upvalues[index];
            } else {
                VM_BREAK; /* shouldn't happen */
            }
            if (!CL_CELL_P(cell_obj)) {
                cl_error(CL_ERR_TYPE, "OP_CELL_SET_UPVAL: upvalue[%u] is not a cell (internal compiler error)", (unsigned)index);
            }
            cell = (CL_Cell *)CL_OBJ_TO_PTR(cell_obj);
            cell->value = cl_vm.stack[cl_vm.sp - 1]; /* peek */
            VM_BREAK;
        }

        VM_CASE(OP_UPVAL): {
            /* Flat upvalue access: single index into closure's upvalues[] */
            uint8_t index = code[ip++];
            if (CL_CLOSURE_P(frame->bytecode)) {
                CL_Closure *cl = (CL_Closure *)CL_OBJ_TO_PTR(frame->bytecode);
                cl_vm_push(cl->upvalues[index]);
            } else {
                cl_vm_push(CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_POP):
            cl_vm_pop();
            VM_BREAK;

        VM_CASE(OP_DUP):
            cl_vm_push(cl_vm.stack[cl_vm.sp - 1]);
            VM_BREAK;

        VM_CASE(OP_CONS): {
            CL_Obj cdr_val = cl_vm_pop();
            CL_Obj car_val = cl_vm_pop();
            cl_vm_push(cl_cons(car_val, cdr_val));
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_CAR): {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_car(obj));
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_CDR): {
            CL_Obj obj = cl_vm_pop();
            cl_vm_push(cl_cdr(obj));
            cl_mv_count = 1;
            VM_BREAK;
        }

        /* Arithmetic opcodes: fixnum fast path inline (no function call,
         * no extra type-check) with cl_arith_* fallback for bignum /
         * float / ratio / complex.  Slow path validates operands since
         * cl_arith_* dereferences non-number heap pointers — these
         * opcodes are emitted by the call-site inliner so the old
         * "trusted by special-form expander" contract no longer holds.
         *
         * Same pattern as OP_LT/OP_GT/etc. above. */
        VM_CASE(OP_ADD): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                int32_t sum = CL_FIXNUM_VAL(a) + CL_FIXNUM_VAL(b);
                if (sum >= CL_FIXNUM_MIN && sum <= CL_FIXNUM_MAX) {
                    cl_vm_push(CL_MAKE_FIXNUM(sum));
                    cl_mv_count = 1;
                    VM_BREAK;
                }
            }
            if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "+");
            if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "+");
            cl_vm_push(cl_arith_add(a, b));
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_SUB): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                int32_t diff = CL_FIXNUM_VAL(a) - CL_FIXNUM_VAL(b);
                if (diff >= CL_FIXNUM_MIN && diff <= CL_FIXNUM_MAX) {
                    cl_vm_push(CL_MAKE_FIXNUM(diff));
                    cl_mv_count = 1;
                    VM_BREAK;
                }
            }
            if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "-");
            if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "-");
            cl_vm_push(cl_arith_sub(a, b));
            cl_mv_count = 1;
            VM_BREAK;
        }

        /* MUL fast path: only when both magnitudes fit in 15 bits, so
         * the 32-bit product is guaranteed to fit in a 31-bit fixnum
         * with no overflow check. Wider operands fall through to
         * cl_arith_mul, which has its own thorough overflow handling. */
        VM_CASE(OP_MUL): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                int32_t va = CL_FIXNUM_VAL(a);
                int32_t vb = CL_FIXNUM_VAL(b);
                int32_t aa = va < 0 ? -va : va;
                int32_t ab = vb < 0 ? -vb : vb;
                if ((aa | ab) < 0x8000) {
                    cl_vm_push(CL_MAKE_FIXNUM(va * vb));
                    cl_mv_count = 1;
                    VM_BREAK;
                }
            }
            if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "*");
            if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "*");
            cl_vm_push(cl_arith_mul(a, b));
            cl_mv_count = 1;
            VM_BREAK;
        }

        /* DIV fast path: both fixnums, exact division (no remainder),
         * b non-zero, and not the FIXNUM_MIN/-1 overflow case.  Inexact
         * results would need a ratio object, which isn't worth inlining
         * — fall through to cl_arith_div. */
        VM_CASE(OP_DIV): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                int32_t va = CL_FIXNUM_VAL(a);
                int32_t vb = CL_FIXNUM_VAL(b);
                if (vb != 0 && (va % vb) == 0 &&
                    !(va == CL_FIXNUM_MIN && vb == -1)) {
                    cl_vm_push(CL_MAKE_FIXNUM(va / vb));
                    cl_mv_count = 1;
                    VM_BREAK;
                }
            }
            if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "/");
            if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "/");
            cl_vm_push(cl_arith_div(a, b));
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_EQ): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            cl_vm_push(a == b ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            VM_BREAK;
        }

        /* Comparison opcodes: fixnum fast path (single int compare, no
         * function call) with cl_arith_compare fallback for bignums /
         * floats / ratios.  Slow path validates types since
         * cl_arith_compare assumes its operands are numbers — the
         * inliner now emits these for user calls so the old "expander
         * has already type-checked" contract no longer holds. */
        VM_CASE(OP_NUMEQ): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                cl_vm_push(a == b ? SYM_T : CL_NIL);
            } else {
                if (!CL_NUMBER_P(a)) cl_signal_type_error(a, "NUMBER", "=");
                if (!CL_NUMBER_P(b)) cl_signal_type_error(b, "NUMBER", "=");
                cl_vm_push(cl_numeric_equal(a, b) ? SYM_T : CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        /* Ordered comparators reject complex per CLHS 12.1.4.1. */
        VM_CASE(OP_LT): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                cl_vm_push(CL_FIXNUM_VAL(a) < CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            } else {
                if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", "<");
                if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", "<");
                cl_vm_push(cl_arith_compare(a, b) < 0 ? SYM_T : CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_GT): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                cl_vm_push(CL_FIXNUM_VAL(a) > CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            } else {
                if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", ">");
                if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", ">");
                cl_vm_push(cl_arith_compare(a, b) > 0 ? SYM_T : CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_LE): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                cl_vm_push(CL_FIXNUM_VAL(a) <= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            } else {
                if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", "<=");
                if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", "<=");
                cl_vm_push(cl_arith_compare(a, b) <= 0 ? SYM_T : CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_GE): {
            CL_Obj b = cl_vm_pop(), a = cl_vm_pop();
            if (CL_FIXNUM_P(a) && CL_FIXNUM_P(b)) {
                cl_vm_push(CL_FIXNUM_VAL(a) >= CL_FIXNUM_VAL(b) ? SYM_T : CL_NIL);
            } else {
                if (!CL_REALP(a)) cl_signal_type_error(a, "REAL", ">=");
                if (!CL_REALP(b)) cl_signal_type_error(b, "REAL", ">=");
                cl_vm_push(cl_arith_compare(a, b) >= 0 ? SYM_T : CL_NIL);
            }
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_NOT): {
            CL_Obj a = cl_vm_pop();
            cl_vm_push(CL_NULL_P(a) ? SYM_T : CL_NIL);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_JMP): {
            int32_t offset = read_i32(code, &ip);
            if (offset < 0) CL_SAFEPOINT();  /* backward jump = loop body */
            ip += offset;
            VM_BREAK;
        }

        VM_CASE(OP_JNIL): {
            int32_t offset = read_i32(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (CL_NULL_P(val)) ip += offset;
            VM_BREAK;
        }

        VM_CASE(OP_JTRUE): {
            int32_t offset = read_i32(code, &ip);
            CL_Obj val = cl_vm_pop();
            if (!CL_NULL_P(val)) ip += offset;
            VM_BREAK;
        }

        VM_CASE(OP_LIST): {
            uint8_t n = code[ip++];
            CL_Obj list = CL_NIL;
            int i;
            /* Build list from stack (last element is on top) */
            for (i = 0; i < n; i++) {
                list = cl_cons(cl_vm_pop(), list);
            }
            cl_vm_push(list);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_CALL):
        VM_CASE(OP_TAILCALL): {
            uint8_t nargs = code[ip++];
            int is_tail = (op == OP_TAILCALL);
            CL_SAFEPOINT();
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

            /* Unwrap funcallable instances (GF struct → discriminating fn). */
            if (cl_funcallable_instance_p(func_obj)) {
                func_obj = cl_unwrap_funcallable(func_obj);
                cl_vm.stack[cl_vm.sp - nargs - 1] = func_obj;
            }

            /* Bounds-check before type predicate macros dereference func_obj */
            if (CL_HEAP_P(func_obj) && func_obj >= cl_heap.arena_size) {
                fprintf(stderr, "[VM] CALL: func_obj=0x%08x out of arena (size=0x%08x) nargs=%d fp=%d ip=%u\n",
                        (unsigned)func_obj, (unsigned)cl_heap.arena_size, nargs, cl_vm.fp, ip);
                cl_capture_backtrace();
                fprintf(stderr, "%s", cl_backtrace_buf);
                cl_error(CL_ERR_GENERAL, "OP_CALL: corrupted function object (out of arena)");
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
                    /* Bounds-check bytecode field before type predicate dereferences it */
                    if (CL_HEAP_P(cl->bytecode) && cl->bytecode >= cl_heap.arena_size) {
                        fprintf(stderr, "[VM] CALL: closure 0x%08x bytecode=0x%08x out of arena (size=0x%08x)\n",
                                (unsigned)func_obj, (unsigned)cl->bytecode, (unsigned)cl_heap.arena_size);
                        cl_capture_backtrace();
                        fprintf(stderr, "%s", cl_backtrace_buf);
                        cl_error(CL_ERR_GENERAL, "OP_CALL: closure bytecode out of arena");
                    }
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
                    CL_Obj fname = callee_bc->name;
                    const char *fn = (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                     ? cl_symbol_name(fname) : "<lambda>";
                    const char *src = callee_bc->source_file
                                      ? callee_bc->source_file : "?";
                    unsigned src_line = callee_bc->source_line;
                    cl_error(CL_ERR_ARGS,
                             "Too few arguments to %s (%s:%u): expected %s%d, got %d",
                             fn, src, src_line,
                             (n_opt || has_rest || has_key) ? "at least " : "",
                             min_args, nargs);
                }
                if (nargs > max_args) {
                    CL_Obj fname = callee_bc->name;
                    const char *fn = (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                     ? cl_symbol_name(fname) : "<lambda>";
                    const char *src = callee_bc->source_file
                                      ? callee_bc->source_file : "?";
                    unsigned src_line = callee_bc->source_line;
                    cl_error(CL_ERR_ARGS,
                             "Too many arguments to %s (%s:%u): expected %s%d, got %d",
                             fn, src, src_line, n_opt ? "at most " : "",
                             max_args, nargs);
                }

                /* Native fast path (m68k JIT, opt-in via cl_jit_compile).
                 * Only bytecodes the JIT recognized as safe-to-replace
                 * carry native_code; today that's trivial-literal-leaf
                 * (0-arg constant return) and 1-arg identity.  Both
                 * allocate nothing and return their result in D0.
                 * Side-steps frame push and goes straight to
                 * result-push, mirroring the builtin dispatch above.
                 * Skipped under TAILCALL (would confuse the existing
                 * TAILCALL frame reuse) and when the callee is being
                 * traced (so TRACE output stays faithful).  Arity
                 * checks above have already enforced nargs == arity for
                 * these shapes (matchers reject optional/&key/&rest),
                 * so cl_jit_invoke can dispatch on nargs unconditionally. */
                if (callee_bc->native_code &&
                    !is_tail && !is_func_traced(func_obj)) {
                    /* Pass the function-object CL_Obj (closure or raw
                     * bytecode) alongside its unwrapped bytecode so JIT'd
                     * code reaches the closure's upvalues[] via OP_UPVAL
                     * / OP_CELL_SET_UPVAL.  Mirrors the VM's
                     * frame->bytecode channel — kept as a separate C arg
                     * because the dispatch needs the resolved bytecode
                     * pointer immediately. */
                    CL_Obj nresult = cl_jit_invoke(func_obj, callee_bc, nargs);
                    cl_vm.sp -= (nargs + 1);
                    cl_vm_push(nresult);
                    VM_BREAK;
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
                        vm_extra_args_count = n_extra;
                    }

                    /* Handle &rest */
                    if (has_rest) {
                        CL_Obj rest = CL_NIL;
                        int j;
                        for (j = n_extra - 1; j >= 0; j--)
                            rest = cl_cons(vm_extra_args[j], rest);
                        cl_vm_push(rest);

                        /* Re-derive callee_bc: cl_cons() above may trigger GC
                         * compaction which moves arena objects.  func_obj was
                         * GC-protected so its CL_Obj was updated, but the raw
                         * C pointer callee_bc still points to the OLD arena
                         * location (now potentially reused by new allocations). */
                        if (CL_CLOSURE_P(func_obj)) {
                            CL_Closure *cl2 = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl2->bytecode);
                        } else {
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(func_obj);
                        }
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(frame->bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* CLHS 3.4.1.4: signals program-error on odd kwarg count */
                        if (n_extra & 1)
                            cl_error(CL_ERR_ARGS,
                                     "odd number of keyword arguments");
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
                        /* Iterate right-to-left so the leftmost duplicate
                         * keyword wins (CLHS 3.4.1.4.1). */
                        {
                            int last_ki = n_extra - 2;
                            if (last_ki & 1) last_ki--;
                            for (ki = last_ki; ki >= 0; ki -= 2) {
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
                    }

                    vm_extra_args_count = 0;
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

                    /* Targeted crash diagnostic: tailcall path */
                    if (0 && cl_vm.fp >= 44) {
                        const char *_fn = "?";
                        if (callee_bc->name != CL_NIL && CL_SYMBOL_P(callee_bc->name))
                            _fn = cl_symbol_name(callee_bc->name);
                        fprintf(stderr, "[DIAG-FP45] TAILCALL fp=%d fn=%s code=%p\n",
                                cl_vm.fp, _fn, (void *)code);
                        fflush(stderr);
                    }

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
                        vm_extra_args_count = n_extra;
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

                        /* Re-derive callee_bc: cl_cons() above may trigger GC
                         * compaction which moves arena objects.  func_obj was
                         * GC-protected so its CL_Obj was updated, but the raw
                         * C pointer callee_bc still points to the OLD arena
                         * location (now potentially reused by new allocations). */
                        if (CL_CLOSURE_P(func_obj)) {
                            CL_Closure *cl2 = (CL_Closure *)CL_OBJ_TO_PTR(func_obj);
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl2->bytecode);
                        } else {
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(func_obj);
                        }
                    }

                    /* Fill remaining locals with NIL (including key slots) */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* CLHS 3.4.1.4: signals program-error on odd kwarg count */
                        if (n_extra & 1)
                            cl_error(CL_ERR_ARGS,
                                     "odd number of keyword arguments");
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
                        /* Right-to-left so leftmost duplicate keyword wins. */
                        {
                            int last_ki = n_extra - 2;
                            if (last_ki & 1) last_ki--;
                            for (ki = last_ki; ki >= 0; ki -= 2) {
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
                    }

                    vm_extra_args_count = 0;
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
                    new_frame->nlx_level = cl_nlx_top;

                    frame = new_frame;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;

                    /* Targeted crash diagnostic: detect frame push at fp=44→45 */
                    if (0 && cl_vm.fp >= 44) {
                        const char *_fn = "?";
                        if (callee_bc->name != CL_NIL && CL_SYMBOL_P(callee_bc->name))
                            _fn = cl_symbol_name(callee_bc->name);
                        fprintf(stderr, "[DIAG-FP45] NORMAL-CALL fp=%d→%d fn=%s code=%p\n",
                                cl_vm.fp - 1, cl_vm.fp, _fn, (void *)code);
                        fflush(stderr);
                    }

                    /* Validate: code pointer must be valid */
                    if (!code) {
                        CL_Obj fname = callee_bc->name;
                        fprintf(stderr, "[VM] BUG: bytecode '%s' has NULL code ptr (bc=0x%08x)\n",
                                (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                (unsigned)func_obj);
                        cl_capture_backtrace();
                        fprintf(stderr, "%s", cl_backtrace_buf);
                        cl_error(CL_ERR_GENERAL, "Bytecode has NULL code pointer");
                    }
                    /* Validate: first byte of code should be a valid opcode.
                     * If code was freed, reading it may crash. Use a signal-safe
                     * probe: read the first byte and check it's a known opcode. */
                    {
                        uint8_t first_op = code[0]; /* may crash here if freed */
                        if (first_op == 0x00 && callee_bc->code_len > 1) {
                            CL_Obj fname = callee_bc->name;
                            fprintf(stderr,
                                "[VM] WARNING: bytecode '%s' starts with opcode 0x00 "
                                "(code=%p code_len=%u bc=0x%08x)\n",
                                (!CL_NULL_P(fname) && CL_SYMBOL_P(fname))
                                    ? cl_symbol_name(fname) : "<anon>",
                                (void *)code, callee_bc->code_len,
                                (unsigned)func_obj);
                            cl_capture_backtrace();
                            fprintf(stderr, "%s", cl_backtrace_buf);
                            fflush(stderr);
                        }
                    }

                    /* Validate: bytecode object must still be valid (not GC'd and reused) */
                    {
                        CL_Obj bc_obj = CL_CLOSURE_P(func_obj)
                            ? ((CL_Closure *)CL_OBJ_TO_PTR(func_obj))->bytecode
                            : func_obj;
                        if (!CL_BYTECODE_P(bc_obj)) {
                            fprintf(stderr,
                                "[VM] BUG: bytecode 0x%08x type=%u (expected BYTECODE=%u) "
                                "— GC swept/reused? fn=%s code=%p\n",
                                (unsigned)bc_obj,
                                (unsigned)CL_HDR_TYPE(CL_OBJ_TO_PTR(bc_obj)),
                                (unsigned)TYPE_BYTECODE,
                                callee_bc->name != CL_NIL && CL_SYMBOL_P(callee_bc->name)
                                    ? cl_symbol_name(callee_bc->name) : "<anon>",
                                (void *)callee_bc->code);
                            cl_capture_backtrace();
                            fprintf(stderr, "%s", cl_backtrace_buf);
                            cl_error(CL_ERR_GENERAL,
                                     "Bytecode object type changed — likely GC'd and reused");
                        }
                    }

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
                    /* Extra safety: if first opcode needs constants but ptr is NULL,
                     * catch it before we SIGSEGV.  This also catches corruption where
                     * n_constants was zeroed but code actually uses constants. */
                    if (!constants && code) {
                        uint8_t first_op = code[0];
                        if (first_op == OP_CONST || first_op == OP_FLOAD ||
                            first_op == OP_GLOAD || first_op == OP_GSTORE ||
                            first_op == OP_DYNBIND || first_op == OP_FSTORE ||
                            first_op == OP_CLOSURE) {
                            CL_Obj fname = callee_bc->name;
                            fprintf(stderr, "[VM] BUG: bytecode '%s' first_op=0x%02x needs constants but constants=NULL n_constants=%d\n",
                                    (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                    first_op, callee_bc->n_constants);
                            fprintf(stderr, "[VM]   bytecode=0x%08x code=%p code_len=%u\n",
                                    (unsigned)func_obj, (void *)code, callee_bc->code_len);
                            cl_capture_backtrace();
                            fprintf(stderr, "%s", cl_backtrace_buf);
                            cl_error(CL_ERR_GENERAL, "Bytecode %s: NULL constants but first op (0x%02x) needs them",
                                     (!CL_NULL_P(fname) && CL_SYMBOL_P(fname)) ? cl_symbol_name(fname) : "<anon>",
                                     first_op);
                        }
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
            VM_BREAK;
        }

        VM_CASE(OP_RET): {
            CL_Obj result;
            result = (cl_vm.sp > (int)(frame->bp + frame->n_locals))
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

            /* CRASH HUNT: check before/after push when returning from CLASS-SLOT-INDEX-TABLE at fp >= 44 */
            /* [RET-DIAG] suppressed */

            cl_vm_push(result);
            VM_BREAK;
        }

        VM_CASE(OP_CLOSURE): {
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
            VM_BREAK;
        }

        VM_CASE(OP_DEFMACRO): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_macro(name, expander);
            cl_vm_push(name);
            VM_BREAK;
        }

        VM_CASE(OP_DEFTYPE): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name = constants[idx];
            CL_Obj expander = cl_vm_pop();
            cl_register_type(name, expander);
            cl_vm_push(name);
            VM_BREAK;
        }

        VM_CASE(OP_DEFSETF): {
            extern CL_Obj setf_table;
            extern void *cl_tables_rwlock;
            uint16_t acc_idx = read_u16(code, &ip);
            uint16_t upd_idx = read_u16(code, &ip);
            CL_Obj accessor = constants[acc_idx];
            CL_Obj updater  = constants[upd_idx];
            {
                CL_Obj pair = cl_cons(accessor, updater);
                cl_tables_wrlock();
                setf_table = cl_cons(pair, setf_table);
                cl_tables_rwunlock();
            }
            cl_vm_push(accessor);
            VM_BREAK;
        }

        VM_CASE(OP_DEFVAR): {
            uint16_t sym_idx = read_u16(code, &ip);
            CL_Obj sym_obj = constants[sym_idx];
            CL_Symbol *sym = (CL_Symbol *)CL_OBJ_TO_PTR(sym_obj);
            CL_Obj val = cl_vm_pop();
            sym->flags |= CL_SYM_SPECIAL;
            if (sym->value == CL_UNBOUND)
                sym->value = val;
            VM_BREAK;
        }

        VM_CASE(OP_HANDLER_PUSH): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj type_sym = constants[idx];
            CL_Obj handler = cl_vm_pop();

            if (cl_handler_top >= CL_MAX_HANDLER_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Handler stack overflow");

            cl_handler_stack[cl_handler_top].type_name = type_sym;
            cl_handler_stack[cl_handler_top].handler = handler;
            cl_handler_stack[cl_handler_top].handler_mark = cl_handler_top;
            cl_handler_top++;
            VM_BREAK;
        }

        VM_CASE(OP_HANDLER_POP): {
            uint8_t count = code[ip++];
            cl_handler_top -= count;
            if (cl_handler_top < 0) cl_handler_top = 0;
            VM_BREAK;
        }

        VM_CASE(OP_RESTART_PUSH): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj name_sym = constants[idx];
            /* compile_restart_case pushes: handler, report, interactive,
             * test, tag — pop in reverse. */
            CL_Obj tag = cl_vm_pop();
            CL_Obj test = cl_vm_pop();
            CL_Obj interactive = cl_vm_pop();
            CL_Obj report = cl_vm_pop();
            CL_Obj handler = cl_vm_pop();
            CL_Obj restart;

            if (cl_restart_top >= CL_MAX_RESTART_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Restart stack overflow");

            /* Build the first-class restart object.  cl_make_restart
             * GC-protects its own parameter copies, but the caller's C
             * locals (handler, tag, name_sym) are not registered and may
             * be stale after compaction.  Read canonical values back from
             * the newly allocated object. */
            restart = cl_make_restart(name_sym, handler, report,
                                      interactive, test, tag);
            {
                CL_Restart *rp = (CL_Restart *)CL_OBJ_TO_PTR(restart);
                cl_restart_stack[cl_restart_top].name    = rp->name;
                cl_restart_stack[cl_restart_top].handler = rp->function;
                cl_restart_stack[cl_restart_top].tag     = rp->tag;
                cl_restart_stack[cl_restart_top].restart = restart;
            }
            cl_restart_top++;
            VM_BREAK;
        }

        VM_CASE(OP_RESTART_POP): {
            uint8_t count = code[ip++];
            cl_restart_top -= count;
            if (cl_restart_top < 0) cl_restart_top = 0;
            VM_BREAK;
        }

        VM_CASE(OP_ASSERT_TYPE): {
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
            VM_BREAK;
        }

        VM_CASE(OP_BLOCK_PUSH): {
            /* Set up NLX block frame for return-from support */
            uint16_t tag_idx = read_u16(code, &ip);
            int32_t block_offset = read_i32(code, &ip);
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
            nlx->mv_count = 1;
            nlx->saved_pending_mark = cl_saved_pending_top;

            if (CL_SETJMP(nlx->buf) == 0) {
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
                cl_saved_pending_top = nlx->saved_pending_mark;
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
                    /* Restore multiple values preserved across NLX */
                    cl_mv_count = nlx->mv_count;
                    { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                        cl_mv_values[mi] = nlx->mv_values[mi]; }
                    cl_vm_push(block_result);
                }
            }
            VM_BREAK;
        }

        VM_CASE(OP_BLOCK_POP): {
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
            VM_BREAK;
        }

        VM_CASE(OP_BLOCK_RETURN): {
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
                            cl_pending_mv_count = cl_mv_count;
                            { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                                cl_pending_mv_values[mi] = cl_mv_values[mi]; }
                            cl_nlx_top = j;
                            CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                        }
                    }
                    /* No interposing UWPROT — longjmp directly to block */
                    cl_nlx_stack[i].result = value;
                    /* Preserve multiple values across NLX */
                    cl_nlx_stack[i].mv_count = cl_mv_count;
                    { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                        cl_nlx_stack[i].mv_values[mi] = cl_mv_values[mi]; }
                    cl_nlx_top = i;
                    CL_LONGJMP(cl_nlx_stack[i].buf, 1);
                }
            }
            cl_error(CL_ERR_GENERAL, "RETURN-FROM: no block named %s",
                     CL_NULL_P(block_tag) ? "NIL" : cl_symbol_name(block_tag));
            VM_BREAK;
        }

        VM_CASE(OP_TAGBODY_PUSH): {
            /* Set up NLX tagbody frame for cross-closure GO support */
            uint16_t id_idx = read_u16(code, &ip);
            int32_t tb_offset = read_i32(code, &ip);
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
            nlx->saved_pending_mark = cl_saved_pending_top;

            if (CL_SETJMP(nlx->buf) == 0) {
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
                cl_saved_pending_top = nlx->saved_pending_mark;
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
            VM_BREAK;
        }

        VM_CASE(OP_TAGBODY_POP): {
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
            VM_BREAK;
        }

        VM_CASE(OP_TAGBODY_GO): {
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
                            cl_pending_mv_count = cl_mv_count;
                            { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                                cl_pending_mv_values[mi] = cl_mv_values[mi]; }
                            cl_nlx_top = j;
                            CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                        }
                    }
                    /* No interposing UWPROT — longjmp directly to tagbody */
                    cl_nlx_stack[i].result = tag_index;
                    cl_nlx_top = i;
                    CL_LONGJMP(cl_nlx_stack[i].buf, 1);
                }
            }
            cl_error(CL_ERR_GENERAL, "GO: tagbody frame not found");
            VM_BREAK;
        }

        VM_CASE(OP_PROGV_BIND): {
            /* Pop values-list, pop symbols-list, push dyn_mark, bind all */
            CL_Obj values_list = cl_vm_pop();
            CL_Obj symbols_list = cl_vm_pop();
            int mark = cl_dyn_top;
            CL_Thread *thr = CT;

            /* Push saved dyn_mark as fixnum */
            cl_vm_push(CL_MAKE_FIXNUM(mark));

            /* Iterate symbols, pair with values */
            while (!CL_NULL_P(symbols_list)) {
                CL_Obj sym_obj = cl_car(symbols_list);
                CL_Obj val;
                CL_Obj old_tlv;

                if (!CL_SYMBOL_P(sym_obj))
                    cl_error(CL_ERR_TYPE, "PROGV: expected symbol, got non-symbol");

                val = !CL_NULL_P(values_list) ? cl_car(values_list) : CL_UNBOUND;

                if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
                    cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");

                old_tlv = cl_tlv_get(thr, sym_obj);
                cl_dyn_stack[cl_dyn_top].symbol = sym_obj;
                cl_dyn_stack[cl_dyn_top].old_value = old_tlv;
                cl_dyn_top++;
                cl_tlv_set(thr, sym_obj, val);

                symbols_list = cl_cdr(symbols_list);
                if (!CL_NULL_P(values_list))
                    values_list = cl_cdr(values_list);
            }
            VM_BREAK;
        }

        VM_CASE(OP_PROGV_UNBIND): {
            /* Pop body result, pop dyn_mark, restore bindings, push result */
            CL_Obj result = cl_vm_pop();
            CL_Obj mark_obj = cl_vm_pop();
            int mark = CL_FIXNUM_VAL(mark_obj);
            cl_dynbind_restore_to(mark);
            cl_vm_push(result);
            VM_BREAK;
        }

        VM_CASE(OP_ARGC): {
            cl_vm_push(CL_MAKE_FIXNUM(frame->nargs));
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_APPLY): {
            /* (apply func arglist) — inline dispatch to avoid C stack nesting */
            CL_Obj arglist;
            CL_SAFEPOINT();
            arglist = cl_vm_pop();
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
                if (!CL_NULL_P(a)) {
                    cl_error(CL_ERR_ARGS, "APPLY: too many arguments (>64) in arglist");
                }
            }

            /* Resolve symbol to its function binding */
            if (CL_SYMBOL_P(apply_func)) {
                CL_Symbol *s = (CL_Symbol *)CL_OBJ_TO_PTR(apply_func);
                apply_func = s->function;
                if (CL_NULL_P(apply_func) || apply_func == CL_UNBOUND)
                    cl_error(CL_ERR_TYPE, "APPLY: symbol has no function binding");
            }
            /* Unwrap funcallable instances (GF struct → discriminating fn). */
            apply_func = cl_unwrap_funcallable(apply_func);

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
                        vm_extra_args_count = n_extra;
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

                        /* Re-derive callee_bc: cl_cons() above may trigger GC
                         * compaction which moves arena objects.  call_func was
                         * GC-protected so its CL_Obj was updated, but the raw
                         * C pointer callee_bc still points to the OLD arena
                         * location (now potentially reused by new allocations). */
                        if (CL_CLOSURE_P(call_func)) {
                            CL_Closure *cl2 = (CL_Closure *)CL_OBJ_TO_PTR(call_func);
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(cl2->bytecode);
                        } else {
                            callee_bc = (CL_Bytecode *)CL_OBJ_TO_PTR(call_func);
                        }
                    }

                    /* Fill remaining locals */
                    while (cl_vm.sp < (int)(new_bp + callee_bc->n_locals))
                        cl_vm_push(CL_NIL);

                    /* Keyword matching */
                    if (has_key) {
                        int allow = (callee_bc->flags & 2) != 0;
                        int ki;
                        /* CLHS 3.4.1.4: signals program-error on odd kwarg count */
                        if (n_extra & 1)
                            cl_error(CL_ERR_ARGS,
                                     "odd number of keyword arguments");
                        if (!allow) {
                            for (ki = 0; ki + 1 < n_extra; ki += 2) {
                                if (vm_extra_args[ki] == KW_ALLOW_OTHER_KEYS &&
                                    !CL_NULL_P(vm_extra_args[ki + 1])) {
                                    allow = 1; break;
                                }
                            }
                        }
                        /* Right-to-left so leftmost duplicate keyword wins. */
                        {
                            int last_ki = n_extra - 2;
                            if (last_ki & 1) last_ki--;
                            for (ki = last_ki; ki >= 0; ki -= 2) {
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
                    }

                    vm_extra_args_count = 0;
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

                    /* DIAG: OP_APPLY frame push */
                    if (0 && cl_vm.fp >= 44) {
                        const char *_fn = "?";
                        if (callee_bc->name != CL_NIL && CL_SYMBOL_P(callee_bc->name))
                            _fn = cl_symbol_name(callee_bc->name);
                        fprintf(stderr, "[DIAG-FP45] APPLY fp=%d fn=%s code=%p\n",
                                cl_vm.fp, _fn, (void *)callee_bc->code);
                        fflush(stderr);
                    }

                    frame = new_frame;
                    code = callee_bc->code;
                    constants = callee_bc->constants;
                    ip = 0;
                }
            } else {
                cl_error(CL_ERR_TYPE, "APPLY: not a callable function");
            }
            VM_BREAK;
        }

        VM_CASE(OP_CATCH): {
            int32_t catch_offset = read_i32(code, &ip);
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
            nlx->mv_count = 1;
            nlx->saved_pending_mark = cl_saved_pending_top;

            if (CL_SETJMP(nlx->buf) == 0) {
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
                cl_saved_pending_top = nlx->saved_pending_mark;
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
                    /* Restore multiple values preserved across NLX */
                    cl_mv_count = nlx->mv_count;
                    { int mi; for (mi = 0; mi < cl_mv_count && mi < CL_MAX_MV; mi++)
                        cl_mv_values[mi] = nlx->mv_values[mi]; }
                    cl_vm_push(throw_result);
                }
            }
            VM_BREAK;
        }

        VM_CASE(OP_UNCATCH): {
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
            VM_BREAK;
        }

        VM_CASE(OP_UWPROT): {
            int32_t uwp_offset = read_i32(code, &ip);
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

            if (CL_SETJMP(nlx->buf) == 0) {
                /* Normal path: save and clear pending throw state so that a
                 * nested unwind-protect inside the cleanup cannot clobber
                 * the outer non-local transfer. */
                if (cl_saved_pending_top >= cl_saved_pending_max)
                    cl_error(CL_ERR_OVERFLOW, "saved-pending stack overflow");
                {
                    CL_SavedPending *sp = &cl_saved_pending_stack[cl_saved_pending_top++];
                    sp->pending_throw    = cl_pending_throw;
                    sp->pending_tag      = cl_pending_tag;
                    sp->pending_value    = cl_pending_value;
                    sp->pending_mv_count = cl_pending_mv_count;
                    { int _mi; for (_mi = 0; _mi < cl_pending_mv_count && _mi < CL_MAX_MV; _mi++)
                        sp->pending_mv_values[_mi] = cl_pending_mv_values[_mi]; }
                    sp->pending_error_code = cl_pending_error_code;
                    strncpy(sp->pending_error_msg, cl_pending_error_msg,
                            sizeof(sp->pending_error_msg) - 1);
                    sp->pending_error_msg[sizeof(sp->pending_error_msg) - 1] = '\0';
                    sp->entered_via_longjmp = 0; /* set to 1 in longjmp branch if triggered */
                }
                cl_pending_throw    = 0;
                cl_pending_tag      = CL_NIL;
                cl_pending_value    = CL_NIL;
                cl_pending_mv_count = 0;
                cl_pending_error_code = 0;
                cl_pending_error_msg[0] = '\0';
                nlx->saved_pending_mark = cl_saved_pending_top;
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
                cl_saved_pending_top = nlx->saved_pending_mark;
                /* The saved slot was pushed at arming time (before the throw).
                 * Update it now with the actual pending state that triggered
                 * this cleanup, so UWRETHROW can re-initiate it if the cleanup
                 * body completes without a new NLX of its own. */
                if (cl_saved_pending_top > 0) {
                    CL_SavedPending *sp =
                        &cl_saved_pending_stack[cl_saved_pending_top - 1];
                    sp->pending_throw    = cl_pending_throw;
                    sp->pending_tag      = cl_pending_tag;
                    sp->pending_value    = cl_pending_value;
                    sp->pending_mv_count = cl_pending_mv_count;
                    { int _mi; for (_mi = 0;
                                    _mi < cl_pending_mv_count && _mi < CL_MAX_MV;
                                    _mi++)
                        sp->pending_mv_values[_mi] = cl_pending_mv_values[_mi]; }
                    sp->pending_error_code = cl_pending_error_code;
                    strncpy(sp->pending_error_msg, cl_pending_error_msg,
                            sizeof(sp->pending_error_msg) - 1);
                    sp->pending_error_msg[sizeof(sp->pending_error_msg) - 1] = '\0';
                    sp->entered_via_longjmp = 1; /* our longjmp fired — UWRETHROW must rethrow */
                }
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
            VM_BREAK;
        }

        VM_CASE(OP_UWPOP): {
            /* Normal exit from protected form — pop NLX frame.
             * Must find the matching UWPROT frame, as tail calls in called
             * functions may have leaked BLOCK/CATCH frames above it.
             * NOTE: we do NOT clear cl_pending_throw here; OP_UWPROT's
             * arming already cleared it, and OP_UWRETHROW restores it. */
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
            VM_BREAK;
        }

        VM_CASE(OP_UWRETHROW): {
            /* After cleanup forms: pop our saved pending snapshot (pushed at
             * OP_UWPROT arming) and re-initiate pending throw/error if any.
             *
             * A UWRETHROW re-initiates a pending NLX in two situations:
             *   1. A new NLX was initiated during the cleanup body itself
             *      (cl_pending_throw != 0 at entry — rethrow_active).
             *   2. Our own UWPROT's longjmp was triggered (entered_via_longjmp=1
             *      in the saved entry) AND after restoring the saved state the
             *      pending NLX is still live. This handles the case where a nested
             *      CATCH inside the cleanup consumed an intermediate throw, clearing
             *      cl_pending_throw, even though the outer NLX (the one that armed
             *      our UWPROT via longjmp) still needs to be re-initiated.
             *
             * If neither condition holds — our UWPROT entered normally and no new
             * NLX fired during cleanup — leave the restored pending state parked in
             * the globals. The enclosing UWP's UWRETHROW (or the next NLX search
             * point) will see it. Re-initiating it here would skip cleanup code in
             * the enclosing UWP that runs after our call site. */
            int rethrow_active = (cl_pending_throw != 0);
            int should_rethrow = 0;
            {
                CL_SavedPending saved;
                if (cl_saved_pending_top > 0) {
                    saved = cl_saved_pending_stack[--cl_saved_pending_top];
                } else {
                    /* Should not happen; initialize to zero for safety. */
                    saved.pending_throw = 0;
                    saved.pending_tag = CL_NIL;
                    saved.pending_value = CL_NIL;
                    saved.pending_mv_count = 0;
                    saved.pending_error_code = 0;
                    saved.pending_error_msg[0] = '\0';
                    saved.entered_via_longjmp = 0;
                }
                if (cl_pending_throw == 0) {
                    /* Cleanup body completed without a new NLX — restore the
                     * pending state that was active before UWPROT armed. */
                    cl_pending_throw      = saved.pending_throw;
                    cl_pending_tag        = saved.pending_tag;
                    cl_pending_value      = saved.pending_value;
                    cl_pending_mv_count   = saved.pending_mv_count;
                    { int _mi; for (_mi = 0; _mi < saved.pending_mv_count && _mi < CL_MAX_MV; _mi++)
                        cl_pending_mv_values[_mi] = saved.pending_mv_values[_mi]; }
                    cl_pending_error_code = saved.pending_error_code;
                    strncpy(cl_pending_error_msg, saved.pending_error_msg,
                            sizeof(cl_pending_error_msg) - 1);
                    cl_pending_error_msg[sizeof(cl_pending_error_msg) - 1] = '\0';
                }
                /* If cl_pending_throw != 0 a new NLX was initiated during the
                 * cleanup; fall through to the rethrow logic below using the
                 * new (current) pending state, discarding `saved`. */
                should_rethrow = rethrow_active ||
                                 (saved.entered_via_longjmp && cl_pending_throw != 0);
            }
            if (should_rethrow && cl_pending_throw == 1) {
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
                                CL_LONGJMP(cl_nlx_stack[j].buf, 1);
                            }
                        }
                        /* No interposing UWPROT, go directly to target */
                        cl_pending_throw = 0;
                        cl_nlx_stack[i].result = pval;
                        cl_nlx_stack[i].mv_count = cl_pending_mv_count;
                        { int mi; for (mi = 0; mi < cl_pending_mv_count && mi < CL_MAX_MV; mi++)
                            cl_nlx_stack[i].mv_values[mi] = cl_pending_mv_values[mi]; }
                        cl_nlx_top = i;
                        CL_LONGJMP(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No catch found — signal error */
                cl_pending_throw = 0;
                cl_error(CL_ERR_GENERAL, "No catch for tag during re-throw");
            } else if (should_rethrow && cl_pending_throw == 2) {
                /* Re-throw error: find interposing UWPROT or error frame (skip stale) */
                int i;
                for (i = cl_nlx_top - 1; i >= 0; i--) {
                    if (cl_nlx_stack[i].type == CL_NLX_UWPROT &&
                        !nlx_frame_is_stale(&cl_nlx_stack[i])) {
                        cl_nlx_top = i;
                        CL_LONGJMP(cl_nlx_stack[i].buf, 1);
                    }
                }
                /* No more UWPROT — propagate to error handler.
                 * Restore VM state to before this cl_vm_eval call
                 * so subsequent evaluations start from a clean state. */
                {
                    int err_code = cl_pending_error_code;
                    cl_pending_throw = 0;
                    cl_nlx_top = 0;
                    cl_saved_pending_top = 0;
                    cl_dynbind_restore_to(0);
                    cl_handler_top = 0;
                    cl_restart_top = 0;
                    cl_vm.fp = base_fp;
                    cl_vm.sp = cl_vm.frames[base_fp].bp;
                    cl_error_code = err_code;
                    strncpy(cl_error_msg, cl_pending_error_msg, sizeof(cl_error_msg) - 1);
                    cl_error_msg[sizeof(cl_error_msg) - 1] = '\0';
                    if (cl_error_frame_top > 0) {
                        /* Don't decrement here — CL_UNCATCH at the catch site pops */
                        CL_LONGJMP(cl_error_frames[cl_error_frame_top - 1].buf, err_code);
                    }
                    platform_write_string("FATAL ERROR: ");
                    platform_write_string(cl_error_msg);
                    platform_write_string("\n");
                    exit(1);
                }
            }
            /* !should_rethrow: nop — pending state parked for enclosing UWP */
            VM_BREAK;
        }

        VM_CASE(OP_MV_LOAD): {
            uint8_t index = code[ip++];
            cl_vm_push((int)index < cl_mv_count ? cl_mv_values[index] : CL_NIL);
            VM_BREAK;
        }

        VM_CASE(OP_MV_TO_LIST): {
            /* Pops primary from stack, builds list from MV buffer.
             * For single values (inline opcodes), uses the popped primary.
             *
             * cl_mv_count == 0 normally means (values) — no values, empty
             * list.  But many value-propagating opcodes (OP_LOAD, OP_DUP,
             * OP_POP, …) don't reset cl_mv_count, so a non-NIL primary on
             * the stack with cl_mv_count == 0 indicates a stale leak from
             * an earlier (values) call rather than a true zero-value
             * return.  Treat non-NIL primary with count 0 as a single
             * value to preserve the value through (multiple-value-list …)
             * and unwind-protect's MV round-trip.  Legitimate (values)
             * keeps primary=NIL, so the empty-list case still fires for
             * it. */
            CL_Obj primary = cl_vm_pop();
            CL_Obj list = CL_NIL;
            if (cl_mv_count == 0) {
                if (!CL_NULL_P(primary))
                    list = cl_cons(primary, CL_NIL);
            } else if (cl_mv_count == 1) {
                list = cl_cons(primary, CL_NIL);
            } else {
                int i;
                for (i = cl_mv_count - 1; i >= 0; i--)
                    list = cl_cons(cl_mv_values[i], list);
            }
            cl_vm_push(list);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_NTH_VALUE): {
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
            VM_BREAK;
        }

        VM_CASE(OP_DYNBIND): {
            uint16_t idx = read_u16(code, &ip);
            CL_Obj sym = constants[idx];
            CL_Obj new_val = cl_vm_pop();
            CL_Thread *thr = CT;
            CL_Obj old_tlv = cl_tlv_get(thr, sym);
            if (cl_dyn_top >= CL_MAX_DYN_BINDINGS)
                cl_error(CL_ERR_OVERFLOW, "Dynamic binding stack overflow");
            cl_dyn_stack[cl_dyn_top].symbol = sym;
            cl_dyn_stack[cl_dyn_top].old_value = old_tlv; /* CL_TLV_ABSENT if none */
            cl_dyn_top++;
            cl_tlv_set(thr, sym, new_val);
            if (sym == SYM_STAR_PACKAGE)
                cl_sync_current_package_from_dynamic();
            VM_BREAK;
        }

        VM_CASE(OP_DYNUNBIND): {
            uint8_t count = code[ip++];
            cl_dynbind_restore_to(cl_dyn_top - count);
            VM_BREAK;
        }

        VM_CASE(OP_MV_RESET):
            cl_mv_count = 1;
            VM_BREAK;

        VM_CASE(OP_STRUCT_REF): {
            uint8_t idx = code[ip++];
            CL_Obj obj = cl_vm_pop();
            CL_Struct *st;
            if (!CL_STRUCT_P(obj))
                cl_signal_type_error(obj, "STRUCTURE", "%STRUCT-REF");
            st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            if ((uint32_t)idx >= st->n_slots)
                cl_error(CL_ERR_ARGS,
                         "%%STRUCT-REF: index %u out of range (n_slots=%u)",
                         (unsigned)idx, (unsigned)st->n_slots);
            cl_vm_push(st->slots[idx]);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_STRUCT_SET): {
            uint8_t idx = code[ip++];
            CL_Obj val = cl_vm_pop();
            CL_Obj obj = cl_vm_pop();
            CL_Struct *st;
            if (!CL_STRUCT_P(obj))
                cl_signal_type_error(obj, "STRUCTURE", "%STRUCT-SET");
            st = (CL_Struct *)CL_OBJ_TO_PTR(obj);
            if ((uint32_t)idx >= st->n_slots)
                cl_error(CL_ERR_ARGS,
                         "%%STRUCT-SET: index %u out of range (n_slots=%u)",
                         (unsigned)idx, (unsigned)st->n_slots);
            st->slots[idx] = val;
            cl_vm_push(val);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_AMIGA_CALL): {
            uint16_t sym_idx = read_u16(code, &ip);
            int16_t  off     = read_i16(code, &ip);
            uint32_t regspec = (uint32_t)read_i32(code, &ip);
            uint8_t  n_args  = code[ip++];
            CL_Obj base_sym, base_val, result;
            CL_ForeignPtr *bfp;

            CL_SAFEPOINT();
            if (!constants)
                cl_error(CL_ERR_GENERAL,
                         "OP_AMIGA_CALL with NULL constants ptr");
            base_sym = constants[sym_idx];
            base_val = cl_symbol_value(base_sym);
            if (base_val == CL_UNBOUND)
                cl_error(CL_ERR_UNBOUND,
                         "OP_AMIGA_CALL: unbound library base %s",
                         cl_symbol_name(base_sym));
            if (!CL_FOREIGN_POINTER_P(base_val))
                cl_error(CL_ERR_TYPE,
                         "OP_AMIGA_CALL: %s is not a foreign pointer",
                         cl_symbol_name(base_sym));
            bfp = (CL_ForeignPtr *)CL_OBJ_TO_PTR(base_val);

            /* Args sit at [sp-n_args .. sp-1]; pop after dispatch. */
            result = cl_amiga_ffi_call_dispatch(
                bfp->address, off, regspec, (int)n_args,
                &cl_vm.stack[cl_vm.sp - n_args]);
            cl_vm.sp -= n_args;
            cl_vm_push(result);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_RPLACA): {
            CL_Obj new_car = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACA: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->car = new_car;
            cl_vm_push(new_car);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_RPLACD): {
            CL_Obj new_cdr = cl_vm_pop();
            CL_Obj cons_obj = cl_vm_pop();
            CL_Cons *cell;
            if (!CL_CONS_P(cons_obj))
                cl_error(CL_ERR_TYPE, "RPLACD: not a cons");
            cell = (CL_Cons *)CL_OBJ_TO_PTR(cons_obj);
            cell->cdr = new_cdr;
            cl_vm_push(new_cdr);
            cl_mv_count = 1;
            VM_BREAK;
        }

        VM_CASE(OP_ASET): {
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
            } else if (CL_ANY_STRING_P(vec_obj)) {
                uint32_t slen = cl_string_length(vec_obj);
                if (idx < 0 || (uint32_t)idx >= slen)
                    cl_error(CL_ERR_ARGS, "ASET: index %d out of range (0-%lu)",
                             (int)idx, (unsigned long)(slen - 1));
                if (!CL_CHAR_P(val))
                    cl_error(CL_ERR_TYPE, "ASET: value must be a character for string");
                cl_string_set_char_at(vec_obj, (uint32_t)idx, CL_CHAR_VAL(val));
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
            VM_BREAK;
        }

        VM_DEFAULT:
#ifndef USE_COMPUTED_GOTO
        unknown_opcode:
#endif
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
#ifndef USE_COMPUTED_GOTO
        }  /* end switch */
    }  /* end for */
#endif

/* end of inline hot-path macros */
#undef cl_vm_push
#undef cl_vm_pop
#undef cl_vm
#define cl_vm (CT->vm)
#undef cl_mv_count
#define cl_mv_count (CT->mv_count)
#undef cl_mv_values
#define cl_mv_values (CT->mv_values)

#undef VM_DISPATCH
#undef VM_CASE
#undef VM_BREAK
#undef VM_DEFAULT
#ifdef USE_COMPUTED_GOTO
#undef USE_COMPUTED_GOTO
#endif
}  /* end cl_vm_run */

/*
 * cl_vm_eval — execute a bytecode object.
 * Thin wrapper: pushes initial frame, calls cl_vm_run.
 */
CL_Obj cl_vm_eval(CL_Obj bytecode_obj)
{
    CL_Bytecode *bc;
    CL_Frame *frame;
    int base_fp, base_nlx, saved_sp;
    CL_Obj result;

    cl_check_c_stack("cl_vm_eval");
#ifdef DEBUG_VM
    vm_eval_depth++;
    if (vm_eval_depth > vm_eval_max_depth)
        vm_eval_max_depth = vm_eval_depth;
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
    saved_sp = cl_vm.sp;
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
    frame->nlx_level = cl_nlx_top;

    /* Allocate space for locals */
    {
        int i;
        for (i = 0; i < bc->n_locals; i++)
            cl_vm_push(CL_NIL);
    }

    /* Targeted crash diagnostic: cl_vm_eval path (disabled) */
#if 0
    if (bc->name != CL_NIL && CL_SYMBOL_P(bc->name)) {
        const char *_fn = cl_symbol_name(bc->name);
        if (_fn && _fn[0] == 'C' && _fn[1] == 'L' && _fn[2] == 'A' &&
            _fn[3] == 'S' && _fn[4] == 'S' && _fn[5] == '-' &&
            _fn[6] == 'S' && _fn[7] == 'L' && _fn[8] == 'O' &&
            _fn[9] == 'T') {
            fprintf(stderr, "[DIAG-EVAL] Entering %s: code=%p fp=%d sp=%d\n",
                    _fn, (void *)bc->code, cl_vm.fp, cl_vm.sp);
            fflush(stderr);
        }
    }
#endif

    result = cl_vm_run(base_fp, base_nlx);

    /* Restore fp/sp: OP_HALT doesn't decrement fp, so the eval frame
     * would leak.  Without this restore, each cl_vm_eval call leaves
     * fp one higher, corrupting the frame stack for the caller. */
    cl_vm.fp = base_fp;
    cl_vm.sp = saved_sp;

#ifdef DEBUG_VM
    vm_eval_depth--;
#endif
    return result;
}
